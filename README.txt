KWH METER - ESP32 + PZEM-004T + FIREBASE REALTIME DATABASE

This repository contains the ESP32 firmware and LittleFS dashboard.
The firmware broadcasts live readings to the local dashboard over WebSocket
and uploads readings to Firebase Realtime Database over HTTPS/REST.

FIREBASE CONFIGURATION
- Project: wokwi-f4741
- Database: https://wokwi-f4741-default-rtdb.asia-southeast1.firebasedatabase.app
- Device ID: kwh_meter_esp32_001

DATABASE STRUCTURE
/devices/kwh_meter_esp32_001/latest
  voltage       number, volts
  current       number, amperes
  powerFactor   number
  activePower   number, watts
  energy        number, kWh
  frequency     number, Hz
  timestamp     Firebase server timestamp (milliseconds)

/devices/kwh_meter_esp32_001/history/{push-id}
  Same fields as latest

The latest snapshot is updated every 5 seconds. A history record is appended
every 60 seconds to limit database writes and growth.

FIREBASE RULES / AUTHENTICATION
The firmware uses Firebase Realtime Database REST API. The Firebase Web API
key is not an authentication credential for these REST requests.

For a temporary isolated test only, Realtime Database Rules can allow read
and write access to /devices/kwh_meter_esp32_001. Do not leave public write
rules enabled for a production database.

If the database requires authentication, set FIREBASE_AUTH_TOKEN in
kwh_meter.ino to a valid Firebase Authentication ID token. ID tokens expire;
a production implementation should obtain/refresh tokens securely rather
than hard-code an expiring token. Never embed a service-account private key
in ESP32 firmware.

WOKWI / NO-HARDWARE TEST
- PZEM_SIMULATION is set to 1 by default. This generates plausible sample
  voltage/current/power/frequency readings and integrates simulated energy.
- Wi-Fi is configured as Wokwi-GUEST with no password.
- Run the sketch in Wokwi with internet access enabled. The ESP32 sends the
  generated readings to Firebase even without a physical PZEM module.
- Set PZEM_SIMULATION to 0 when using the real PZEM-004T sensor.

PHYSICAL ESP32 SETUP
- Change ssid/password to the local Wi-Fi credentials.
- Set PZEM_SIMULATION to 0.
- PZEM TX -> ESP32 GPIO16 (RX2)
- PZEM RX -> ESP32 GPIO17 (TX2)
- PZEM GND -> ESP32 GND
- UART: 9600 baud.
- Install PZEM004Tv30 by Jakub Mandula.
- Install WebSockets by Markus Sattler.
- Install ArduinoJson 7.

SECURITY NOTE
client.setInsecure() is used only to make HTTPS testing convenient in Wokwi.
It disables TLS certificate verification. Replace it with CA certificate
validation before deploying this firmware to a real installation.

SAFETY
PZEM measures hazardous AC voltage. Mains wiring, CT placement, and
insulation must be handled by a qualified person with the supply isolated.
Never connect mains voltage to ESP32 GPIO pins.

LOCAL DASHBOARD
The existing LittleFS dashboard and WebSocket stream remain available:
- HTTP server: port 80
- WebSocket server: port 81
- Upload all files in kwh_meter/data/ to LittleFS.
