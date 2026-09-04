#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <WiFi.h>
#include <HTTPClient.h>

// --- WIFI CREDENTIALS ---
const char* ssid = "Xyz";
const char* password = "123456";

// --- SENSOR PINS ---
#define DHTPIN 23
#define DHTTYPE DHT11
#define BUTTON_PIN 12
#define LED_PIN 2

// --- MOTOR DRIVER PINS ---
#define FAN_IN1 32
#define FAN_IN2 33
#define PUMP_IN3 25
#define PUMP_IN4 26

// --- SENSOR OBJECTS ---
DHT dht(DHTPIN, DHTTYPE);
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
Adafruit_MPU6050 mpu;

// --- MONITORING VARIABLES ---
bool monitoring = false;
bool lastButtonState = false;

float ambientTemp = 0;
float humidity = 0;
float bodyTemp = 0;
bool motionDetected = true;

// --- THRESHOLDS ---
const float AMBIENT_TEMP_THRESHOLD = 35.0;  // °C
const float BODY_TEMP_THRESHOLD = 40.0;     // °C
const int MOTION_TIMEOUT = 60000;           // 1 min in milliseconds
unsigned long lastMotionTime = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  // --- INIT PINS ---
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(FAN_IN1, OUTPUT);
  pinMode(FAN_IN2, OUTPUT);
  pinMode(PUMP_IN3, OUTPUT);
  pinMode(PUMP_IN4, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  // --- INIT SENSORS ---
  Wire.begin(21, 22);

  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050 chip!");
  } else {
    Serial.println("MPU6050 ready.");
  }

  if (!mlx.begin()) {
    Serial.println("Failed to find MLX90614 sensor!");
  } else {
    Serial.println("MLX90614 ready.");
  }

  dht.begin();
  Serial.println("DHT11 ready.");

  lastMotionTime = millis();

  // --- CONNECT TO WIFI ---
  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("System ready. Press button to start/stop monitoring.");
}

void loop() {
  // --- BUTTON TOGGLE MONITORING ---
  bool buttonState = (digitalRead(BUTTON_PIN) == LOW);
  if (buttonState != lastButtonState) {
    delay(50);  // debounce
    if (buttonState == LOW) {
      monitoring = !monitoring;
      Serial.print("Monitoring ");
      Serial.println(monitoring ? "STARTED" : "STOPPED");

      if (!monitoring) {
        stopActuators();
        digitalWrite(LED_PIN, LOW);
      }
    }
    lastButtonState = buttonState;
  }

  if (!monitoring) return;

  // --- READ DHT11 ---
  humidity = dht.readHumidity();
  ambientTemp = dht.readTemperature();

  // --- READ MLX90614 ---
  bodyTemp = mlx.readObjectTempC();

  // --- READ MPU6050 ---
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // Simple motion detection
  float accelMagnitude = sqrt(
    a.acceleration.x * a.acceleration.x +
    a.acceleration.y * a.acceleration.y +
    a.acceleration.z * a.acceleration.z
  );

  if (accelMagnitude > 0.5) { // movement threshold
    motionDetected = true;
    lastMotionTime = millis();
  } else {
    if (millis() - lastMotionTime > MOTION_TIMEOUT) {
      motionDetected = false;
    }
  }

  // --- CHECK CONDITIONS ---
  bool highRiskEnv = (ambientTemp >= AMBIENT_TEMP_THRESHOLD) && (humidity > 60);
  bool highBodyTemp = (bodyTemp >= BODY_TEMP_THRESHOLD);
  bool danger = (!motionDetected) || (highRiskEnv && highBodyTemp);

  // --- ACTUATE FAN & PUMP ---
  if (danger) {
    Serial.println("⚠️ Risk detected! Activating cooling systems...");
    digitalWrite(LED_PIN, HIGH);
    turnFan(true);
    turnPump(true);
    sendAlert(highBodyTemp, highRiskEnv, !motionDetected);
  } else {
    digitalWrite(LED_PIN, LOW);
    turnFan(false);
    turnPump(false);
  }

  // --- PRINT STATUS ---
  Serial.print("Ambient: ");
  Serial.print(ambientTemp);
  Serial.print(" °C  Humidity: ");
  Serial.print(humidity);
  Serial.print("%  Body: ");
  Serial.print(bodyTemp);
  Serial.print(" °C  Motion: ");
  Serial.println(motionDetected ? "YES" : "NO");

  delay(2000);
}

// --- ACTUATORS ---
void turnFan(bool on) {
  if (on) {
    digitalWrite(FAN_IN1, HIGH);
    digitalWrite(FAN_IN2, LOW);
  } else {
    digitalWrite(FAN_IN1, LOW);
    digitalWrite(FAN_IN2, LOW);
  }
}

void turnPump(bool on) {
  if (on) {
    digitalWrite(PUMP_IN3, HIGH);
    digitalWrite(PUMP_IN4, LOW);
  } else {
    digitalWrite(PUMP_IN3, LOW);
    digitalWrite(PUMP_IN4, LOW);
  }
}

void stopActuators() {
  turnFan(false);
  turnPump(false);
}

// --- WIFI ALERT ---
void sendAlert(bool bodyTempHigh, bool envHigh, bool motionLost) {
  Serial.println("----- ALERT -----");
  String alertMessage = "";

  if (bodyTempHigh) {
    Serial.println("Body temperature above safe limit!");
    alertMessage += "BodyTempHigh;";
  }
  if (envHigh) {
    Serial.println("Environmental temperature/humidity high!");
    alertMessage += "EnvHigh;";
  }
  if (motionLost) {
    Serial.println("No motion detected! Possible danger!");
    alertMessage += "NoMotion;";
  }
  Serial.println("-----------------");

  // --- Send Alert via HTTP ---
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String serverURL = "http://your-server.com/alert"; // <-- Change this
    http.begin(serverURL);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String postData =
      "ambient=" + String(ambientTemp) +
      "&humidity=" + String(humidity) +
      "&body=" + String(bodyTemp) +
      "&motion=" + String(motionDetected ? "1" : "0") +
      "&alert=" + alertMessage;

    int httpResponseCode = http.POST(postData);

    if (httpResponseCode > 0) {
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);
    } else {
      Serial.print("HTTP POST failed, error: ");
      Serial.println(String(httpResponseCode));
    }
    http.end();
  } else {
    Serial.println("WiFi not connected. Cannot send alert.");
  }
}


