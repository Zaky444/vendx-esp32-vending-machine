# VendX ESP32 Vending Machine

Firmware ESP32 untuk prototype vending machine VendX. ESP32 berkomunikasi hanya dengan Backend API, sedangkan Backend API yang mengelola Firebase Realtime Database, Dashboard, Midtrans, transaksi, timeout, dan state machine.

## Arsitektur Final

```text
ESP32 -> Backend API -> Firebase Realtime Database
Dashboard -> Backend API -> Firebase Realtime Database
Midtrans -> Backend Webhook -> Firebase Realtime Database
```

Backend API:

```text
https://api.vendx.site
```

ESP32 tidak lagi memakai Firebase SDK, tidak membaca Firebase langsung, dan tidak menulis Firebase langsung.

## Tanggung Jawab ESP32

- Menampilkan menu produk di Serial dan TFT ILI9341.
- Menerima input produk dari Serial.
- Meminta backend membuat transaksi.
- Menampilkan QRIS/Snap fallback di layar TFT.
- Polling command dari backend.
- Menjalankan servo saat backend memberi command `DISPENSE`.
- Membaca simulasi sensor barang lewat input Serial `y` / `n`.
- Mengirim `dispense_result` ke backend.
- Mengirim event mesin ke backend.

## Tanggung Jawab Backend

- Validasi item, stok, harga, dan status aktif.
- Membuat transaksi Midtrans Sandbox/QRIS.
- Menangani webhook Midtrans.
- Menentukan payment timeout.
- Menulis state ke Firebase.
- Menyediakan command untuk ESP32.
- Memproses `dispense_result`.
- Menyediakan data realtime untuk Dashboard.

## Konfigurasi Utama

Di [src/main.cpp](src/main.cpp):

```cpp
#define BACKEND_BASE_URL "https://api.vendx.site"
#define PAYMENT_METHOD "qris"
#define MACHINE_ID "VM001"
```

Pin hardware:

```cpp
#define TFT_CS   5
#define TFT_DC   2
#define TFT_RST  4
#define SERVO_PIN   13
#define BUZZER_PIN  27
```

WiFi Wokwi:

```cpp
#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""
```

## Menu Produk

Input melalui Serial Monitor:

```text
1 = cola
2 = sprite
3 = susu
4 = kopi
```

## Flow Pembelian

1. User memilih produk dari Serial Monitor.
2. ESP32 mengirim event `ITEM_SELECTED`.
3. ESP32 memanggil `POST /api/transactions`.
4. Backend memvalidasi item/stok/harga/status aktif.
5. Jika gagal, ESP32 menampilkan error dan kembali ke menu.
6. Jika sukses, ESP32 menampilkan QRIS di TFT.
7. ESP32 mengirim event `QR_DISPLAYED`.
8. ESP32 polling `GET /api/machines/VM001/command`.
9. Jika backend mengirim command `DISPENSE`, ESP32 menjalankan servo.
10. ESP32 membaca sensor simulasi.
11. ESP32 mengirim `SUCCESS` atau `FAILED` ke backend.
12. ESP32 mengirim event `IDLE` dan kembali ke menu.

## Endpoint Yang Dipakai ESP32

```text
POST /api/transactions
GET  /api/machines/VM001/command
POST /api/machines/VM001/events
POST /api/transactions/{transactionId}/dispense-result
```

ESP32 tidak memakai endpoint Firebase langsung.

## Event Mesin

Event yang dikirim ESP32:

```text
ONLINE
IDLE
ITEM_SELECTED
QR_DISPLAYED
DISPENSE_STARTED
PAYMENT_TIMEOUT_ACK
ERROR
```

## Command Backend

Command yang dibaca ESP32:

```text
IDLE
WAIT_PAYMENT
DISPENSE
PAYMENT_TIMEOUT
COMPLETED
DISPENSE_FAILED
NEEDS_REVIEW
ERROR
```

Hanya command `DISPENSE` yang membuat ESP32 menjalankan servo.

Jika command `PAYMENT_TIMEOUT` diterima, ESP32 hanya menampilkan timeout, mengirim `PAYMENT_TIMEOUT_ACK`, dan kembali ke menu. ESP32 tidak mengirim `dispense_result = FAILED` untuk payment timeout.

## Library

Dependency PlatformIO ada di [platformio.ini](platformio.ini):

```ini
lib_deps =
    bblanchon/ArduinoJson@^6.21.5
    madhephaestus/ESP32Servo@^3.0.7
    adafruit/Adafruit GFX Library@^1.12.1
    adafruit/Adafruit ILI9341@^1.6.2
    adafruit/Adafruit BusIO@^1.17.0
    ricmoo/QRCode@^0.0.1
```

Tidak ada dependency Firebase di firmware ESP32.

## Build

```bash
platformio run
```

Upload ke board:

```bash
platformio run --target upload
```

Monitor Serial:

```bash
platformio device monitor
```

## Testing Demo

### Transaksi sukses

1. Pilih produk aktif dengan stok cukup.
2. QRIS muncul di TFT.
3. Bayar melalui Midtrans Sandbox.
4. Backend mengirim command `DISPENSE`.
5. Servo berjalan.
6. Input `y` untuk barang keluar.
7. ESP32 mengirim `dispense_result = SUCCESS`.

### Barang gagal keluar

1. Ikuti transaksi sukses sampai servo berjalan.
2. Input `n`.
3. ESP32 mengirim `dispense_result = FAILED`.

### Stok habis

1. Set stok produk ke `0` dari backend/dashboard.
2. Pilih produk tersebut.
3. Backend menolak dengan `OUT_OF_STOCK`.
4. ESP32 menampilkan `OUT OF STOCK` dan kembali ke menu.

### Payment timeout

1. Buat transaksi dan jangan bayar.
2. Backend menentukan timeout.
3. ESP32 menerima command `PAYMENT_TIMEOUT`.
4. ESP32 mengirim `PAYMENT_TIMEOUT_ACK`.
5. Servo tidak berjalan dan tidak ada `dispense_result = FAILED`.
