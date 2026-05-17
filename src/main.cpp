#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include <ESP32Servo.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <qrcode.h>

SET_LOOP_TASK_STACK_SIZE(16384);

// WIFI CONFIG
#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""

// BACKEND CONFIG
#define BACKEND_BASE_URL "https://api.vendx.site"
#define PAYMENT_METHOD "qris"

// MACHINE CONFIG
#define MACHINE_ID "VM001"

// TFT PIN
#define TFT_CS   5
#define TFT_DC   2
#define TFT_RST  4

// HARDWARE PIN
#define SERVO_PIN   13
#define BUZZER_PIN  27

// HARDWARE OBJECT
Servo myServo;
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

// GLOBAL STATE
bool menuPrinted = false;

// PRODUCT LIST UNTUK SIMULASI SERIAL
const int PRODUCT_COUNT = 4;
String productIds[PRODUCT_COUNT] = {"cola", "sprite", "susu", "kopi"};

const int QR_VERSION = 15;
const int QR_BUFFER_SIZE = 1024;
uint8_t qrCodeData[QR_BUFFER_SIZE];

String lastApiErrorCode = "NONE";
String lastApiErrorMessage = "NONE";
int lastTransactionPrice = 0;
int lastTransactionTotalPrice = 0;

struct MachineCommand {
  String command;
  String transactionId;
  String itemId;
  String itemName;
  String paymentState;
  String orderState;
};

// ================= LCD HELPER =================

void showLCD(String title, String subtitle, uint16_t bgColor) {
  tft.fillScreen(bgColor);
  tft.setTextColor(ILI9341_WHITE);

  tft.setTextSize(2);
  tft.setCursor(25, 75);
  tft.println(title);

  tft.setTextSize(2);
  tft.setCursor(25, 125);
  tft.println(subtitle);
}

void drawQRCode(String text) {
  text.trim();

  if (text == "" || text == "NONE") {
    showLCD("PAYMENT QR", "NOT FOUND", ILI9341_RED);
    return;
  }

  QRCode qrcode;

  // Version 15 keeps most QRIS strings scannable on 320x240 at scale 2.
  qrcode_initText(&qrcode, qrCodeData, QR_VERSION, ECC_LOW, text.c_str());

  int scale = 2;
  int qrPixelSize = qrcode.size * scale;

  if (qrPixelSize > 180) {
    scale = 1;
    qrPixelSize = qrcode.size * scale;
  }

  int qrX = (320 - qrPixelSize) / 2;
  int qrY = 48;

  tft.fillScreen(ILI9341_WHITE);
  tft.setTextColor(ILI9341_BLACK);

  tft.setTextSize(2);
  tft.setCursor(84, 12);
  tft.println("QRIS PAYMENT");

  tft.fillRect(qrX, qrY, qrPixelSize, qrPixelSize, ILI9341_WHITE);

  for (uint8_t y = 0; y < qrcode.size; y++) {
    for (uint8_t x = 0; x < qrcode.size; x++) {
      if (qrcode_getModule(&qrcode, x, y)) {
        tft.fillRect(qrX + x * scale, qrY + y * scale, scale, scale, ILI9341_BLACK);
      }
    }
  }

  tft.setTextSize(2);
  tft.setCursor(96, 218);
  tft.println("SCAN TO PAY");
}

void showPaymentQr(String paymentMethod, String qrString, String paymentUrl, String qrUrl) {
  paymentMethod.trim();
  qrString.trim();
  qrUrl.trim();
  paymentUrl.trim();

  if (paymentMethod == "qris" && qrString != "" && qrString != "NONE") {
    drawQRCode(qrString);
    return;
  }

  if (qrUrl != "" && qrUrl != "NONE") {
    drawQRCode(qrUrl);
    return;
  }

  if (paymentUrl != "" && paymentUrl != "NONE") {
    drawQRCode(paymentUrl);
    return;
  }

  showLCD("PAYMENT QR", "NOT FOUND", ILI9341_RED);
}

// ================= WIFI =================

void connectWiFi() {
  Serial.println("\nConnecting WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected!");
}

// ================= HTTP HELPER =================

