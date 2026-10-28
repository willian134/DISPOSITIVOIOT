/*
  ============================================================================
  Práctica 2 + Trabajo Autónomo 2 – Estación inteligente
  Aplicaciones Tecnológicas Autónomas – ITS Sudamericano
  ----------------------------------------------------------------------------
  El ESP32 cumple DOS papeles al mismo tiempo:
    1) SERVIDOR: expone una API REST en JSON
         GET  /api/estado  -> lecturas, modo, LED y configuración (pública)
         POST /api/servo   -> mueve la compuerta (X-API-Key; 409 en modo automático)
    2) CLIENTE: habla con el backend Next.js
         POST {BACKEND_URL}/api/lecturas -> cada N s; la respuesta trae la configuración
         POST {BACKEND_URL}/api/eventos  -> apertura, cierre y cambio_modo

  Escenario: el servo es la compuerta de una bodega (0° = cerrada, 90° = abierta).
    - Modo automático: persona a menos del umbral -> abre y enciende el LED.
                       5 s seguidos sin nadie     -> cierra y apaga el LED.
    - Modo manual:     la compuerta solo se mueve con órdenes del dashboard.
    - El pulsador alterna entre los dos modos.

  Periféricos: DHT11, LDR (divisor con 10 kΩ), HC-SR04 (ECHO con divisor
  1 kΩ / 2 kΩ), servo SG90 (fuente de 5 V externa), LCD 16x2 I2C,
  LED rojo de alerta (GPIO25 + resistencia) y pulsador (GPIO33, pull-up interno).
  ============================================================================
*/
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>        // Benoit Blanchon, versión 7.x
#include <DHT.h>                // Adafruit DHT sensor library
#include <ESP32Servo.h>         // Kevin Harrington / John K. Bennett
#include <Wire.h>
#include <LiquidCrystal_I2C.h>  // Frank de Brabander
#include "secrets.h"            // WIFI_SSID, WIFI_PASS, API_KEY, BACKEND_URL

// ------------------------------- Pines --------------------------------------
#define PIN_DHT        4
#define PIN_LDR        34   // ADC1: el ADC2 NO funciona mientras el WiFi está activo
#define PIN_TRIG       26
#define PIN_ECHO       27   // llega a través del divisor 1 kΩ / 2 kΩ (5 V -> 3.3 V)
#define PIN_SERVO      13
#define PIN_LED_ENVIO  2    // LED integrado: parpadea en cada envío al backend
#define PIN_ALERTA     25   // LED rojo de alerta, con su resistencia limitadora
#define PIN_BOTON      33   // pulsador entre el pin y GND (pull-up interno)

#define DHTTYPE    DHT11
#define DEVICE_ID  "grupo6"   // también es el nombre mDNS: http://grupo6.local

// ------------------------------ Compuerta -----------------------------------
const int ANGULO_CERRADA = 0;
const int ANGULO_ABIERTA = 90;

// ------------------------------- Tiempos ------------------------------------
const unsigned long INTERVALO_SENSORES   = 2000;  // DHT11 y LDR (el DHT11 admite ~1 lectura/s)
const unsigned long INTERVALO_DISTANCIA  = 200;   // ultrasónico: rápido para detectar personas
const unsigned long INTERVALO_LCD        = 3000;  // alterna las dos páginas del LCD
const unsigned long INTERVALO_WIFI       = 10000; // reintento de conexión si se pierde el WiFi
const unsigned long TIEMPO_SIN_PRESENCIA = 5000;  // 5 s seguidos sin nadie -> cerrar
const unsigned long ANTIRREBOTE_MS       = 50;    // el pulsador debe estar estable 50 ms

// ------------------------------- Objetos ------------------------------------
DHT dht(PIN_DHT, DHTTYPE);
Servo servo;
LiquidCrystal_I2C lcd(0x27, 16, 2);   // dirección encontrada con el escáner I2C
WebServer server(80);

// Últimas lecturas (caché). La API responde con esto, no lee el sensor en cada petición.
struct Lecturas {
  float temperatura = NAN;             // °C
  float humedad     = NAN;             // %
  int   luz         = 0;               // 0-100 %
  float distancia   = -1;              // cm, -1 = sin eco (fuera de rango)
  int   servo       = ANGULO_CERRADA;  // grados
} actual;

