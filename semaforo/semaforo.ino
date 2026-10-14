/*
 * Semaforo ESP32 — secuencia de semaforo + aviso al servidor web.
 *
 *  Pines y duraciones (pedidas):
 *    ROJO     -> GPIO 2, 20 segundos
 *    AMARILLO -> GPIO 4,  5 segundos
 *    VERDE    -> GPIO 5, 15 segundos
 *
 *  Secuencia: VERDE (15 s) -> AMARILLO (5 s) -> ROJO (20 s) -> repetir.
 *
 *  Como avisa a la pagina:
 *    La ESP32 se conecta por WiFi y hace POST JSON al servidor
 *    (server.py) cada 1 s y en cada cambio de luz:
 *        POST http://<IP-DE-TU-PC>:8000/api/state
 *        {"light":"red", "remaining":12.5}
 *    El servidor guarda ese estado y la pagina lo muestra con
 *    cuenta regresiva. Ademas imprime por Serial "STATE:red:12"
 *    como respaldo (monitor serie / conexion USB directa).
 *
 *  Configura abajo WIFI_SSID, WIFI_PASS y SERVER_URL con la IP de tu PC.
 */

#include <WiFi.h>
#include <HTTPClient.h>

// ---------- CONFIGURA ESTO ----------
const char* WIFI_SSID  = "TU_WIFI";
const char* WIFI_PASS  = "TU_PASSWORD";
const char* SERVER_URL = "http://192.168.1.50:8000/api/state";  // IP de tu PC + puerto
// ------------------------------------

#define PIN_ROJO     2
#define PIN_AMARILLO 4
#define PIN_VERDE    5

#define SEG_ROJO     20
#define SEG_AMARILLO 5
#define SEG_VERDE    15

enum Luz { VERDE, AMARILLO, ROJO };

Luz luzActual = VERDE;
unsigned long inicioLuz = 0;      // millis() cuando cambio la luz
unsigned long ultimoEnvio = 0;    // throttle de POST

int duracionDe(Luz l) {
  if (l == ROJO) return SEG_ROJO;
  if (l == AMARILLO) return SEG_AMARILLO;
  return SEG_VERDE;
}

const char* nombreDe(Luz l) {
  if (l == ROJO) return "red";
  if (l == AMARILLO) return "yellow";
  return "green";
}

void aplicarLuces() {
  digitalWrite(PIN_ROJO,     luzActual == ROJO ? HIGH : LOW);
  digitalWrite(PIN_AMARILLO, luzActual == AMARILLO ? HIGH : LOW);
  digitalWrite(PIN_VERDE,    luzActual == VERDE ? HIGH : LOW);
}

float restanteSeg() {
  long transcurrido = (long)(millis() - inicioLuz) / 1000L;
  float r = (float)duracionDe(luzActual) - (float)transcurrido;
  return r < 0 ? 0 : r;
}

// Manda el estado al servidor (HTTP) y al monitor serie (USB).
void enviarEstado(bool forzar) {
  unsigned long ahora = millis();
  if (!forzar && ahora - ultimoEnvio < 1000) return;  // 1 POST por segundo max.
  ultimoEnvio = ahora;

  float rest = restanteSeg();
  const char* nombre = nombreDe(luzActual);

  // 1) Respaldo por USB: linea facil de leer para depurar.
  Serial.print("STATE:");
  Serial.print(nombre);
  Serial.print(":");
  Serial.println((int)rest);

  // 2) Senal para la pagina web via servidor.
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");
  String body = String("{\"light\":\"") + nombre +
                String("\",\"remaining\":") + String(rest, 1) +
                String(",\"duration\":") + duracionDe(luzActual) + String("}");
  int code = http.POST(body);
  if (code < 200 || code >= 300) {
    Serial.print("HTTP error: ");
    Serial.println(code);
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_ROJO, OUTPUT);
  pinMode(PIN_AMARILLO, OUTPUT);
  pinMode(PIN_VERDE, OUTPUT);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Conectando WiFi");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi OK, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Sin WiFi: el semaforo sigue, pero la pagina no se actualizara.");
  }

  luzActual = VERDE;
  inicioLuz = millis();
  aplicarLuces();
  enviarEstado(true);
}

void loop() {
  unsigned long ahora = millis();

  // Cambio de luz cuando se agota la duracion.
  if (ahora - inicioLuz >= (unsigned long)duracionDe(luzActual) * 1000UL) {
    if (luzActual == VERDE)         luzActual = AMARILLO;
    else if (luzActual == AMARILLO) luzActual = ROJO;
    else                            luzActual = VERDE;
    inicioLuz = ahora;
    aplicarLuces();
    enviarEstado(true);  // aviso inmediato del cambio
  }

  enviarEstado(false);  // heartbeat 1 s con el restante (cuenta regresiva)
  delay(100);
}
