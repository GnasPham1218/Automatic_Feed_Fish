// ==================== fishfeeder_extended.ino ====================

// Thư viện
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <RTClib.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// ========== CẤU HÌNH (thay bằng thông tin của bạn) ==========
const char* WIFI_SSID = "VNPT";
const char* WIFI_PASSWORD = "bichduyenbuii997";

const char* MQTT_HOST = "f56d445d19d541e58e19a6a7a3972565.s1.eu.hivemq.cloud";
const uint16_t MQTT_PORT = 8883; // TLS
const char* MQTT_USER = "iot_device";
const char* MQTT_PASS = "sang123A@";

String CLIENT_ID; // sẽ khởi tạo trong setup()

// ========== PIN & DISPLAY ==========
#define SERVO_PIN 13
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// ========== TOPIC (PHẢI TRÙNG VỚI WEB) ==========
// Status topics (ESP sẽ publish, tốt nhất publish với retained=true)
const char* TOPIC_STATUS_TEMP = "fishfeeder/status/temperature";
const char* TOPIC_STATUS_FEED_COUNT = "fishfeeder/status/feedcount";
const char* TOPIC_STATUS_FOOD_LEVEL = "fishfeeder/status/foodlevel";
const char* TOPIC_STATUS_SCHEDULE_LIST = "fishfeeder/status/schedulelist";
const char* TOPIC_STATUS_AUTO_MODE = "fishfeeder/status/automode";
const char* TOPIC_STATUS_ROTATIONS = "fishfeeder/status/rotations";
const char* TOPIC_STATUS_SERVO_DURATION = "fishfeeder/status/servoduration";
const char* TOPIC_STATUS_ONLINE = "fishfeeder/status/online";
const char* TOPIC_STATUS_ACK = "fishfeeder/status/ack";
const char* TOPIC_STATUS_VERSION = "fishfeeder/status/version";
const char* TOPIC_STATUS_LASTFEED = "fishfeeder/status/lastfeedtime";

// Command topics (web -> ESP)
const char* TOPIC_CMD_FEED_NOW = "fishfeeder/command/feednow";
const char* TOPIC_CMD_SERVO_DURATION = "fishfeeder/command/servoduration";
const char* TOPIC_CMD_ROTATIONS = "fishfeeder/command/rotations";
const char* TOPIC_CMD_AUTO_MODE = "fishfeeder/command/automode";
const char* TOPIC_CMD_SCHEDULE_UPDATE = "fishfeeder/command/scheduleupdate";
const char* TOPIC_CMD_REFILL = "fishfeeder/command/refill";

// GET topics (web -> ESP requests ESP to re-publish current statuses)
const char* TOPIC_CMD_GET_STATUS = "fishfeeder/command/getstatus";
const char* TOPIC_CMD_GET_SCHEDULE = "fishfeeder/command/getschedule";

// ========== OBJECTS ==========
WiFiClientSecure espClient;
PubSubClient client(espClient);
Servo myServo;
RTC_DS3231 rtc;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Preferences prefs;

// ========== STATE ==========
unsigned long lastDisplayUpdate = 0;
unsigned long lastTempPublish = 0;
unsigned long lastScheduleCheck = 0; // kiểm tra lịch
int lastScheduledFeedMinute = -1;    // ngăn lặp trong cùng 1 phút

int servoDuration = 1000; // ms (mặc định)
int rotations = 1;
int feedCountToday = 0;
bool autoMode = false;
bool oledInitialized = false;
String schedulesJson = "[]"; // lưu JSON lịch hiện tại

// Mức thức ăn — ESP theo dõi (đơn vị %)
float currentFoodLevel = 100.0;
const float FOOD_CONSUMPTION_BASE = 2.5; // % tiêu thụ chuẩn cho 1 rotation ở 1000ms

// Flags (nếu cần dùng)
bool rtcPresent = false;

// === Feeding non-blocking state machine ===
bool feeding = false;
unsigned long feedPhaseStart = 0;
int feedPhase = 0; // 0 idle, 1 = servo on (rotate), 2 = between rotations
int feedIteration = 0;