// Configuración remota. Estos valores se usan hasta recibir los del backend.
struct Configuracion {
  int umbralCm   = 20;   // 5 - 200 cm
  int intervaloS = 10;   // 5 - 60 s
} config;

bool   modoAuto = false;       // arranca en manual: la compuerta no se mueve sola hasta saber el modo
String modoServidor = "";      // último modo que llegó del backend ("auto" o "manual")
bool   ledAlerta = false;
unsigned long tUltimaPresencia = 0;

unsigned long tSensores = 0, tDistancia = 0, tEnvio = 0, tLcd = 0, tWifi = 0;
bool   pantallaA = true;       // LCD: true = clima + IP, false = luz/distancia + último evento
String ultimoEvento = "Iniciando";

// Pulsador (antirrebote)
int lecturaBotonAnterior = HIGH;   // última lectura cruda del pin
int estadoBotonEstable   = HIGH;   // último estado aceptado como real
unsigned long tCambioBoton = 0;    // cuándo cambió por última vez la lectura cruda

const char* textoModo() { return modoAuto ? "auto" : "manual"; }

// ============================ Lectura de sensores ===========================
float leerDistanciaCm() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);       // pulso de disparo de 10 µs
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  // Timeout de 25 ms (~4 m). Sin timeout, pulseIn bloquea 1 s y el servidor se congela.
  unsigned long us = pulseIn(PIN_ECHO, HIGH, 25000);
  if (us == 0) return -1;             // sin eco
  return us * 0.0343 / 2.0;           // velocidad del sonido 343 m/s, ida y vuelta
}

int leerLuzPorcentaje() {
  long suma = 0;
  for (int i = 0; i < 10; i++) suma += analogRead(PIN_LDR);   // promedio contra el ruido
  int crudo = suma / 10;                                      // 0 - 4095 (12 bits)
  return map(crudo, 0, 4095, 0, 100);
}

void actualizarClimaYLuz() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) actual.temperatura = t;   // si falla, se conserva el último valor válido
  if (!isnan(h)) actual.humedad = h;
  actual.luz = leerLuzPorcentaje();
}

// ============================== JSON ========================================
// Un número no válido (NaN o -1) se envía como null: el backend lo acepta.
void agregarNumero(JsonObject obj, const char* clave, float valor, float invalido = NAN) {
  if (isnan(valor) || valor == invalido) obj[clave] = nullptr;
  else obj[clave] = round(valor * 10) / 10.0;   // 1 decimal
}

void llenarLecturas(JsonObject obj) {
  obj["dispositivo"] = DEVICE_ID;
  agregarNumero(obj, "temperatura", actual.temperatura);
  agregarNumero(obj, "humedad", actual.humedad);
  obj["luz"] = actual.luz;
  agregarNumero(obj, "distancia", actual.distancia, -1);
  obj["servo"] = actual.servo;
}

void llenarConfig(JsonObject obj) {
  obj["umbral_distancia_cm"] = config.umbralCm;
  obj["intervalo_envio_s"]   = config.intervaloS;
  obj["modo"]                = textoModo();
}

void responderJson(int codigo, JsonDocument& doc) {
  String cuerpo;
  serializeJson(doc, cuerpo);
  server.send(codigo, "application/json", cuerpo);
}

void responderError(int codigo, const char* mensaje) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = mensaje;
  responderJson(codigo, doc);
}

// ========================== Actuadores y modo ===============================
void moverCompuerta(int angulo) {
  servo.write(angulo);
  actual.servo = angulo;
}

void ponerAlerta(bool encendido) {
  ledAlerta = encendido;
  digitalWrite(PIN_ALERTA, encendido ? HIGH : LOW);
}

void cambiarModo(bool nuevoAuto) {
  if (nuevoAuto == modoAuto) return;
  modoAuto = nuevoAuto;
  if (modoAuto) tUltimaPresencia = millis();   // la cuenta de 5 s empieza de cero
  else ponerAlerta(false);                     // en manual las reglas no actúan: alerta apagada
  Serial.printf("[MODO] Ahora en modo %s\n", textoModo());
}

