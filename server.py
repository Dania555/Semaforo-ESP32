#!/usr/bin/env python3
"""Servidor sencillo del semaforo ESP32 (solo libreria estandar).

- Sirve la pagina `index.html` en http://localhost:8000/
- Recibe la senal del ESP32 con  POST /api/state  {"light":"red","remaining":12}
- La pagina consulta          GET  /api/state  cada 500 ms y muestra
  el foco activo + cuenta regresiva + pin activo.
- Si el ESP32 no manda senal por >4 s, el servidor SIMULA el ciclo
  (verde 15 s -> amarillo 5 s -> rojo 20 s) para poder probar la pagina
  sin hardware. El JSON incluye "simulated": true en ese caso.

Uso:
    python3 server.py [puerto]
    p.ej. python3 server.py 8000
"""
import glob
import json
import os
import re
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

DURATIONS = {"green": 15, "yellow": 5, "red": 20}
PINS = {"green": 5, "yellow": 4, "red": 2}  # rojo en pin 2 (pedido), otros libres
ORDER = ["green", "yellow", "red"]
CYCLE_TOTAL = sum(DURATIONS[l] for l in ORDER)  # 40 s
ESP_TIMEOUT = 4.0  # sin POST en 4 s -> modo demo

_state_lock = threading.Lock()
_esp_light = None       # 'red' | 'yellow' | 'green' | None
_esp_remaining = 0.0
_esp_at = 0.0           # time.monotonic() del ultimo POST
_boot = time.monotonic()


def _simulated_state(now):
    """Calcula el estado del ciclo simulado en el instante `now` (monotonic)."""
    pos = (now - _boot) % CYCLE_TOTAL
    acc = 0.0
    for light in ORDER:
        d = DURATIONS[light]
        if pos < acc + d:
            return light, (acc + d) - pos
        acc += d
    return "red", DURATIONS["red"]


def current_state():
    """Estado actual: prioriza senal del ESP32, si no hay -> simulacion."""
    now = time.monotonic()
    with _state_lock:
        fresh = _esp_light is not None and (now - _esp_at) < ESP_TIMEOUT
        if fresh:
            light = _esp_light
            remaining = max(0.0, _esp_remaining - (now - _esp_at))
        else:
            light, remaining = _simulated_state(now)
    return {
        "light": light,
        "remaining": round(remaining, 1),
        "duration": DURATIONS[light],
        "pin": PINS[light],
        "connected": fresh,
        "simulated": not fresh,
        "durations": DURATIONS,
        "pins": PINS,
    }


def _note_esp(light, remaining):
    """Guarda la ultima senal del ESP32 (la usa la pagina web)."""
    now = time.monotonic()
    with _state_lock:
        global _esp_light, _esp_remaining, _esp_at
        _esp_light, _esp_remaining, _esp_at = light, remaining, now


STATE_RE = re.compile(r"STATE:(red|yellow|green):(\d+(?:\.\d+)?)")
SERIAL_BAUD = int(os.environ.get("SERIAL_BAUD", "115200"))


def _detect_serial_port():
    env = os.environ.get("SERIAL_PORT")
    if env:
        return env
    for pat in ("/dev/cu.usbserial*", "/dev/cu.SLAB_USBtoUART*",
                "/dev/cu.wchusbserial*", "/dev/cu.usbmodem*",
                "COM3", "COM4", "COM5", "COM6"):
        for p in sorted(glob.glob(pat)):
            if "Bluetooth" not in p:
                return p
        if os.name == "nt" and os.path.exists(pat):
            return pat
    return None


def _start_serial_reader():
    """Hilo que lee STATE:... del ESP32 por cable USB (opcional)."""
    port = _detect_serial_port()
    if not port:
        print("USB: no se detecto ESP32. Modo demo hasta conectar el cable.")
        print("     Tip: SERIAL_PORT=/dev/cu.usbserial-XXXX python3 server.py 8000")
        return
    try:
        import serial
    except ImportError:
        print("USB: falta pyserial -> pip install pyserial (sin el, solo modo demo).")
        return
    print(f"USB: leyendo ESP32 en {port} a {SERIAL_BAUD} baudios...")

    def _run():
        while True:
            try:
                with serial.Serial(port, SERIAL_BAUD, timeout=1) as ser:
                    while True:
                        line = ser.readline().decode("utf-8", "replace")
                        m = STATE_RE.search(line)
                        if not m:
                            continue
                        light = m.group(1)
                        remaining = max(0.0, min(float(DURATIONS[light]), float(m.group(2))))
                        _note_esp(light, remaining)
                        print(f"[USB] {light} restante={remaining:.1f}s", flush=True)
            except Exception as e:
                print(f"USB: error ({e}), reintentando en 3 s...", flush=True)
            time.sleep(3)

    threading.Thread(target=_run, daemon=True).start()


class Handler(BaseHTTPRequestHandler):
    server_version = "SemaforoServer/1.0"

    def _send_json(self, obj, code=200):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_GET(self):
        if self.path.split("?")[0] in ("/api/state", "/api/status"):
            self._send_json(current_state())
            return
        # Sirve index.html en / (y archivos estaticos del directorio)
        path = self.path.split("?")[0]
        if path == "/":
            path = "/index.html"
        f = Path(__file__).parent / path.lstrip("/")
        if f.is_file() and f.suffix in (".html", ".css", ".js", ".png", ".ico"):
            data = f.read_bytes()
            ctype = {".html": "text/html; charset=utf-8", ".css": "text/css",
                     ".js": "text/javascript"}.get(f.suffix, "application/octet-stream")
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        self.send_response(404)
        self.end_headers()

    def do_POST(self):
        if self.path.split("?")[0] not in ("/api/state", "/api/status"):
            self.send_response(404)
            self.end_headers()
            return
        try:
            n = int(self.headers.get("Content-Length", 0))
            payload = json.loads(self.rfile.read(n).decode("utf-8") or "{}")
        except Exception:
            self._send_json({"ok": False, "error": "JSON invalido"}, 400)
            return
        light = str(payload.get("light", "")).lower().strip()
        if light not in DURATIONS:
            self._send_json({"ok": False, "error": "light debe ser red|yellow|green"}, 400)
            return
        try:
            remaining = float(payload.get("remaining", DURATIONS[light]))
        except (TypeError, ValueError):
            remaining = float(DURATIONS[light])
        remaining = max(0.0, min(float(DURATIONS[light]), remaining))
        _note_esp(light, remaining)
        print(f"[ESP32] {light} restante={remaining:.1f}s", flush=True)
        self._send_json({"ok": True, "state": current_state()})

    def log_message(self, *args):
        pass  # silencia log por defecto; el POST ya imprime su linea


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    srv = ThreadingHTTPServer(("0.0.0.0", port), Handler)
    print(f"Semaforo: http://localhost:{port}/  (POST del ESP32 a /api/state)")
    _start_serial_reader()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