// ========== HỖ TRỢ ==========
void updateDisplay(String l1, String l2, String l3);
void publishTemperature();
void startFeeding(); // non-blocking start
void handleFeeding(); // called in loop
void addSchedule(String time);
void removeSchedule(String time);
void setup_wifi();
void reconnect();
void callback(char* topic, byte* payload, unsigned int length);
void checkSchedules();
void publishAllStatuses(); // publish tất cả status (retained)
void publishScheduleList();
bool validTimeFormat(const String &t);
String isoNow();

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  delay(100);

  // seed random trước khi tạo CLIENT_ID
  randomSeed(analogRead(0));
  CLIENT_ID = "esp32_fishfeeder_" + String(random(0, 9999));
  Serial.println("CLIENT_ID = " + CLIENT_ID);

  Wire.begin();

  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC!");
    rtcPresent = false;
  } else {
    rtcPresent = true;
  }

  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    oledInitialized = true;
    Serial.println("SSD1306 OLED initialized");
  } else {
    Serial.println("SSD1306 allocation failed");
  }

  myServo.attach(SERVO_PIN);
  myServo.write(0);

  // Preferences: load persisted values
  prefs.begin("fishfeeder", false);
  rotations = prefs.getInt("rotations", rotations);
  servoDuration = prefs.getInt("servoDur", servoDuration);
  feedCountToday = prefs.getInt("feedCount", feedCountToday);
  currentFoodLevel = prefs.getFloat("foodLevel", currentFoodLevel);
  schedulesJson = prefs.getString("schedules", schedulesJson);

  setup_wifi();

  // TLS - nếu dùng certificate pinning thay đổi ở đây; dev: set insecure
  espClient.setInsecure();

  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setCallback(callback);

  Serial.println("System starting...");
  updateDisplay("Starting...", "", "Connecting...");
}

// ========== LOOP ==========
void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long now = millis();

  // Cập nhật màn hình mỗi 1s
  if (now - lastDisplayUpdate > 1000) {
    lastDisplayUpdate = now;
    DateTime t = rtc.now();
    char timestr[9];

    if (!rtcPresent || t.year() < 2000) {
      sprintf(timestr, "--:--:--");
    } else {
      sprintf(timestr, "%02d:%02d:%02d", t.hour(), t.minute(), t.second());
    }
    String modeStr = autoMode ? "Auto: ON" : "Auto: OFF";
    updateDisplay(client.connected() ? "MQTT: OK" : "MQTT: ---", String(timestr), modeStr);
  }

  // Publish nhiệt độ mỗi 60s
  if (millis() - lastTempPublish > 60000) {
    lastTempPublish = millis();
    publishTemperature();
  }

  // Kiểm tra lịch cho ăn tự động mỗi 10 giây
  if (autoMode && (millis() - lastScheduleCheck > 10000)) {
    lastScheduleCheck = millis();
    checkSchedules();
  }

  // Xử lý non-blocking feeding
  handleFeeding();
}

// ========== FUNCTIONS ==========

// Publish tất cả status hiện tại (dùng khi connect hoặc khi web yêu cầu GET)
void publishAllStatuses() {
  // Temperature (publish current reading if available)
  if (rtcPresent) {
    float temp = rtc.getTemperature();
    if (!(temp < -40 || temp > 80)) {
      client.publish(TOPIC_STATUS_TEMP, String(temp, 1).c_str(), true);
    }
  }

  // Feed count
  client.publish(TOPIC_STATUS_FEED_COUNT, String(feedCountToday).c_str(), true);

  // Food level
  client.publish(TOPIC_STATUS_FOOD_LEVEL, String(currentFoodLevel, 1).c_str(), true);

  // Schedule list
  publishScheduleList();

  // Auto mode
  client.publish(TOPIC_STATUS_AUTO_MODE, autoMode ? "ON" : "OFF", true);

  // Rotations & servo duration
  client.publish(TOPIC_STATUS_ROTATIONS, String(rotations).c_str(), true);
  client.publish(TOPIC_STATUS_SERVO_DURATION, String(servoDuration).c_str(), true);

  // Online flag
  client.publish(TOPIC_STATUS_ONLINE, "1", true);

  // Version
  client.publish(TOPIC_STATUS_VERSION, "v1.0.0", true);
}