// =================== Cliente HTTP: eventos al backend =======================
// modoNuevo solo se envía en cambio_modo, para que el backend actualice su configuración.
void enviarEvento(const char* tipo, const char* origen, const String& detalle,
                  const char* modoNuevo = nullptr) {
  Serial.printf("[EVENTO] %s (%s): %s\n", tipo, origen, detalle.c_str());
  if (WiFi.status() != WL_CONNECTED) return;

  JsonDocument doc;
  doc["dispositivo"] = DEVICE_ID;
  doc["tipo"] = tipo;
  doc["origen"] = origen;
  doc["detalle"] = detalle;
  if (modoNuevo) doc["modo"] = modoNuevo;
  String cuerpo;
  serializeJson(doc, cuerpo);

  HTTPClient http;
  http.begin(String(BACKEND_URL) + "/api/eventos");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", API_KEY);
  http.setConnectTimeout(2000);   // tiempos cortos: las reglas no deben quedarse esperando
  http.setTimeout(2000);
  int codigo = http.POST(cuerpo);
  if (codigo > 0) Serial.printf("[EVENTO] backend respondió %d\n", codigo);
  else Serial.printf("[EVENTO] Error: %s\n", http.errorToString(codigo).c_str());
  http.end();
}

// ============================ Reglas y pulsador =============================
void aplicarReglasAutomaticas() {
  if (!modoAuto) return;
  unsigned long ahora = millis();
  bool hayPersona = actual.distancia >= 0 && actual.distancia < config.umbralCm;

  if (hayPersona) {
    tUltimaPresencia = ahora;
    if (actual.servo != ANGULO_ABIERTA) {
      moverCompuerta(ANGULO_ABIERTA);
      ultimoEvento = "Apertura auto";
      enviarEvento("apertura", "automatico",
                   "Distancia " + String((int)round(actual.distancia)) + " cm < umbral " +
                   String(config.umbralCm) + " cm");
    }
    if (!ledAlerta) ponerAlerta(true);
  } else if (ahora - tUltimaPresencia >= TIEMPO_SIN_PRESENCIA) {
    if (actual.servo != ANGULO_CERRADA) {
      moverCompuerta(ANGULO_CERRADA);
      ultimoEvento = "Cierre auto";
      enviarEvento("cierre", "automatico", "Sin presencia durante 5 s");
    }
    if (ledAlerta) ponerAlerta(false);
  }
}

void revisarPulsador() {
  int lectura = digitalRead(PIN_BOTON);
  if (lectura != lecturaBotonAnterior) {   // cambió (o es un rebote): reinicia el cronómetro
    tCambioBoton = millis();
    lecturaBotonAnterior = lectura;
  }
  // Solo se acepta el cambio si la lectura se mantuvo igual durante 50 ms
  if (millis() - tCambioBoton >= ANTIRREBOTE_MS && lectura != estadoBotonEstable) {
    estadoBotonEstable = lectura;
    if (estadoBotonEstable == LOW) {       // LOW = recién presionado (pull-up)
      String detalle = modoAuto ? "Modo automático → manual" : "Modo manual → automático";
      cambiarModo(!modoAuto);
      ultimoEvento = modoAuto ? "Modo auto" : "Modo manual";
      enviarEvento("cambio_modo", "pulsador", detalle, textoModo());
    }
  }
}

// ===================== Configuración remota del backend =====================
// Llega en la respuesta de POST /api/lecturas. Un valor fuera de rango se ignora.
void aplicarConfig(JsonObject cfg) {
  if (cfg["umbral_distancia_cm"].is<int>()) {
    int umbral = cfg["umbral_distancia_cm"];
    if (umbral >= 5 && umbral <= 200) {
      if (umbral != config.umbralCm) Serial.printf("[CONFIG] Umbral %d -> %d cm\n", config.umbralCm, umbral);
      config.umbralCm = umbral;
    } else {
      Serial.printf("[CONFIG] Umbral %d fuera de rango (5-200): se ignora\n", umbral);
    }
  }
  if (cfg["intervalo_envio_s"].is<int>()) {
    int intervalo = cfg["intervalo_envio_s"];
    if (intervalo >= 5 && intervalo <= 60) {
      if (intervalo != config.intervaloS) Serial.printf("[CONFIG] Intervalo %d -> %d s\n", config.intervaloS, intervalo);
      config.intervaloS = intervalo;
    } else {
      Serial.printf("[CONFIG] Intervalo %d fuera de rango (5-60): se ignora\n", intervalo);
    }
  }
  if (cfg["modo"].is<const char*>()) {
    String modo = cfg["modo"].as<const char*>();
    if (modo != "auto" && modo != "manual") {
      Serial.println("[CONFIG] Modo desconocido: se ignora");
    } else if (modo != modoServidor) {
      // Solo se aplica cuando el modo CAMBIA en el servidor (alguien lo editó en la web).
      // Así, un cambio hecho con el pulsador no se deshace en el siguiente envío.
      modoServidor = modo;
      cambiarModo(modo == "auto");
    }
  }
}

