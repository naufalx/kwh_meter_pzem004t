#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <PZEM004Tv30.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <math.h>

// =====================================================
// CONFIGURATION
// =====================================================

// Wokwi internet access. For a physical ESP32, replace
// these with the Wi-Fi credentials for the installation.
const char *ssid = "Wokwi-GUEST";
const char *password = "";

// Firebase Realtime Database (asia-southeast1).
// Do not add a trailing slash.
static const char *FIREBASE_HOST =
    "https://wokwi-f4741-default-rtdb.asia-southeast1.firebasedatabase.app";

// Stable ID for this meter. Change if you deploy more than one device.
static const char *DEVICE_ID = "kwh_meter_esp32_001";

// Leave empty only when Realtime Database Rules permit the request.
// If using a Firebase ID token, provide a current token here.
// Never put a service-account private key in ESP32 firmware.
static const char *FIREBASE_AUTH_TOKEN = "";

// 1 = generate sample readings for Wokwi/no PZEM hardware.
// 0 = read the real PZEM-004T sensor.
#define PZEM_SIMULATION 1

#define PZEM_RX_PIN 16
#define PZEM_TX_PIN 17

// Local dashboard / WebSocket
WebServer server(80);
WebSocketsServer webSocket(81);

// PZEM UART2
PZEM004Tv30 pzem(Serial2, PZEM_RX_PIN, PZEM_TX_PIN);

// Meter readings
float voltage = 0.0f;
float current = 0.0f;
float powerFactor = 0.0f;
float activePower = 0.0f;
float energy = 0.0f;
float frequency = 0.0f;

// Timing
unsigned long lastSend = 0;
unsigned long lastFirebaseLatest = 0;
unsigned long lastFirebaseHistory = 0;
unsigned long lastSimulationUpdate = 0;

const unsigned long SEND_INTERVAL = 1000;              // WebSocket
const unsigned long FIREBASE_LATEST_INTERVAL = 5000;   // latest snapshot
const unsigned long FIREBASE_HISTORY_INTERVAL = 60000; // one history row/minute

// =====================================================
// FIREBASE REST HELPERS
// =====================================================

String firebaseUrl(const char *path) {
    String url = String(FIREBASE_HOST) + path;

    if (FIREBASE_AUTH_TOKEN[0] != '\0') {
        url += "?auth=";
        url += FIREBASE_AUTH_TOKEN;
    }

    return url;
}

bool firebaseRequest(const char *method, const char *path, const String &payload) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Firebase] Skip: Wi-Fi disconnected");
        return false;
    }

    WiFiClientSecure client;

    // Development-only convenience for Wokwi.
    // For production, validate the server certificate with the
    // appropriate CA certificate instead of setInsecure().
    client.setInsecure();

    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(5000);

    const String url = firebaseUrl(path);

    if (!http.begin(client, url)) {
        Serial.println("[Firebase] Could not start HTTPS request");
        return false;
    }

    http.addHeader("Content-Type", "application/json");

    int httpCode = -1;

    if (strcmp(method, "PATCH") == 0) {
        httpCode = http.sendRequest("PATCH", payload);
    } else if (strcmp(method, "POST") == 0) {
        httpCode = http.POST(payload);
    } else {
        Serial.println("[Firebase] Unsupported HTTP method");
        http.end();
        return false;
    }

    const bool success = httpCode >= 200 && httpCode < 300;

    if (success) {
        Serial.printf("[Firebase] %s %s -> HTTP %d\n",
                      method, path, httpCode);
    } else {
        Serial.printf("[Firebase] %s %s failed -> HTTP %d\n",
                      method, path, httpCode);

        if (httpCode > 0) {
            Serial.println(http.getString());
        } else {
            Serial.println(http.errorToString(httpCode));
        }
    }

    http.end();
    return success;
}

String buildReadingJson() {
    JsonDocument doc;

    doc["voltage"] = voltage;
    doc["current"] = current;
    doc["powerFactor"] = powerFactor;
    doc["activePower"] = activePower;
    doc["energy"] = energy;
    doc["frequency"] = frequency;

    // Firebase server timestamp (milliseconds since Unix epoch).
    doc["timestamp"][".sv"] = "timestamp";

    String payload;
    serializeJson(doc, payload);
    return payload;
}

void sendFirebaseLatest() {
    const String path = String("/devices/") + DEVICE_ID + "/latest.json";
    firebaseRequest("PATCH", path.c_str(), buildReadingJson());
}

void sendFirebaseHistory() {
    const String path = String("/devices/") + DEVICE_ID + "/history.json";

    // POST makes Firebase generate a unique child key for each record.
    firebaseRequest("POST", path.c_str(), buildReadingJson());
}

// =====================================================
// SENSOR
// =====================================================

#if PZEM_SIMULATION
void readSimulatedPzem() {
    const unsigned long now = millis();

    if (lastSimulationUpdate == 0) {
        lastSimulationUpdate = now;
    }

    const float elapsedHours =
        (now - lastSimulationUpdate) / 3600000.0f;
    lastSimulationUpdate = now;

    const float seconds = now / 1000.0f;

    voltage = 220.0f + 2.5f * sinf(seconds / 7.0f);
    current = 1.20f + 0.15f * sinf(seconds / 5.0f);
    powerFactor = 0.92f;
    activePower = voltage * current * powerFactor;
    frequency = 50.0f;

    // Integrate simulated power into cumulative kWh.
    energy += (activePower * elapsedHours) / 1000.0f;
}
#endif