// Publish schedule list with retain
void publishScheduleList() {
  if (schedulesJson.length() == 0) schedulesJson = "[]";
  client.publish(TOPIC_STATUS_SCHEDULE_LIST, schedulesJson.c_str(), true);
}

// Kiểm tra lịch & thực hiện feed nếu trùng khớp
void checkSchedules() {
  if (!rtcPresent) return;

  DateTime now = rtc.now();

  // Chỉ thực hiện nếu RTC hợp lệ
  if (now.year() < 2000) return;

  // Nếu cùng phút đã cho ăn, skip
  if (now.minute() == lastScheduledFeedMinute) {
    return;
  }

  char currentTimeStr[6]; // Format HH:MM
  sprintf(currentTimeStr, "%02d:%02d", now.hour(), now.minute());

  // Parse schedulesJson
  const size_t capacity = 1024;
  DynamicJsonDocument doc(capacity);
  DeserializationError err = deserializeJson(doc, schedulesJson);
  if (err) {
    // Serial.println("Failed to parse schedulesJson for checking.");
    return;
  }

  JsonArray arr = doc.as<JsonArray>();
  for (JsonVariant v : arr) {
    String scheduledTime = v.as<String>();
    if (scheduledTime == String(currentTimeStr)) {
      Serial.printf("!!! Auto-feed triggered by schedule: %s\n", currentTimeStr);
      startFeeding();
      lastScheduledFeedMinute = now.minute(); // đánh dấu đã cho ăn
      break;
    }
  }
}

// Non-blocking feeding: bắt đầu
void startFeeding() {
  if (feeding) {
    Serial.println("Already feeding, ignoring start request.");
    // send ack anyway
    client.publish(TOPIC_STATUS_ACK, "feed:BUSY", false);
    return;
  }
  feeding = true;
  feedPhase = 1; // start rotation (servo ON)
  feedPhaseStart = millis();
  feedIteration = 0;
  myServo.write(180);
  Serial.printf("Start feeding (non-blocking): %d rotations, %dms each\n", rotations, servoDuration);
  client.publish(TOPIC_STATUS_ACK, "feed:STARTED", false);
  // publish lastfeedtime when finished (in handleFeeding)
}

// Handle feeding phases in loop()
void handleFeeding() {
  if (!feeding) return;
  unsigned long now = millis();

  if (feedPhase == 1) {
    // servo on duration
    if (now - feedPhaseStart >= (unsigned long)servoDuration) {
      // turn off servo
      myServo.write(0);
      feedPhase = 2;
      feedPhaseStart = now;
    }
  } else if (feedPhase == 2) {
    // wait small gap between rotations (500ms)
    if (now - feedPhaseStart >= 500) {
      feedIteration++;
      if (feedIteration >= rotations) {
        // finish feeding
        feeding = false;
        // update feedCount and foodLevel, publish statuses
        feedCountToday++;
        // consume food
        float consumedAmount = rotations * FOOD_CONSUMPTION_BASE * (servoDuration / 1000.0f);
        currentFoodLevel -= consumedAmount;
        if (currentFoodLevel < 0.0) currentFoodLevel = 0.0;

        // persist
        prefs.putInt("feedCount", feedCountToday);
        prefs.putFloat("foodLevel", currentFoodLevel);

        // publish updates (retained where relevant)
        client.publish(TOPIC_STATUS_FEED_COUNT, String(feedCountToday).c_str(), true);
        client.publish(TOPIC_STATUS_FOOD_LEVEL, String(currentFoodLevel, 1).c_str(), true);
        client.publish(TOPIC_STATUS_ROTATIONS, String(rotations).c_str(), true);
        client.publish(TOPIC_STATUS_SERVO_DURATION, String(servoDuration).c_str(), true);
        client.publish(TOPIC_STATUS_AUTO_MODE, autoMode ? "ON" : "OFF", true);
        client.publish(TOPIC_STATUS_ACK, "feed:DONE", false);
        // publish last feed time
        client.publish(TOPIC_STATUS_LASTFEED, isoNow().c_str(), true);
        Serial.printf("Feeding done. Consumed %.2f%% -> foodLevel = %.2f%%\n", consumedAmount, currentFoodLevel);
      } else {
        // start next rotation
        myServo.write(180);
        feedPhase = 1;
        feedPhaseStart = now;
      }
    }
  }
}