// ===================== Cliente HTTP: envío de lecturas ======================
void enviarLecturas() {
  if (WiFi.status() != WL_CONNECTED) return;

  JsonDocument doc;
  llenarLecturas(doc.to<JsonObject>());
  String cuerpo;
  serializeJson(doc, cuerpo);

  HTTPClient http;
  http.begin(String(BACKEND_URL) + "/api/lecturas");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", API_KEY);
  http.setConnectTimeout(3000);
  http.setTimeout(3000);   // si el backend no responde, no bloquear más de 3 s

  digitalWrite(PIN_LED_ENVIO, HIGH);
  int codigo = http.POST(cuerpo);
  digitalWrite(PIN_LED_ENVIO, LOW);

  if (codigo > 0) {
    Serial.printf("[POST] %d -> %s\n", codigo, cuerpo.c_str());
    ultimoEvento = (codigo == 201) ? String("Envio OK") : "Backend " + String(codigo);
    if (codigo == 201) {
      JsonDocument respuesta;
      if (!deserializeJson(respuesta, http.getString()) && respuesta["config"].is<JsonObject>()) {
        aplicarConfig(respuesta["config"].as<JsonObject>());
      }
    }
  } else {
    Serial.printf("[POST] Error: %s\n", http.errorToString(codigo).c_str());
    ultimoEvento = "Sin backend";
  }
  http.end();
}

// ============================ Rutas del servidor ============================
void rutaRaiz() {
  JsonDocument doc;
  doc["dispositivo"] = DEVICE_ID;
  JsonArray rutas = doc["endpoints"].to<JsonArray>();
  rutas.add("GET  /api/estado");
  rutas.add("POST /api/servo  {\"angulo\": 0-180}  (requiere X-API-Key; solo en modo manual)");
  responderJson(200, doc);
}

void rutaEstado() {
  JsonDocument doc;
  llenarLecturas(doc.to<JsonObject>());
  doc["modo"] = textoModo();
  doc["led"] = ledAlerta;
  llenarConfig(doc["config"].to<JsonObject>());
  doc["ip"] = WiFi.localIP().toString();
  doc["rssi"] = WiFi.RSSI();                 // intensidad de señal WiFi (dBm)
  doc["uptime_s"] = millis() / 1000;
  doc["ultimo_evento"] = ultimoEvento;
  responderJson(200, doc);
}

void rutaServo() {
  // 1) Autenticación: solo quien conoce la clave puede mover el actuador
  if (server.header("X-API-Key") != API_KEY) {
    responderError(401, "API key inválida");
    return;
  }
  // 2) En modo automático la compuerta la manejan las reglas, no la web
  if (modoAuto) {
    responderError(409, "La compuerta está en modo automático: cambie a manual para moverla desde la web");
    return;
  }
  // 3) El cuerpo JSON llega en el argumento especial "plain"
  JsonDocument entrada;
  if (deserializeJson(entrada, server.arg("plain"))) {
    responderError(400, "JSON inválido");
    return;
  }
  // 4) Validación: el hardware solo admite 0-180°
  if (!entrada["angulo"].is<int>()) {
    responderError(400, "Falta el campo 'angulo' (entero)");
    return;
  }
  int angulo = entrada["angulo"];
  if (angulo < 0 || angulo > 180) {
    responderError(422, "El ángulo debe estar entre 0 y 180");
    return;
  }
  // 5) Acción física
  moverCompuerta(angulo);
  ultimoEvento = "Servo " + String(angulo) + " grados";
  Serial.println("[API] " + ultimoEvento);

  JsonDocument doc;
  doc["ok"] = true;
  doc["angulo"] = angulo;
  responderJson(200, doc);
}

