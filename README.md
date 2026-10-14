# Semáforo ESP32 + Página Web

Diseño basado en el Figma: caja `STATUS` gris a la izquierda, semáforo vertical a la derecha
(verde arriba, amarillo en medio, rojo abajo).

## Archivos

| Archivo | Qué es |
|---|---|
| `index.html` | Página web (muestra foco activo, cuenta regresiva y pin activo) |
| `server.rb` | Servidor recomendado en este Mac (Ruby ya viene instalado, sin instalar nada) |
| `server.py` | Servidor alternativo en Python (requiere Command Line Tools de Apple) |
| `semaforo/semaforo.ino` | Código Arduino para la ESP32 |

## Pines y tiempos

- Rojo: pin **2**, 20 s · Amarillo: pin **4**, 5 s · Verde: pin **5**, 15 s
- Secuencia: verde → amarillo → rojo → repetir.

## Cómo usar

### 1. Servidor + página (en este equipo)

```bash
ruby server.rb 8000
# (alternativa si tienes las Command Line Tools: python3 server.py 8000;
#  ese modo USB requiere: pip install pyserial)
```

Abre http://localhost:8000/ — sin ESP32 verás el ciclo en **MODO DEMO**.
Al conectar el ESP32 por USB, el servidor detecta el puerto solo
(`/dev/cu.usbserial*`, `SLAB_USBtoUART*`, `wchusbserial*`, `usbmodem*`)
y la página cambia a **ESP32 conectado**. Si no lo detecta, fíjalo así:

```bash
SERIAL_PORT=/dev/cu.usbserial-0001 ruby server.rb 8000
```

### 2. ESP32 (por cable USB, sin WiFi)

1. Abre `semaforo/semaforo.ino` en Arduino IDE (placa "ESP32 Dev Module").
2. El sketch no usa WiFi: no hay nada que configurar.
3. Conecta el ESP32 por USB, elige su puerto en Herramientas → Puerto,
   sube el sketch y abre el Monitor Serie (115200).
   Verás líneas `STATE:red:17` — esas las lee el servidor para la página.

### 3. Protocolo (por si quieres otro cliente)

```bash
# La ESP32 envía por USB (115200 baudios):
STATE:red:17
# (formato anterior por WiFi, aún soportado: POST /api/state {"light":"red","remaining":17.2})

# La página consulta:
GET  /api/state
# → {"light":"red","remaining":17.2,"duration":20,"pin":2,
#    "connected":true,"simulated":false,...}
```

Si no hay POST en 4 s, el servidor simula el ciclo y responde `"simulated": true.