bool httpPostJson(String endpoint, String body, String &responseBody) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi tidak terkoneksi.");
    return false;
  }

  String url = String(BACKEND_BASE_URL) + endpoint;

  WiFiClientSecure *secureClient = new WiFiClientSecure;
  HTTPClient *http = new HTTPClient;

  if (secureClient == nullptr || http == nullptr) {
    Serial.println("Gagal alokasi HTTP client.");
    delete secureClient;
    delete http;
    return false;
  }

  secureClient->setInsecure();
  secureClient->setTimeout(20000);

  http->setTimeout(20000);
  http->setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  Serial.print("POST ");
  Serial.println(url);
  Serial.print("Body: ");
  Serial.println(body);

  if (!http->begin(*secureClient, url)) {
    Serial.println("HTTP begin gagal.");
    delete http;
    delete secureClient;
    return false;
  }

  http->addHeader("Content-Type", "application/json");
  http->addHeader("Accept", "application/json");
  http->addHeader("User-Agent", "ESP32-VendX");
  http->addHeader("Connection", "close");

  int httpCode = http->POST(body);
  responseBody = http->getString();

  Serial.print("HTTP Code: ");
  Serial.println(httpCode);
  Serial.print("Response: ");
  Serial.println(responseBody);

  http->end();
  delete http;
  delete secureClient;

  return httpCode >= 200 && httpCode < 300;
}

bool httpGetJson(String endpoint, String &responseBody) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi tidak terkoneksi.");
    return false;
  }

  String url = String(BACKEND_BASE_URL) + endpoint;

  WiFiClientSecure *secureClient = new WiFiClientSecure;
  HTTPClient *http = new HTTPClient;

  if (secureClient == nullptr || http == nullptr) {
    Serial.println("Gagal alokasi HTTP client.");
    delete secureClient;
    delete http;
    return false;
  }

  secureClient->setInsecure();
  secureClient->setTimeout(20000);

  http->setTimeout(20000);
  http->setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  Serial.print("GET ");
  Serial.println(url);

  if (!http->begin(*secureClient, url)) {
    Serial.println("HTTP begin gagal.");
    delete http;
    delete secureClient;
    return false;
  }

  http->addHeader("Accept", "application/json");
  http->addHeader("User-Agent", "ESP32-VendX");
  http->addHeader("Connection", "close");

  int httpCode = http->GET();
  responseBody = http->getString();

  Serial.print("HTTP Code: ");
  Serial.println(httpCode);
  Serial.print("Response: ");
  Serial.println(responseBody);

  http->end();
  delete http;
  delete secureClient;

  return httpCode >= 200 && httpCode < 300;
}

// ================= MACHINE EVENT / COMMAND API =================

bool sendMachineEvent(String event, String transactionId = "NONE", String itemId = "NONE", String message = "NONE") {
  if (event == "" || event == "NONE") {
    Serial.println("Event mesin tidak valid.");
    return false;
  }

  StaticJsonDocument<384> requestDoc;
  requestDoc["event"] = event;

  if (transactionId != "" && transactionId != "NONE") {
    requestDoc["transaction_id"] = transactionId;
  }

  if (itemId != "" && itemId != "NONE") {
    requestDoc["item_id"] = itemId;
  }

  if (message != "" && message != "NONE") {
    requestDoc["message"] = message;
  }

  String requestBody;
  serializeJson(requestDoc, requestBody);

  String responseBody;
  String endpoint = "/api/machines/" + String(MACHINE_ID) + "/events";

  return httpPostJson(endpoint, requestBody, responseBody);
}

bool getMachineCommand(MachineCommand &cmd) {
  String responseBody;
  String endpoint = "/api/machines/" + String(MACHINE_ID) + "/command";

  if (!httpGetJson(endpoint, responseBody)) {
    Serial.println("GET command dari backend gagal.");
    return false;
  }

  DynamicJsonDocument responseDoc(1536);
  DeserializationError error = deserializeJson(responseDoc, responseBody);

  if (error) {
    Serial.print("Parse response command gagal: ");
    Serial.println(error.c_str());
    return false;
  }

  bool success = responseDoc["success"] | false;
  if (!success) {
    Serial.println("Backend menolak get command.");
    const char *message = responseDoc["message"] | "No message";
    Serial.print("Message: ");
    Serial.println(message);
    return false;
  }

  JsonVariant data = responseDoc["data"];
  if (data.isNull()) {
    Serial.println("Data command tidak ditemukan di response backend.");
    return false;
  }

  cmd.command = data["command"] | "IDLE";
  cmd.transactionId = data["transaction_id"] | "NONE";
  cmd.itemId = data["item_id"] | "NONE";
  cmd.itemName = data["item_name"] | "NONE";
  cmd.paymentState = data["payment_state"] | "NONE";
  cmd.orderState = data["order_state"] | "NONE";

  return true;
}