void rutaNoEncontrada() {
  responderError(404, "Ruta no encontrada");
}

// ================================ LCD =======================================
// Página A: clima + IP.   Página B: luz/distancia + último evento.
// En la esquina superior derecha: A = automático, M = manual.
void actualizarLcd() {
  lcd.clear();

  lcd.setCursor(0, 0);
  if (pantallaA) {
    if (isnan(actual.temperatura)) lcd.print("T:--  H:--");
    else lcd.printf("T:%.1fC H:%.0f%%", actual.temperatura, actual.humedad);
  } else {
    if (actual.distancia < 0) lcd.printf("L:%d%% D:--", actual.luz);
    else lcd.printf("L:%d%% D:%.0fcm", actual.luz, actual.distancia);
  }
  lcd.setCursor(15, 0);
  lcd.print(modoAuto ? 'A' : 'M');

  lcd.setCursor(0, 1);
  if (pantallaA) lcd.print(WiFi.localIP());
  else lcd.print(ultimoEvento.substring(0, 16));

  pantallaA = !pantallaA;
}

// ================================ WiFi ======================================
void conectarWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Conectando a WiFi");
  lcd.setCursor(0, 0);
  lcd.print("Conectando WiFi");
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("No se pudo conectar. Se reintentará en el loop.");
  }
}

// ================================ setup =====================================
void setup() {
  Serial.begin(115200);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_LED_ENVIO, OUTPUT);
  pinMode(PIN_ALERTA, OUTPUT);
  digitalWrite(PIN_ALERTA, LOW);
  pinMode(PIN_BOTON, INPUT_PULLUP);   // HIGH en reposo, LOW al presionar
  analogReadResolution(12);           // 0 - 4095
  analogSetAttenuation(ADC_11db);     // rango aproximado 0 - 3.1 V

  dht.begin();
  servo.setPeriodHertz(50);           // servo estándar: 50 Hz (periodo 20 ms)
  servo.attach(PIN_SERVO, 500, 2400); // ancho de pulso 0.5 ms (0°) a 2.4 ms (180°)
  moverCompuerta(ANGULO_CERRADA);     // la compuerta arranca cerrada

  Wire.begin(21, 22);                 // SDA, SCL
  lcd.init();
  lcd.backlight();

  conectarWiFi();

  if (MDNS.begin(DEVICE_ID)) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS: http://" DEVICE_ID ".local");
  }

  // Encabezados que el servidor debe conservar para leerlos en las rutas
  const char* encabezados[] = {"X-API-Key"};
  server.collectHeaders(encabezados, 1);

  server.on("/", HTTP_GET, rutaRaiz);
  server.on("/api/estado", HTTP_GET, rutaEstado);
  server.on("/api/servo", HTTP_POST, rutaServo);
  server.onNotFound(rutaNoEncontrada);
  server.begin();
  Serial.println("Servidor HTTP iniciado en el puerto 80");
}

// ================================= loop =====================================
// Sin delay(): cada tarea se ejecuta cuando le toca, y el servidor siempre atiende.
void loop() {
  server.handleClient();
  revisarPulsador();
  unsigned long ahora = millis();

  if (ahora - tDistancia >= INTERVALO_DISTANCIA) {
    tDistancia = ahora;
    actual.distancia = leerDistanciaCm();
    aplicarReglasAutomaticas();
  }
  if (ahora - tSensores >= INTERVALO_SENSORES) {
    tSensores = ahora;
    actualizarClimaYLuz();
  }
  if (ahora - tEnvio >= (unsigned long)config.intervaloS * 1000UL) {
    tEnvio = ahora;
    enviarLecturas();
  }
  if (ahora - tLcd >= INTERVALO_LCD) {
    tLcd = ahora;
    actualizarLcd();
  }
  // Reconexión automática si se pierde el WiFi
  if (WiFi.status() != WL_CONNECTED && ahora - tWifi >= INTERVALO_WIFI) {
    tWifi = ahora;
    Serial.println("WiFi perdido, reconectando...");
    WiFi.reconnect();
  }
}