// Publish nhiệt độ
void publishTemperature() {
  if (!rtcPresent) return;
  float temp = rtc.getTemperature();
  if (temp < -40 || temp > 80) {
     Serial.println("Invalid temperature reading from RTC.");
     return;
  }

  Serial.printf("Publish temp: %.1f\n", temp);
  if (client.connected()) {
    client.publish(TOPIC_STATUS_TEMP, String(temp, 1).c_str(), true);
  } else {
    Serial.println("MQTT not connected - cannot publish temperature");
  }
}

// Cập nhật màn hình OLED
void updateDisplay(String l1, String l2, String l3) {
  if (!oledInitialized) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(l1);

  display.setTextSize(2);
  display.setCursor(0, 16);
  display.println(l2);

  display.setTextSize(1);
  display.setCursor(0, 44);
  display.println(l3);
  display.display();
}

// Xử lý message đến
void callback(char* topic, byte* payload, unsigned int length) {
  // đảm bảo null-terminated
  char msgbuf[512];
  if (length >= sizeof(msgbuf)) length = sizeof(msgbuf) - 1;
  memcpy(msgbuf, payload, length);
  msgbuf[length] = '\0';
  String msg = String(msgbuf);

  Serial.printf("MQTT arrived [%s] -> %s\n", topic, msg.c_str());

  String t = String(topic);

  // Các command xử lý
  if (t == TOPIC_CMD_FEED_NOW) {
    // non-blocking start
    startFeeding();
  }
  else if (t == TOPIC_CMD_SERVO_DURATION) {
    int ms = msg.toInt();
    if (ms < 100) ms = 100;
    if (ms > 2500) ms = 2500;
    servoDuration = ms;
    Serial.printf("Servo duration set: %d\n", servoDuration);
    prefs.putInt("servoDur", servoDuration);
    // publish updated value
    if (client.connected()) client.publish(TOPIC_STATUS_SERVO_DURATION, String(servoDuration).c_str(), true);
    client.publish(TOPIC_STATUS_ACK, "servoduration:OK", false);
  }
  else if (t == TOPIC_CMD_ROTATIONS) {
    int n = msg.toInt();
    if (n < 1) n = 1;
    if (n > 10) n = 10; // allow up to 10, configurable
    rotations = n;
    Serial.printf("Rotations set: %d\n", rotations);
    prefs.putInt("rotations", rotations);
    if (client.connected()) client.publish(TOPIC_STATUS_ROTATIONS, String(rotations).c_str(), true);
    client.publish(TOPIC_STATUS_ACK, "rotations:OK", false);
  }
  else if (t == TOPIC_CMD_AUTO_MODE) {
    String up = msg;
    up.trim();
    up.toUpperCase();
    if (up == "ON" || up == "1" || up == "TRUE") autoMode = true;
    else autoMode = false;
    Serial.printf("Auto mode: %s\n", autoMode ? "ON" : "OFF");
    if (client.connected()) client.publish(TOPIC_STATUS_AUTO_MODE, autoMode ? "ON" : "OFF", true);
    client.publish(TOPIC_STATUS_ACK, "automode:OK", false);
  }
  else if (t == TOPIC_CMD_SCHEDULE_UPDATE) {
    if (msg.startsWith("add:")) {
      String time = msg.substring(4);
      time.trim();
      addSchedule(time);
    } else if (msg.startsWith("remove:")) {
      String time = msg.substring(7);
      time.trim();
      removeSchedule(time);
    }
  }
  else if (t == TOPIC_CMD_REFILL) {
    String m = msg;
    m.trim();
    m.toUpperCase();
    if (m == "1" || m == "REFILL" || m == "YES") {
      currentFoodLevel = 100.0;
      prefs.putFloat("foodLevel", currentFoodLevel);
      client.publish(TOPIC_STATUS_FOOD_LEVEL, String(currentFoodLevel, 1).c_str(), true);
      client.publish(TOPIC_STATUS_ACK, "refill:OK", false);
      Serial.println("Refill command processed: foodLevel = 100%");
    } else {
      client.publish(TOPIC_STATUS_ACK, "refill:INVALID", false);
    }
  }
  else if (t == TOPIC_CMD_GET_STATUS) {
    // web gửi GET -> publish lại tất cả status
    Serial.println("GET_STATUS received -> publishing all statuses");
    publishAllStatuses();
  }
  else if (t == TOPIC_CMD_GET_SCHEDULE) {
    Serial.println("GET_SCHEDULE received -> publishing schedule list");
    publishScheduleList();
  }
  else {
    Serial.println("Unknown topic command");
  }
}

