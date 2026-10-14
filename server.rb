#!/usr/bin/env ruby
# Servidor sencillo del semaforo ESP32 (solo libreria estandar de Ruby).
#
# - Sirve la pagina `index.html` en http://localhost:8000/
# - Recibe la senal del ESP32 con  POST /api/state  {"light":"red","remaining":12}
# - La pagina consulta          GET  /api/state  cada 500 ms y muestra
#   el foco activo + cuenta regresiva + pin activo.
# - Si el ESP32 no manda senal por >4 s, el servidor SIMULA el ciclo
#   (verde 15 s -> amarillo 5 s -> rojo 20 s) para probar sin hardware.
#
# Uso:
#     ruby server.rb [puerto]
#     p.ej. ruby server.rb 8000

require 'webrick'
require 'json'

$stdout.sync = true # ver lineas [USB]/[ESP32] en vivo en la terminal

DURATIONS = { 'green' => 15, 'yellow' => 5, 'red' => 20 }.freeze
PINS = { 'green' => 5, 'yellow' => 4, 'red' => 2 }.freeze # rojo en pin 2 (pedido)
ORDER = %w[green yellow red].freeze
CYCLE_TOTAL = ORDER.sum { |l| DURATIONS[l] }.to_f # 40 s
ESP_TIMEOUT = 4.0 # sin POST en 4 s -> modo demo

$lock = Mutex.new
$esp_light = nil
$esp_remaining = 0.0
$esp_at = 0.0
$boot = Process.clock_gettime(Process::CLOCK_MONOTONIC)

def simulated_state(now)
  pos = (now - $boot) % CYCLE_TOTAL
  acc = 0.0
  ORDER.each do |light|
    d = DURATIONS[light].to_f
    return [light, (acc + d) - pos] if pos < acc + d

    acc += d
  end
  ['red', DURATIONS['red'].to_f]
end

def current_state
  now = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  light, remaining, fresh = $lock.synchronize do
    if $esp_light && (now - $esp_at) < ESP_TIMEOUT
      [$esp_light, [0.0, $esp_remaining - (now - $esp_at)].max, true]
    else
      l, r = simulated_state(now)
      [l, r, false]
    end
  end
  {
    'light' => light,
    'remaining' => remaining.round(1),
    'duration' => DURATIONS[light],
    'pin' => PINS[light],
    'connected' => fresh,
    'simulated' => !fresh,
    'durations' => DURATIONS,
    'pins' => PINS
  }
end

def note_esp(light, remaining)
  now = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  $lock.synchronize do
    $esp_light = light
    $esp_remaining = remaining
    $esp_at = now
  end
end

# --- Lector USB (opcional) ---
# Lee lineas "STATE:<luz>:<restante>" del ESP32 conectado por cable USB y
# actualiza el estado que ve la pagina. Sin ESP32, sigue el modo demo.
# Puerto: variable SERIAL_PORT o autodeteccion de /dev/cu.* tipicos.
SERIAL_BAUD = (ENV['SERIAL_BAUD'] || 115200).to_i

def detect_serial_port
  env = ENV['SERIAL_PORT']
  return env unless env.nil? || env.empty?

  ['/dev/cu.usbserial*', '/dev/cu.SLAB_USBtoUART*',
   '/dev/cu.wchusbserial*', '/dev/cu.usbmodem*'].each do |pat|
    found = Dir.glob(pat).reject { |p| p.include?('Bluetooth') }.first
    return found if found
  end
  nil
end

def start_serial_reader
  port = detect_serial_port
  unless port
    puts 'USB: no se detecto ESP32 en /dev/cu.*. Modo demo hasta conectar el cable.'
    puts '     Tip: SERIAL_PORT=/dev/cu.usbserial-XXXX ruby server.rb 8000'
    return
  end
  puts "USB: leyendo ESP32 en #{port} a #{SERIAL_BAUD} baudios..."
  Thread.new do
    loop do
      begin
        # stty puede fallar con dispositivos no-TTY; no es fatal
        system('stty', '-f', port, SERIAL_BAUD.to_s, 'raw', '-echo',
               out: File::NULL, err: File::NULL)
        File.open(port, 'r') do |f|
          f.each_line do |line|
            m = line.match(/STATE:(red|yellow|green):(\d+(?:\.\d+)?)/)
            next unless m

            light = m[1]
            remaining = [[m[2].to_f, 0.0].max, DURATIONS[light].to_f].min
            note_esp(light, remaining)
            puts "[USB] #{light} restante=#{remaining.round(1)}s"
          end
        end
      rescue StandardError => e
        puts "USB: error (#{e.message}), reintentando en 3 s..."
      end
      sleep 3
    end
  end
end

def handle_api(req, res)
  res['Access-Control-Allow-Origin'] = '*'
  case req.request_method
  when 'OPTIONS'
    res['Access-Control-Allow-Methods'] = 'GET, POST, OPTIONS'
    res['Access-Control-Allow-Headers'] = 'Content-Type'
    res.status = 204
  when 'GET'
    res['Content-Type'] = 'application/json'
    res.body = JSON.generate(current_state)
  when 'POST'
    begin
      payload = JSON.parse(req.body.to_s)
    rescue JSON::ParserError
      res.status = 400
      res['Content-Type'] = 'application/json'
      res.body = JSON.generate({ 'ok' => false, 'error' => 'JSON invalido' })
      return
    end
    light = payload['light'].to_s.downcase.strip
    unless DURATIONS.key?(light)
      res.status = 400
      res['Content-Type'] = 'application/json'
      res.body = JSON.generate({ 'ok' => false, 'error' => 'light debe ser red|yellow|green' })
      return
    end
    begin
      remaining = Float(payload.fetch('remaining', DURATIONS[light]))
    rescue StandardError
      remaining = DURATIONS[light].to_f
    end
    remaining = [[remaining, 0.0].max, DURATIONS[light].to_f].min
    note_esp(light, remaining)
    puts "[ESP32] #{light} restante=#{remaining.round(1)}s"
    res['Content-Type'] = 'application/json'
    res.body = JSON.generate({ 'ok' => true, 'state' => current_state })
  else
    res.status = 405
  end
end

port = (ARGV[0] || 8000).to_i
server = WEBrick::HTTPServer.new(
  Port: port,
  BindAddress: '0.0.0.0',
  DocumentRoot: File.dirname(__FILE__),
  AccessLog: [],
  Logger: WEBrick::Log.new(File::NULL)
)
server.mount_proc('/api/state', &method(:handle_api))
server.mount_proc('/api/status', &method(:handle_api))
trap('INT') { server.shutdown }

puts "Semaforo: http://localhost:#{port}/  (POST del ESP32 a /api/state)"
start_serial_reader
server.start
