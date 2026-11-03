KWH METER - PROGRAM GABUNGAN

Paket ini mempertahankan website asli dari kwh_meter.zip (Dashboard, Real-time Data,
Riwayat Data, Energy, Pengaturan, Tentang), lalu menambahkan penyimpanan riwayat
di browser (IndexedDB) dan tombol Unduh CSV pada halaman Riwayat Data.

FILE:
- kwh_meter.ino
- data/index.html
- data/real-time.html
- data/riwayat.html
- data/energy.html
- data/pengaturan.html
- data/tentang.html
- data/style.css
- data/script.js

CARA PASANG:
1. Ekstrak ZIP.
2. Buka kwh_meter.ino dengan Arduino IDE.
3. Program menggunakan SSID/password dari file program yang dikirim. Ubah bila perlu.
4. Upload sketch ke ESP32.
5. Upload seluruh isi folder data/ ke LittleFS (bukan folder .git).
6. Buka Serial Monitor 115200 baud, catat IP ESP32.
7. Buka http://IP-ESP32/ di perangkat yang tersambung ke Wi-Fi yang sama.
8. Buka menu Riwayat Data, lalu tekan Unduh CSV.

CATATAN:
- Program .ino sudah menggunakan PZEM-004T melalui UART2 ESP32. Data simulasi/random sudah dihapus.
  Library tambahan: PZEM004Tv30 (PZEM004Tv30 by Jakub Mandula), instal melalui Library Manager Arduino IDE.

WIRING PZEM-004T V3.0 TTL KE ESP32:
- PZEM TX -> ESP32 GPIO16 (RX2)
- PZEM RX -> ESP32 GPIO17 (TX2)
- GND PZEM -> GND ESP32
- VCC/modul PZEM: ikuti spesifikasi modul yang digunakan; jangan menganggap terminal pengukuran AC sebagai catu daya ESP32.

CATATAN PEMBACAAN:
- UART PZEM diset 9600 baud.
- Energi dibaca dari register PZEM dalam kWh, bukan dihitung ulang oleh ESP32.
- Jika pembacaan sensor gagal (NaN), ESP32 tidak mengirim data palsu ke dashboard.
- Energi PZEM merupakan nilai kumulatif pada modul. Reset energi dilakukan melalui prosedur/library PZEM, bukan dengan mengubah nilai di website.
- Website tetap memakai key JSON voltage, current, powerFactor, activePower, dan energy sehingga script.js tidak perlu diubah.

KESELAMATAN:
- PZEM mengukur tegangan AC berbahaya. Pemasangan sisi AC, CT, dan isolasi harus dilakukan dengan pengawasan teknisi/orang dewasa yang kompeten, dalam kondisi sumber listrik terputus.
- Pastikan jenis PZEM (TTL/CT dan versi modul) sesuai. Jangan sambungkan tegangan AC ke pin ESP32.
- Riwayat disimpan di browser saat browser menerima data WebSocket. Data tidak direkam
  ketika browser tertutup atau terputus dari ESP32.
- IndexedDB tersimpan pada browser/perangkat yang sama, bukan otomatis di ESP32.
- Library yang diperlukan: WebSockets by Markus Sattler dan ArduinoJson versi yang
  mendukung JsonDocument (ArduinoJson 7).
- File HTML/CSS/JS di-upload ke LittleFS; jangan meng-upload mklittlefs.bin.
