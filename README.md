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
# (alternativa si tienes las Command Line Tools: python3 server.py 8000)
```

Abre http://localhost:8000/ — sin ESP32 verás el ciclo en **MODO DEMO**.
Al conectar el ESP32, la página cambia sola a **ESP32 conectado**.

### 2. ESP32

1. Abre `semaforo/semaforo.ino` en Arduino IDE (placa "ESP32 Dev Module").
2. Instala la librería incluida por defecto (WiFi, HTTPClient — ya vienen con el core ESP32).
3. Configura arriba del archivo:
   ```cpp
   const char* WIFI_SSID  = "TU_WIFI";
   const char* WIFI_PASS  = "TU_PASSWORD";
   const char* SERVER_URL = "http://192.168.1.50:8000/api/state"; // IP de ESTE equipo
   ```
   La IP debe ser la de este equipo en la misma WiFi que el ESP32.
4. Conecta el ESP32 por USB, sube el sketch y abre el Monitor Serie (115200).
   Verás líneas `STATE:red:17` y `[ESP32] red restante=...` en la terminal del servidor.

### 3. Protocolo (por si quieres otro cliente)

```bash
# La ESP32 envía:
POST /api/state   {"light":"red", "remaining":17.2}

# La página consulta:
GET  /api/state
# → {"light":"red","remaining":17.2,"duration":20,"pin":2,
#    "connected":true,"simulated":false,...}
```

Si no hay POST en 4 s, el servidor simula el ciclo y responde `"simulated": true.
