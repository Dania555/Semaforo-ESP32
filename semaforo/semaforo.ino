/*
 * Semaforo ESP32 por USB — secuencia de semaforo + aviso por cable serie.
 *
 *  Pines y duraciones (pedidas):
 *    ROJO     -> GPIO 2, 20 segundos
 *    AMARILLO -> GPIO 4,  5 segundos
 *    VERDE    -> GPIO 5, 15 segundos
 *
 *  Secuencia: VERDE (15 s) -> AMARILLO (5 s) -> ROJO (20 s) -> repetir.
 *
 *  Como avisa a la pagina:
 *    La ESP32 esta conectada por CABLE USB a este equipo y escribe por
 *    Serial (115200) una linea cada 1 s y en cada cambio de luz:
 *        STATE:red:17
 *    El servidor (server.rb / server.py) lee ese puerto USB y actualiza
 *    la pagina web. No se necesita WiFi ni configurar IP.
 */

#define PIN_ROJO     2
#define PIN_AMARILLO 4
#define PIN_VERDE    5

#define SEG_ROJO     20
#define SEG_AMARILLO 5
#define SEG_VERDE    15

#define BAUDIOS      115200

enum Luz { VERDE, AMARILLO, ROJO };

Luz luzActual = VERDE;
unsigned long inicioLuz = 0;      // millis() cuando cambio la luz
unsigned long ultimoEnvio = 0;    // throttle de lineas STATE (1 por segundo)

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

// Emite el estado por USB: linea "STATE:<luz>:<restante>" que lee el servidor.
void enviarEstado(bool forzar) {
  unsigned long ahora = millis();
  if (!forzar && ahora - ultimoEnvio < 1000) return;  // 1 linea por segundo max.
  ultimoEnvio = ahora;

  Serial.print("STATE:");
  Serial.print(nombreDe(luzActual));
  Serial.print(":");
  Serial.println((int)restanteSeg());
}

void setup() {
  Serial.begin(BAUDIOS);
  pinMode(PIN_ROJO, OUTPUT);
  pinMode(PIN_AMARILLO, OUTPUT);
  pinMode(PIN_VERDE, OUTPUT);

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
