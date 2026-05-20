# SPEDI RC Controller

SPEDI RC Controller adalah aplikasi Flutter untuk mengontrol kapal/vehicle SPEDI secara real-time. Aplikasi menyediakan mode kontrol manual berbasis joystick, mode grid berbasis waypoint, tampilan telemetri, pemantauan GPS, serta komunikasi ke backend, WebSocket, dan MQTT.

## Fitur

- Kontrol manual throttle dan steering dengan joystick.
- Emergency stop untuk menghentikan motor.
- Monitoring telemetri kapal: GPS, speed, heading, satellite, HDOP, GSM, sinyal, IMU/fusion mode, dan obstacle sensor.
- Mode grid untuk membuat rute waypoint langsung dari peta.
- Pengiriman route ke device melalui MQTT dan pencatatan route ke backend.
- WebSocket untuk command kontrol melalui backend.
- MQTT untuk komunikasi langsung dengan device/Arduino.
- Auto reconnect untuk WebSocket dan MQTT.

## Teknologi

- Flutter
- Dart
- HTTP REST API
- WebSocket
- MQTT
- flutter_map
- latlong2
- shared_preferences

## Struktur Project

```text
lib/
  main.dart                  Entry point aplikasi
  controller_page.dart       Mode manual joystick dan telemetri
  grid_control_page.dart     Mode grid/waypoint berbasis peta
  core/
    api_client.dart          HTTP client dan konfigurasi API
    api_exception.dart       Model error API
  services/
    auth_service.dart        Login/register
    session_service.dart     Session device aktif
    websocket_service.dart   Koneksi WebSocket kontrol
    mqtt_device_service.dart Koneksi MQTT dan telemetri device
    route_service.dart       API route/waypoint

spedi_boat_v15_complete/
  spedi_boat_v15_FULL.ino    Firmware Arduino/ESP32 pendukung
  *.md                       Dokumentasi firmware dan instruksi build
```

## Prasyarat

- Flutter SDK versi kompatibel dengan Dart SDK `^3.9.2`.
- Android Studio atau emulator/device Android untuk menjalankan aplikasi mobile.
- Koneksi internet untuk akses backend, WebSocket, broker MQTT, dan tile map.

## Instalasi

1. Clone repository:

   ```bash
   git clone https://github.com/Ramadanil11/Aplikasi-Kontrol-Spedi.git
   cd Aplikasi-Kontrol-Spedi
   ```

2. Ambil dependency:

   ```bash
   flutter pub get
   ```

3. Jalankan aplikasi:

   ```bash
   flutter run
   ```

## Build Android

```bash
flutter build apk
```

Hasil build APK akan tersedia di:

```text
build/app/outputs/flutter-apk/app-release.apk
```

## Cara Pakai Singkat

1. Buka aplikasi dalam mode landscape.
2. Pastikan device SPEDI, backend, WebSocket, dan MQTT dapat dijangkau.
3. Gunakan mode `MANUAL` untuk throttle dan steering langsung.
4. Gunakan tombol emergency stop untuk menghentikan motor.
5. Masuk ke mode `GRID`, tap peta untuk menambahkan waypoint.
6. Tekan `EXECUTE` untuk mengirim route ke kapal.
7. Tekan `STOP`, `UNDO`, atau `RESET` sesuai kebutuhan.

## Catatan Konfigurasi

Konfigurasi backend, device id, dan koneksi MQTT saat ini berada di kode aplikasi. Untuk penggunaan produksi, sebaiknya pindahkan nilai sensitif ke environment variable, konfigurasi build, atau secret manager agar tidak tersimpan langsung di repository.

## Firmware Device

Folder `spedi_boat_v15_complete/` berisi firmware dan dokumentasi pendukung untuk device kapal. Mulai dari:

- `START_HERE.md`
- `QUICK_START.md`
- `BUILD_INSTRUCTIONS.md`
- `spedi_boat_v15_FULL.ino`

## Pengujian

Jalankan test Flutter:

```bash
flutter test
```

Jalankan analisis kode:

```bash
flutter analyze
```
