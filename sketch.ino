#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

// Pin Definitions
#define DHTPIN 4        // DHT22 data pin
#define DHTTYPE DHT22
#define MQ_PIN 34       // MQ sensor analog pin

// Carnation optimal ranges
#define TEMP_MIN 18.0
#define TEMP_MAX 24.0
#define HUM_MIN 50.0
#define HUM_MAX 70.0

// WiFi
const char* WIFI_SSID     = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";
const int   WIFI_CHANNEL  = 6;   // skips the scan

// InfluxDB Cloud settings 
const char* INFLUX_URL   = "https://us-east-1-1.aws.cloud2.influxdata.com";
const char* INFLUX_ORG   = "Dev_team";
const char* INFLUX_BUCKET= "carnation_monitor";
const char* INFLUX_TOKEN = "JL1trn813khEprs_NirDoro2FCUIUPqzeD-xX0zpYLDnh6sdxX0sqKaQGdsaza7_f1m8ROifCs7AoJdzno4Tzw==";

DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 20, 4); 

void setup() {
  Serial.begin(115200);
  dht.begin();

  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("  Carnation Monitor ");
  lcd.setCursor(0, 1);
  lcd.print("   Connecting WiFi  ");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("  Carnation Monitor ");
  lcd.setCursor(0, 1);
  lcd.print("   Initializing...  ");
  delay(1500);
  lcd.clear();
}

void loop() {
  float temperature = dht.readTemperature();
  float humidity    = dht.readHumidity();
  int   mqRaw       = analogRead(MQ_PIN);

  // Validate DHT22 reading
  if (isnan(temperature) || isnan(humidity)) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Sensor Error!");
    Serial.println("DHT22 read failed");
    delay(2000);
    return;
  }

  // Determine status
  String tempStatus = getTempStatus(temperature);
  String humStatus  = getHumStatus(humidity);

  // Print to Serial
  Serial.printf("Temp: %.1f°C [%s] | Hum: %.1f%% [%s] | MQ: %d\n",
                temperature, tempStatus.c_str(),
                humidity,    humStatus.c_str(),
                mqRaw);

  // LCD Display
  lcd.setCursor(0, 0);
  lcd.printf("Temp: %5.1f C  %-4s", temperature, tempStatus.c_str());

  lcd.setCursor(0, 1);
  lcd.printf("Hum:  %5.1f%%  %-4s", humidity, humStatus.c_str());

  lcd.setCursor(0, 2);
  lcd.printf("Air Quality: %4d   ", mqRaw);

  lcd.setCursor(0, 3);
  if (tempStatus == "OK" && humStatus == "OK") {
    lcd.print(" Conditions Optimal ");
  } else {
    lcd.print(buildAlert(tempStatus, humStatus));
  }

  // Send to InfluxDB Cloud 
  sendToInflux(temperature, humidity, mqRaw, tempStatus, humStatus);

  delay(2000);
}

// Helper functions 

String getTempStatus(float t) {
  if (t < TEMP_MIN) return "LOW";
  if (t > TEMP_MAX) return "HIGH";
  return "OK";
}

String getHumStatus(float h) {
  if (h < HUM_MIN) return "LOW";
  if (h > HUM_MAX) return "HIGH";
  return "OK";
}

String buildAlert(String ts, String hs) {
  String msg = "";
  if (ts != "OK") msg += "Temp " + ts + " ";
  if (hs != "OK") msg += "Hum "  + hs;
  while (msg.length() < 20) msg += " ";
  if (msg.length() > 20)    msg  = msg.substring(0, 20);
  return msg;
}

// Builds a line-protocol point and POSTs it to InfluxDB Cloud over HTTPS
void sendToInflux(float temperature, float humidity, int mqRaw,
                   String tempStatus, String humStatus) {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, skipping Influx write");
    return;
  }

  String lineProtocol = "carnation_monitor,device=esp32_01 "
    "temperature=" + String(temperature, 1) +
    ",humidity=" + String(humidity, 1) +
    ",air_quality=" + String(mqRaw) +
    ",temp_status=\"" + tempStatus + "\"" +
    ",hum_status=\"" + humStatus + "\"";

  WiFiClientSecure client;
  client.setInsecure();  // skip TLS cert validatio

  HTTPClient http;
  String url = String(INFLUX_URL) + "/api/v2/write?org=" + INFLUX_ORG +
               "&bucket=" + INFLUX_BUCKET + "&precision=s";

  http.begin(client, url);
  http.addHeader("Authorization", "Token " + String(INFLUX_TOKEN));
  http.addHeader("Content-Type", "text/plain; charset=utf-8");

  int httpCode = http.POST(lineProtocol);

  if (httpCode == 204) {
    Serial.println("InfluxDB write OK");
  } else {
    Serial.printf("InfluxDB write failed, code: %d\n", httpCode);
    Serial.println(http.getString());
  }

  http.end();
}