bool waitDispenseCommand(String transactionId, String &itemName, unsigned long maxWaitMs) {
  unsigned long start = millis();

  Serial.println("\nMenunggu command dari Backend API...");
  Serial.print("Transaction ID: ");
  Serial.println(transactionId);

  while (millis() - start < maxWaitMs) {
    MachineCommand cmd;

    if (getMachineCommand(cmd)) {
      Serial.print("Command: ");
      Serial.print(cmd.command);
      Serial.print(" | Tx: ");
      Serial.print(cmd.transactionId);
      Serial.print(" | Payment: ");
      Serial.print(cmd.paymentState);
      Serial.print(" | Order: ");
      Serial.println(cmd.orderState);

      if (cmd.transactionId != "NONE" && cmd.transactionId != "" && cmd.transactionId != transactionId) {
        Serial.println("Command untuk transaksi lain, abaikan.");
        delay(3000);
        continue;
      }

      if (cmd.command == "DISPENSE") {
        if (cmd.itemName != "" && cmd.itemName != "NONE") {
          itemName = cmd.itemName;
        }

        Serial.println("Command DISPENSE diterima.");
        return true;
      }

      if (cmd.command == "PAYMENT_TIMEOUT") {
        Serial.println("Backend menetapkan PAYMENT_TIMEOUT.");
        showLCD("PAYMENT", "TIMEOUT", ILI9341_RED);
        sendMachineEvent("PAYMENT_TIMEOUT_ACK", transactionId);
        return false;
      }

      if (cmd.command == "NEEDS_REVIEW") {
        Serial.println("Backend meminta review payment.");
        showLCD("PAYMENT", "REVIEW", ILI9341_RED);
        return false;
      }

      if (cmd.command == "ERROR") {
        Serial.println("Backend mengirim command ERROR.");
        showLCD("SYSTEM", "ERROR", ILI9341_RED);
        return false;
      }
    } else {
      Serial.println("Gagal membaca command. Coba lagi...");
    }

    delay(3000);
  }

  Serial.println("Local safety timeout menunggu command.");
  showLCD("WAIT", "TIMEOUT", ILI9341_RED);
  return false;
}

// ================= BACKEND API =================

bool createTransaction(
  String itemId,
  int qty,
  String &transactionId,
  String &itemName,
  String &paymentMethod,
  String &paymentUrl,
  String &qrUrl,
  String &qrString,
  String &snapToken
) {
  lastApiErrorCode = "NONE";
  lastApiErrorMessage = "NONE";
  lastTransactionPrice = 0;
  lastTransactionTotalPrice = 0;

  StaticJsonDocument<384> requestDoc;
  requestDoc["machine_id"] = MACHINE_ID;
  requestDoc["item_id"] = itemId;
  requestDoc["qty"] = qty;
  requestDoc["payment_method"] = PAYMENT_METHOD;

  String requestBody;
  serializeJson(requestDoc, requestBody);

  String responseBody;

  bool httpOk = httpPostJson("/api/transactions", requestBody, responseBody);

  DynamicJsonDocument responseDoc(4096);
  DeserializationError error = deserializeJson(responseDoc, responseBody);

  if (error) {
    Serial.print("Parse response create transaction gagal: ");
    Serial.println(error.c_str());
    lastApiErrorCode = httpOk ? "PARSE_ERROR" : "HTTP_ERROR";
    lastApiErrorMessage = httpOk ? "Invalid backend response" : "Create transaction request failed";
    return false;
  }

  bool success = responseDoc["success"] | false;
  if (!httpOk || !success) {
    Serial.println("Backend menolak create transaction.");
    lastApiErrorCode = responseDoc["code"] | (httpOk ? "UNKNOWN_ERROR" : "HTTP_ERROR");
    lastApiErrorMessage = responseDoc["message"] | (httpOk ? "Unknown error" : "Create transaction request failed");
    Serial.print("Code: ");
    Serial.println(lastApiErrorCode);
    Serial.print("Message: ");
    Serial.println(lastApiErrorMessage);
    return false;
  }

  transactionId = responseDoc["data"]["transaction_id"] | "NONE";
  itemName = responseDoc["data"]["item_name"] | itemId;
  paymentMethod = responseDoc["data"]["payment_method"] | "NONE";
  paymentUrl = responseDoc["data"]["payment_url"] | "NONE";
  qrUrl = responseDoc["data"]["qr_url"] | "NONE";
  qrString = responseDoc["data"]["qr_string"] | "NONE";
  snapToken = responseDoc["data"]["snap_token"] | "NONE";
  lastTransactionPrice = responseDoc["data"]["price"] | 0;
  lastTransactionTotalPrice = responseDoc["data"]["total_price"] | 0;

  String paymentState = responseDoc["data"]["payment_state"] | "NONE";
  String orderState = responseDoc["data"]["order_state"] | "NONE";

  Serial.print("Backend Payment State: ");
  Serial.println(paymentState);
  Serial.print("Backend Order State  : ");
  Serial.println(orderState);

  if (transactionId == "NONE" || transactionId == "") {
    Serial.println("transaction_id tidak ditemukan di response backend.");
    lastApiErrorCode = "INVALID_RESPONSE";
    lastApiErrorMessage = "transaction_id missing";
    return false;
  }

  return true;
}