// ========== SCHEDULE (ArduinoJson) ==========
void addSchedule(String time) {
  Serial.println("Schedule add requested: " + time);
  if (!validTimeFormat(time)) {
    Serial.println("Invalid time format, ignore.");
    client.publish(TOPIC_STATUS_ACK, "schedule:INVALID_FORMAT", false);
    return;
  }

  const size_t capacity = 2048;
  DynamicJsonDocument doc(capacity);
  DeserializationError err = deserializeJson(doc, schedulesJson);
  if (err) {
    // nếu parse thất bại, tạo array mới
    doc.to<JsonArray>();
  }
  JsonArray arr = doc.as<JsonArray>();

  // check duplicate
  bool exists = false;
  for (JsonVariant v : arr) {
    if (v.as<String>() == time) {
      exists = true;
      break;
    }
  }

  if(exists) {
    Serial.println("Schedule already exists, skip add");
    client.publish(TOPIC_STATUS_ACK, "schedule:EXISTS", false);
    return;
  }

  arr.add(time);

  // Optional: sort array (simple bubble since small)
  // Copy to vector then sort, reconstruct JSON
  std::vector<String> times;
  for (JsonVariant v : arr) times.push_back(v.as<String>());
  sort(times.begin(), times.end());
  DynamicJsonDocument newDoc(capacity);
  JsonArray newArr = newDoc.to<JsonArray>();
  for (String &s : times) newArr.add(s);

  schedulesJson = "";
  serializeJson(newDoc, schedulesJson);
  Serial.println("New schedulesJson: " + schedulesJson);

  // persist
  prefs.putString("schedules", schedulesJson);

  if (client.connected()) {
    client.publish(TOPIC_STATUS_SCHEDULE_LIST, schedulesJson.c_str(), true);
    client.publish(TOPIC_STATUS_ACK, "schedule:ADDED", false);
  }
}

void removeSchedule(String time) {
  Serial.println("Schedule remove requested: " + time);

  const size_t capacity = 2048;
  DynamicJsonDocument doc(capacity);
  DeserializationError err = deserializeJson(doc, schedulesJson);
  if (err) {
    // nếu parse lỗi, nothing to remove
    client.publish(TOPIC_STATUS_ACK, "schedule:REMOVE_FAILED", false);
    return;
  }
  JsonArray arr = doc.as<JsonArray>();

  DynamicJsonDocument newDoc(capacity);
  JsonArray newArr = newDoc.to<JsonArray>();

  bool removed = false;
  for (JsonVariant v : arr) {
    if (v.as<String>() != time) {
      newArr.add(v.as<String>());
    } else removed = true;
  }

  schedulesJson = "";
  serializeJson(newDoc, schedulesJson);
  Serial.println("Updated schedulesJson: " + schedulesJson);

  // persist
  prefs.putString("schedules", schedulesJson);

  if (client.connected()) {
    client.publish(TOPIC_STATUS_SCHEDULE_LIST, schedulesJson.c_str(), true);
    client.publish(TOPIC_STATUS_ACK, removed ? "schedule:REMOVED" : "schedule:NOT_FOUND", false);
  }
}

