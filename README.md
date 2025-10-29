# 🐠 FishFeeder Extended – Hệ thống cho cá ăn tự động ESP32 + Web Dashboard

Dự án gồm **2 phần chính**:
1. 🧩 **Firmware ESP32 (FishFeeder Extended)** – điều khiển phần cứng cho cá ăn tự động.  
2. 🌐 **MQTT Web Dashboard** – giám sát và điều khiển thiết bị từ trình duyệt qua MQTT WebSocket.

---

# ⚙️ PHẦN 1: NẠP CODE CHO ESP32

## 📘 Giới thiệu
**FishFeeder Extended** là một dự án IoT dùng **ESP32** để điều khiển việc cho cá ăn tự động theo lịch hoặc điều khiển thủ công thông qua **MQTT (HiveMQ Cloud)**.  
Thiết bị được tích hợp các thành phần:
- Mô-tơ Servo điều khiển việc rải thức ăn
- Màn hình OLED SSD1306 hiển thị trạng thái
- Module thời gian thực (RTC DS3231)
- Lưu cấu hình bằng bộ nhớ **Preferences**
- Kết nối **Wi-Fi + MQTT (TLS)** an toàn

Dự án có thể hoạt động độc lập hoặc kết nối Web Dashboard thông qua MQTT.

---

## 🧩 Cấu hình phần cứng
| Thành phần | Kết nối |
|-------------|----------|
| **ESP32** | Vi điều khiển chính |
| **Servo Motor** | Pin tín hiệu → GPIO **13** |
| **OLED SSD1306 (I2C)** | SDA → GPIO 21, SCL → GPIO 22 |
| **RTC DS3231 (I2C)** | Cùng bus I2C với OLED |
| **Nguồn cấp** | 5V cho servo và ESP32 |

---

## 🌐 Cấu hình Wi-Fi & MQTT
Trong code Arduino:

```cpp
// Wi-Fi
const char* WIFI_SSID = "YourWiFiName";
const char* WIFI_PASSWORD = "YourWiFiPassword";

// MQTT (HiveMQ Cloud)
const char* MQTT_HOST = "xxxxx.s1.eu.hivemq.cloud";
const uint16_t MQTT_PORT = 8883;
const char* MQTT_USER = "iot_device";
const char* MQTT_PASS = "your_password";
```

---

## 🪶 Cấu trúc MQTT Topics

### Status (ESP → Web)
| Topic | Nội dung |
|-------|-----------|
| `fishfeeder/status/temperature` | Nhiệt độ hiện tại |
| `fishfeeder/status/feedcount` | Số lần cho ăn trong ngày |
| `fishfeeder/status/foodlevel` | Mức thức ăn còn lại (%) |
| `fishfeeder/status/schedulelist` | Danh sách lịch cho ăn |
| `fishfeeder/status/automode` | `ON` / `OFF` |
| `fishfeeder/status/rotations` | Số vòng servo |
| `fishfeeder/status/servoduration` | Thời gian servo (ms) |
| `fishfeeder/status/lastfeedtime` | Thời gian cho ăn gần nhất |
| `fishfeeder/status/ack` | Phản hồi cho lệnh |

### Command (Web → ESP)
| Topic | Payload | Mô tả |
|-------|----------|-------|
| `fishfeeder/command/feednow` | _(bất kỳ)_ | Cho ăn ngay |
| `fishfeeder/command/automode` | `ON` / `OFF` | Bật/tắt chế độ tự động |
| `fishfeeder/command/servoduration` | `100`–`2500` | Thời gian quay servo (ms) |
| `fishfeeder/command/rotations` | `1`–`10` | Số vòng quay servo |
| `fishfeeder/command/scheduleupdate` | `add:HH:MM` / `remove:HH:MM` | Quản lý lịch cho ăn |
| `fishfeeder/command/refill` | `REFILL` | Nạp lại thức ăn |

---

## 🧰 Yêu cầu thư viện Arduino
- `WiFi`, `WiFiClientSecure`
- `PubSubClient`
- `ESP32Servo`
- `RTClib`
- `Adafruit GFX Library`, `Adafruit SSD1306`
- `ArduinoJson`
- `Preferences`

---

# 🌐 PHẦN 2: CHẠY WEB DASHBOARD

## 🐟 MQTT Web Dashboard - Máy Cho Cá Ăn Tự Động

Dự án này là một **bảng điều khiển web** để giám sát và điều khiển **máy cho cá ăn tự động**, sử dụng **MQTT qua WebSocket**.

---

## 🚀 Tính năng chính
- Kết nối MQTT bảo mật (SSL) qua HiveMQ Cloud.  
- Hiển thị **nhiệt độ**, **mức thức ăn**, **số lần cho ăn**.  
- Gửi lệnh điều khiển cho ESP32/ESP8266:
  - Cho ăn ngay.
  - Bật/tắt chế độ Auto.
  - Điều chỉnh servo.
  - Cập nhật số vòng quay.
  - Quản lý lịch cho ăn.
- Hiển thị biểu đồ nhiệt độ (Chart.js).
- Cảnh báo khi thức ăn sắp hết.
- Giao diện đăng nhập cơ bản.

---

## 🧠 Cấu trúc thư mục
```
📂 Project Root
├── index.html        # Trang Dashboard chính
├── login.html        # Trang đăng nhập
├── app.js            # Logic MQTT và UI
├── style.css         # Giao diện
```

---

## ⚙️ Cấu hình MQTT

Trong `app.js`:

```js
const MQTT_HOST = "f56d445d19d541e58e19a6a7a3972565.s1.eu.hivemq.cloud";
const MQTT_PORT = 8884;
const MQTT_USER = "admin";
const MQTT_PASS = "sang123A@";
```

> 🔒 **Lưu ý:** Nếu triển khai online, hãy đổi mật khẩu và ẩn thông tin trong `.env`.

---

## 🧩 Các topic MQTT

| Loại | Topic | Chức năng |
|------|--------|-----------|
| Status | `fishfeeder/status/temperature` | Nhiệt độ |
| Status | `fishfeeder/status/feedcount` | Số lần cho ăn |
| Status | `fishfeeder/status/schedulelist` | Danh sách lịch |
| Status | `fishfeeder/status/foodlevel` | Mức thức ăn |
| Command | `fishfeeder/command/feednow` | Cho ăn ngay |
| Command | `fishfeeder/command/automode` | Bật/tắt Auto |
| Command | `fishfeeder/command/servoduration` | Thời gian servo |
| Command | `fishfeeder/command/rotations` | Vòng quay |
| Command | `fishfeeder/command/scheduleupdate` | Cập nhật lịch |

---

## 🖥️ Cách chạy project

1. Mở `login.html` trong trình duyệt.  
2. Đăng nhập với:
   ```
   Username: admin
   Password: sang123A@
   ```
3. Tự động chuyển đến `index.html`.  
4. Dashboard sẽ kết nối đến MQTT broker và hiển thị dữ liệu từ ESP32.

---

## 📊 Thư viện sử dụng
- [Paho MQTT JS](https://www.eclipse.org/paho/)
- [Chart.js](https://www.chartjs.org/)
- Google Fonts (Roboto)

---

## 👨‍💻 Tác giả
**Phạm Sang**  
📧 Email: gnasai1218@gmail.com  
📅 Cập nhật: 2025-10-29  
🔖 License: MIT