bool sendDispenseResultToApi(String transactionId, String result) {
  if (result != "SUCCESS" && result != "FAILED") {
    Serial.println("dispense_result tidak valid. Hanya boleh SUCCESS atau FAILED.");
    return false;
  }

  StaticJsonDocument<256> requestDoc;
  requestDoc["machine_id"] = MACHINE_ID;
  requestDoc["dispense_result"] = result;

  String requestBody;
  serializeJson(requestDoc, requestBody);

  String responseBody;
  String endpoint = "/api/transactions/" + transactionId + "/dispense-result";

  return httpPostJson(endpoint, requestBody, responseBody);
}

// ================= HARDWARE =================

void playBuzzer() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(700);
  digitalWrite(BUZZER_PIN, LOW);
}

void runServo() {
  Serial.println("\nServo bergerak...");

  myServo.write(180);
  delay(2500);

  myServo.write(90);
  delay(1000);

  Serial.println("Servo selesai.");
}

bool simulateDropSensor() {
  Serial.println("\nApakah barang jatuh?");
  Serial.println("y = barang jatuh");
  Serial.println("n = barang gagal jatuh");
  Serial.print("Input sensor: ");

  String input = "";

  while (input == "") {
    if (Serial.available()) {
      input = Serial.readStringUntil('\n');
      input.trim();
    }

    delay(10);
  }

  Serial.println(input);

  return input == "y" || input == "Y";
}

// ================= MENU SERIAL SIMULATION =================

void printProductMenu() {
  Serial.println("\n========== VENDX MENU ==========");
  Serial.println("Pilih produk:");
  Serial.println("1. Cola");
  Serial.println("2. Sprite");
  Serial.println("3. Susu");
  Serial.println("4. Kopi");
  Serial.println("Ketik angka 1-4 lalu Enter.");
  Serial.println("================================");

  showLCD("SELECT ITEM", "1-4 SERIAL", ILI9341_BLUE);
}

String readSelectedItem() {
  if (!Serial.available()) return "NONE";

  String input = Serial.readStringUntil('\n');
  input.trim();

  if (input == "1") return "cola";
  if (input == "2") return "sprite";
  if (input == "3") return "susu";
  if (input == "4") return "kopi";

  Serial.println("Pilihan tidak valid.");
  return "NONE";
}

// ================= PURCHASE FLOW =================