// ========== WIFI + MQTT helper ==========
void setup_wifi() {
  delay(10);
  Serial.printf("Connecting to %s\n", WIFI_SSID);
  updateDisplay("Connecting WiFi", WIFI_SSID, "...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
    updateDisplay("WiFi OK", WiFi.localIP().toString(), "");
    delay(1000);
  } else {
    Serial.println("\nWiFi connection failed");
    updateDisplay("WiFi Failed", "Check SSID/PASS", "Rebooting...");
    delay(5000);
    ESP.restart();
  }
}

void reconnect() {
  // dùng LWT: topic, qos=1, retain=true, message "0"
  while (!client.connected()) {
    Serial.print("Attempting MQTT connect...");
    updateDisplay("Connecting MQTT", MQTT_HOST, "...");
    // use connect with will: (id, user, pass, willTopic, willQos, willRetain, willMessage)
    if (client.connect(CLIENT_ID.c_str(), MQTT_USER, MQTT_PASS, TOPIC_STATUS_ONLINE, 1, true, "0")) {
      Serial.println("connected");

      // Subscribe command topics (web -> ESP)
      client.subscribe(TOPIC_CMD_FEED_NOW);
      client.subscribe(TOPIC_CMD_SERVO_DURATION);
      client.subscribe(TOPIC_CMD_ROTATIONS);
      client.subscribe(TOPIC_CMD_AUTO_MODE);
      client.subscribe(TOPIC_CMD_SCHEDULE_UPDATE);
      client.subscribe(TOPIC_CMD_REFILL);

      // Subscribe GET topics so we can respond to web's GET requests
      client.subscribe(TOPIC_CMD_GET_STATUS);
      client.subscribe(TOPIC_CMD_GET_SCHEDULE);

      Serial.println("Subscribed to command & GET topics");
      
      // Publish initial retained statuses so web clients can read immediately
      client.publish(TOPIC_STATUS_ONLINE, "1", true);
      client.publish(TOPIC_STATUS_FEED_COUNT, String(feedCountToday).c_str(), true);
      client.publish(TOPIC_STATUS_SCHEDULE_LIST, schedulesJson.c_str(), true);
      client.publish(TOPIC_STATUS_FOOD_LEVEL, String(currentFoodLevel, 1).c_str(), true);
      client.publish(TOPIC_STATUS_AUTO_MODE, autoMode ? "ON" : "OFF", true);
      client.publish(TOPIC_STATUS_ROTATIONS, String(rotations).c_str(), true);
      client.publish(TOPIC_STATUS_SERVO_DURATION, String(servoDuration).c_str(), true);
      client.publish(TOPIC_STATUS_VERSION, "v1.0.0", true);

      // Publish temperature once on connect (if available)
      if (rtcPresent) {
        float temp = rtc.getTemperature();
        if (!(temp < -40 || temp > 80)) {
          client.publish(TOPIC_STATUS_TEMP, String(temp, 1).c_str(), true);
        }
      }

      updateDisplay("MQTT OK", "System Ready", "");
    } else {
      Serial.print("failed, rc="); Serial.print(client.state());
      Serial.println(" try again in 5s");
      updateDisplay("MQTT Failed", "Retrying...", "");
      delay(5000);
    }
  }
}

// ========== UTIL ==========
bool validTimeFormat(const String &t) {
  if (t.length() != 5) return false;
  if (t.charAt(2) != ':') return false;
  String sh = t.substring(0,2);
  String sm = t.substring(3,5);
  if (!(isDigit(sh.charAt(0)) && isDigit(sh.charAt(1)) && isDigit(sm.charAt(0)) && isDigit(sm.charAt(1)))) return false;
  int hh = sh.toInt();
  int mm = sm.toInt();
  return (hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59);
}

String isoNow() {
  if (!rtcPresent) return String("");
  DateTime t = rtc.now();
  char buf[25];
  sprintf(buf, "%04d-%02d-%02dT%02d:%02d:%02d", t.year(), t.month(), t.day(), t.hour(), t.minute(), t.second());
  return String(buf);
}
