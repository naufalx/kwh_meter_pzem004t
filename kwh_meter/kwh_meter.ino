#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <PZEM004Tv30.h>

const char *ssid = "Polinema Hotspot";
const char *password = "";


#define PZEM_RX_PIN 16
#define PZEM_TX_PIN 17

PZEM004Tv30 pzem(Serial2, PZEM_RX_PIN, PZEM_TX_PIN);

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);


float voltage = 0.0;
float current = 0.0;
float powerFactor = 0.0;
float activePower = 0.0;
float energy = 0.0;
float frequency = 0.0;

// Interval pembacaan/pengiriman data
unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL =1000;

void webSocketEvent(
    uint8_t clientNum,
    WStype_t type,
    uint8_t *payload,
    size_t length)
{
    switch (type)
    {
    case WStype_CONNECTED:
        Serial.printf("Client terhubung: %u\n", clientNum);
        break;

    case WStype_DISCONNECTED:
        Serial.printf("Client terputus: %u\n", clientNum);
        break;

    case WStype_TEXT:
        Serial.printf("Pesan dari browser: %s\n", (char *)payload);
        break;

    default:
        break;
    }
}

void sendData()
{
    // Baca nilai aktual dari PZEM-004T
    float v  = pzem.voltage();
    float i  = pzem.current();
    float p  = pzem.power();
    float e  = pzem.energy();
    float pf = pzem.pf();
    float f  = pzem.frequency();

    // PZEM mengembalikan NaN jika pembacaan gagal/
    // // sensor belum terhubung/tidak mendapat data.
    // if (isnan(v) || isnan(i) || isnan(p) ||
    //     isnan(e) || isnan(pf) || isnan(f))
    // {
    //     Serial.println("Gagal membaca PZEM-004T. Periksa wiring, suplai, dan koneksi sensor.");
    //     return; // Jangan kirim data simulasi atau data tidak valid
    // }

    voltage = v;
    current = i;
    activePower = p;
    energy = e;          // Energi kumulatif dari register PZEM dalam kWh
    powerFactor = pf;
    frequency = f;

    // Buat JSON dengan nama key yang kompatibel
    // dengan script.js website yang sudah ada.
    JsonDocument doc;
    doc["voltage"] = voltage;
    doc["current"] = current;
    doc["powerFactor"] = powerFactor;
    doc["activePower"] = activePower;
    doc["energy"] = energy;
    doc["frequency"] = frequency;

    String json;
    serializeJson(doc, json);

    webSocket.broadcastTXT(json);
    Serial.println(json);
}

// ======================================
// SETUP
// ======================================
void setup()
{
    Serial.begin(115200);
    delay(500);

    // UART2 untuk PZEM-004T
    Serial2.begin(9600, SERIAL_8N1, PZEM_RX_PIN, PZEM_TX_PIN);
    Serial.println("UART PZEM-004T dimulai pada 9600 baud");

    // WiFi
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    Serial.print("Menghubungkan WiFi");
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    Serial.println("WiFi terhubung!");
    Serial.print("IP ESP32: ");
    Serial.println(WiFi.localIP());

    // LittleFS
    if (!LittleFS.begin(true))
    {
        Serial.println("LittleFS gagal!");
        return;
    }
    Serial.println("LittleFS berhasil");

    // Halaman utama
    server.on("/", HTTP_GET, []()
    {
        File file = LittleFS.open("/index.html", "r");
        if (!file)
        {
            server.send(404, "text/plain", "index.html tidak ditemukan");
            return;
        }

        server.streamFile(file, "text/html");
        file.close();
    });

    // CSS, JavaScript, dan halaman lain
    server.serveStatic("/", LittleFS, "/");
    server.begin();

    // WebSocket
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);

    Serial.println("Web Server aktif pada port 80");
    Serial.println("WebSocket aktif pada port 81");
    Serial.println("Menunggu pembacaan PZEM-004T...");
}

// ======================================
// LOOP
// ======================================
void loop()
{
    server.handleClient();
    webSocket.loop();

    if (millis() - lastSend >= SEND_INTERVAL)
    {
        lastSend = millis();
        sendData();
    }
}