bool readPzem() {
#if PZEM_SIMULATION
    readSimulatedPzem();
    return true;
#else
    const float v = pzem.voltage();
    const float i = pzem.current();
    const float p = pzem.power();
    const float e = pzem.energy();
    const float pf = pzem.pf();
    const float f = pzem.frequency();

    // Do not publish invalid/NaN sensor readings.
    if (isnan(v) || isnan(i) || isnan(p) ||
        isnan(e) || isnan(pf) || isnan(f)) {
        Serial.println("[PZEM] Invalid reading. Check sensor wiring/power.");
        return false;
    }

    voltage = v;
    current = i;
    activePower = p;
    energy = e; // PZEM cumulative energy, kWh
    powerFactor = pf;
    frequency = f;
    return true;
#endif
}

// =====================================================
// WEBSOCKET
// =====================================================

void webSocketEvent(
    uint8_t clientNum,
    WStype_t type,
    uint8_t *payload,
    size_t length
) {
    switch (type) {
    case WStype_CONNECTED:
        Serial.printf("[WebSocket] Client connected: %u\n", clientNum);
        break;

    case WStype_DISCONNECTED:
        Serial.printf("[WebSocket] Client disconnected: %u\n", clientNum);
        break;

    case WStype_TEXT:
        Serial.printf("[WebSocket] Browser message: %.*s\n",
                      (int)length, (const char *)payload);
        break;

    default:
        break;
    }
}

// =====================================================
// SENSOR DATA PIPELINE
// =====================================================

void sendData() {
    if (!readPzem()) {
        return;
    }

    JsonDocument doc;
    doc["voltage"] = voltage;
    doc["current"] = current;
    doc["powerFactor"] = powerFactor;
    doc["activePower"] = activePower;
    doc["energy"] = energy;
    doc["frequency"] = frequency;

    String json;
    serializeJson(doc, json);

    // Preserve the existing local dashboard data stream.
    webSocket.broadcastTXT(json);
    Serial.println(json);

    const unsigned long now = millis();

    if (now - lastFirebaseLatest >= FIREBASE_LATEST_INTERVAL ||
        lastFirebaseLatest == 0) {
        lastFirebaseLatest = now;
        sendFirebaseLatest();
    }

    if (now - lastFirebaseHistory >= FIREBASE_HISTORY_INTERVAL ||
        lastFirebaseHistory == 0) {
        lastFirebaseHistory = now;
        sendFirebaseHistory();
    }
}

// =====================================================
// WEB SERVER
// =====================================================

void setupWebServer() {
    server.on("/", HTTP_GET, []() {
        File file = LittleFS.open("/index.html", "r");

        if (!file) {
            server.send(404, "text/plain", "index.html not found");
            return;
        }

        server.streamFile(file, "text/html");
        file.close();
    });

    server.serveStatic("/", LittleFS, "/");
    server.begin();

    webSocket.begin();
    webSocket.onEvent(webSocketEvent);

    Serial.println("[HTTP] Web server started on port 80");
    Serial.println("[WebSocket] Server started on port 81");
}

// =====================================================
// SETUP
// =====================================================

void setup() {
    Serial.begin(115200);
    delay(500);

#if PZEM_SIMULATION
    Serial.println("\n[KWH Meter] PZEM SIMULATION MODE enabled");
    Serial.println("[KWH Meter] Set PZEM_SIMULATION to 0 for real sensor");
#else
    Serial2.begin(9600, SERIAL_8N1, PZEM_RX_PIN, PZEM_TX_PIN);
    Serial.println("[PZEM] UART2 started at 9600 baud");
#endif

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, password);

    Serial.printf("[WiFi] Connecting to %s", ssid);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\n[WiFi] Connected");
    Serial.print("[WiFi] ESP32 IP: ");
    Serial.println(WiFi.localIP());

    if (!LittleFS.begin(true)) {
        Serial.println("[LittleFS] Mount failed");
    } else {
        Serial.println("[LittleFS] Mounted");
    }

    setupWebServer();

    Serial.println("[Firebase] Target: " + String(FIREBASE_HOST));
    Serial.println("[Firebase] Device ID: " + String(DEVICE_ID));
    Serial.println("[KWH Meter] Waiting for meter readings...");
}

// =====================================================
// LOOP
// =====================================================

void loop() {
    server.handleClient();
    webSocket.loop();

    if (WiFi.status() != WL_CONNECTED) {
        static unsigned long lastReconnectAttempt = 0;

        if (millis() - lastReconnectAttempt >= 5000) {
            lastReconnectAttempt = millis();
            Serial.println("[WiFi] Reconnecting...");
            WiFi.reconnect();
        }
    }

    if (millis() - lastSend >= SEND_INTERVAL) {
        lastSend = millis();
        sendData();
    }

    delay(2);
}
