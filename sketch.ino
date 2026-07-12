/*
  Carnation Environmental Monitor
  --------------------------------
  Hardware : ESP32 DevKit V1, DHT22, MQ-5 gas sensor, 16x2 I2C LCD
  Purpose  : Reads temperature, humidity and air quality, shows a live
             status on the LCD, and streams every reading as a
             time-series point to InfluxDB Cloud (bucket: carnation_monitor).

  Wiring (see diagram.json):
    DHT22  SDA  -> GPIO4
    MQ-5   AOUT -> GPIO34 (ADC1_CH6)
    LCD    SDA  -> GPIO21
    LCD    SCL  -> GPIO22
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ---------- Pin configuration ----------
#define DHTPIN   4
#define DHTTYPE  DHT22
#define MQ_PIN   34

DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);   // 16x2 LCD, PCF8574 I2C backpack at 0x27

// ---------- WiFi ----------
// Wokwi's built-in virtual WiFi (real internet access from the simulator)
const char* WIFI_SSID     = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

// ---------- InfluxDB Cloud ----------
// Replace with your own InfluxDB Cloud cluster URL, org, bucket and token.
// Data Explorer -> "..." -> API Tokens to generate a write token.
const char* INFLUX_URL    = "https://us-east-1-1.aws.cloud2.influxdata.com"; // cluster URL
const char* INFLUX_ORG    = "Dev_team";
const char* INFLUX_BUCKET = "carnation_monitor";
const char* INFLUX_TOKEN  = "PASTE_YOUR_INFLUXDB_API_TOKEN_HERE";
const char* MEASUREMENT   = "carnation_monitor";
const char* DEVICE_TAG    = "esp32_01";

// ---------- Thresholds used to derive status flags ----------
const float TEMP_LOW_C   = 18.0;
const float TEMP_HIGH_C  = 28.0;
const float HUM_LOW_PCT  = 40.0;
const float HUM_HIGH_PCT = 70.0;

// ---------- Timing ----------
const unsigned long SEND_INTERVAL_MS = 20000;  // push a reading every 20s
unsigned long lastSend = 0;

void connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi connected. IP: ");
  Serial.println(WiFi.localIP());
}

String statusFor(float value, float low, float high) {
  if (value < low)  return "LOW";
  if (value > high) return "HIGH";
  return "OK";
}

void updateLCD(float t, float h, int mq, const String &tempStatus, const String &humStatus) {
  lcd.clear();

  // Line 1: "Temp: -23.7 C   L"
  char line1[17];
  snprintf(line1, sizeof(line1), "Temp: %.1f C %5s", t, tempStatus.substring(0, 1).c_str());
  lcd.setCursor(0, 0);
  lcd.print(line1);

  // Line 2: "Hum:   57.5% OK"
  char line2[17];
  snprintf(line2, sizeof(line2), "Hum:  %.1f%% %4s", h, humStatus.c_str());
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

void logToSerial(float t, float h, int mq, const String &tempStatus, const String &humStatus) {
  Serial.print("Temp: ");
  Serial.print(t, 1);
  Serial.print("\u00B0C [");
  Serial.print(tempStatus);
  Serial.print("] | Hum: ");
  Serial.print(h, 1);
  Serial.print("% [");
  Serial.print(humStatus);
  Serial.print("] | MQ: ");
  Serial.println(mq);
}

bool sendToInfluxDB(float t, float h, int mq, const String &tempStatus, const String &humStatus) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("InfluxDB write skipped: WiFi not connected");
    return false;
  }

  // InfluxDB line protocol:
  // measurement,tag_set field_set
  String line = String(MEASUREMENT) + ",device=" + DEVICE_TAG +
                " temperature=" + String(t, 1) +
                ",humidity=" + String(h, 1) +
                ",air_quality=" + String(mq) +
                ",temp_status=\"" + tempStatus + "\"" +
                ",hum_status=\"" + humStatus + "\"";

  HTTPClient http;
  String url = String(INFLUX_URL) + "/api/v2/write?org=" + INFLUX_ORG +
               "&bucket=" + INFLUX_BUCKET + "&precision=s";

  http.begin(url);
  http.addHeader("Authorization", String("Token ") + INFLUX_TOKEN);
  http.addHeader("Content-Type", "text/plain; charset=utf-8");

  int httpCode = http.POST(line);
  bool ok = (httpCode == 204);

  if (ok) {
    Serial.println("InfluxDB write OK");
  } else {
    Serial.print("InfluxDB write FAILED, HTTP code: ");
    Serial.println(httpCode);
    Serial.println(http.getString());
  }

  http.end();
  return ok;
}

void setup() {
  Serial.begin(115200);
  delay(200);

  dht.begin();

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Carnation Mon.");
  lcd.setCursor(0, 1);
  lcd.print("Booting...");

  connectWiFi();
}

void loop() {
  if (millis() - lastSend >= SEND_INTERVAL_MS) {
    lastSend = millis();

    float t = dht.readTemperature();
    float h = dht.readHumidity();
    int mq  = analogRead(MQ_PIN);

    if (isnan(t) || isnan(h)) {
      Serial.println("Failed to read from DHT22 sensor!");
      return;
    }

    String tempStatus = statusFor(t, TEMP_LOW_C, TEMP_HIGH_C);
    String humStatus  = statusFor(h, HUM_LOW_PCT, HUM_HIGH_PCT);

    updateLCD(t, h, mq, tempStatus, humStatus);
    logToSerial(t, h, mq, tempStatus, humStatus);
    sendToInfluxDB(t, h, mq, tempStatus, humStatus);
  }
}