void processPurchase(String itemId) {
  int qty = 1;

  Serial.println("\n========== ORDER START ==========");
  Serial.print("Item ID: ");
  Serial.println(itemId);

  sendMachineEvent("ITEM_SELECTED", "NONE", itemId);
  showLCD("CREATE ORDER", itemId, ILI9341_ORANGE);

  String transactionId = "NONE";
  String itemName = itemId;
  String paymentMethod = "NONE";
  String paymentUrl = "NONE";
  String qrUrl = "NONE";
  String qrString = "NONE";
  String snapToken = "NONE";

  bool created = createTransaction(
    itemId,
    qty,
    transactionId,
    itemName,
    paymentMethod,
    paymentUrl,
    qrUrl,
    qrString,
    snapToken
  );

  if (!created) {
    Serial.print("Create transaction failed: ");
    Serial.print(lastApiErrorCode);
    Serial.print(" - ");
    Serial.println(lastApiErrorMessage);

    if (lastApiErrorCode == "OUT_OF_STOCK") {
      showLCD("OUT OF STOCK", itemId, ILI9341_RED);
    } else if (lastApiErrorCode == "ITEM_INACTIVE") {
      showLCD("ITEM", "INACTIVE", ILI9341_RED);
    } else if (lastApiErrorCode == "ITEM_NOT_FOUND") {
      showLCD("ITEM", "NOT FOUND", ILI9341_RED);
    } else if (lastApiErrorCode == "INVALID_PRICE") {
      showLCD("PRICE", "INVALID", ILI9341_RED);
    } else if (lastApiErrorCode == "VALIDATION_ERROR") {
      showLCD("ORDER", "INVALID", ILI9341_RED);
    } else if (lastApiErrorCode == "MIDTRANS_ERROR") {
      showLCD("PAYMENT", "FAILED", ILI9341_RED);
    } else {
      showLCD("ORDER", "FAILED", ILI9341_RED);
    }

    sendMachineEvent("ERROR", "NONE", itemId, lastApiErrorMessage);
    sendMachineEvent("IDLE");
    delay(2500);
    showLCD("READY", "SELECT ITEM", ILI9341_BLUE);
    menuPrinted = false;
    return;
  }

  Serial.println("\nTransaction created.");
  Serial.print("Transaction ID: ");
  Serial.println(transactionId);
  Serial.print("Item Name     : ");
  Serial.println(itemName);
  Serial.print("Price         : ");
  Serial.println(lastTransactionPrice);
  Serial.print("Total Price   : ");
  Serial.println(lastTransactionTotalPrice);
  Serial.print("Payment Method: ");
  Serial.println(paymentMethod);
  Serial.print("Payment URL   : ");
  Serial.println(paymentUrl);
  Serial.print("QR URL        : ");
  Serial.println(qrUrl);
  Serial.print("QR String Len : ");
  Serial.println(qrString == "NONE" ? 0 : qrString.length());
  Serial.print("Snap Token    : ");
  Serial.println(snapToken);

  showPaymentQr(paymentMethod, qrString, paymentUrl, qrUrl);
  Serial.println("Scan QRIS di layar TFT. Jika QR kosong, gunakan fallback URL dari Serial.");
  sendMachineEvent("QR_DISPLAYED", transactionId);

  String dispenseItemName = itemName;
  bool canDispense = waitDispenseCommand(transactionId, dispenseItemName, 180000);

  if (!canDispense) {
    Serial.println("Tidak ada command dispense. Kembali ke menu.");
    sendMachineEvent("IDLE");
    delay(2500);
    showLCD("READY", "SELECT ITEM", ILI9341_BLUE);
    menuPrinted = false;
    return;
  }

  sendMachineEvent("DISPENSE_STARTED", transactionId);
  showLCD("DISPENSING", dispenseItemName, ILI9341_ORANGE);

  runServo();

  bool detected = simulateDropSensor();

  if (detected) {
    Serial.println("Barang berhasil jatuh.");

    showLCD("TRANSACTION", "SUCCESS", ILI9341_GREEN);
    playBuzzer();

    if (!sendDispenseResultToApi(transactionId, "SUCCESS")) {
      Serial.println("Gagal kirim dispense result SUCCESS ke API.");
    }
  } else {
    Serial.println("Barang gagal jatuh.");

    showLCD("TRANSACTION", "FAILED", ILI9341_RED);

    if (!sendDispenseResultToApi(transactionId, "FAILED")) {
      Serial.println("Gagal kirim dispense result FAILED ke API.");
    }
  }

  delay(3000);

  Serial.println("Dispense selesai. Status transaksi sudah dikirim lewat Backend API.");
  sendMachineEvent("IDLE");
  showLCD("READY", "SELECT ITEM", ILI9341_BLUE);

  menuPrinted = false;

  Serial.println("========== ORDER END ==========\n");
}

// ================= SETUP =================

void setup() {
  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  myServo.attach(SERVO_PIN);
  myServo.write(90);

  tft.begin();
  tft.setRotation(1);

  showLCD("BOOTING", "SYSTEM START", ILI9341_BLACK);

  connectWiFi();
  sendMachineEvent("ONLINE");
  sendMachineEvent("IDLE");

  showLCD("READY", "SELECT ITEM", ILI9341_BLUE);

  Serial.println("\nESP32 READY - VendX Order Mode");
}

// ================= LOOP =================

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Reconnecting...");
    connectWiFi();
  }

  if (!menuPrinted) {
    printProductMenu();
    menuPrinted = true;
  }

  String selectedItem = readSelectedItem();

  if (selectedItem != "NONE") {
    processPurchase(selectedItem);
  }

  delay(200);
}
