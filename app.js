// =============================
// MQTT Web Dashboard (Paho)
// =============================

// === THAY THẾ NẾU CẦN ===
const MQTT_HOST = "f56d445d19d541e58e19a6a7a3972565.s1.eu.hivemq.cloud";
const MQTT_PORT = 8884;
const MQTT_USER = "admin";
const MQTT_PASS = "sang123A@";
const CLIENT_ID = "webClient_" + Math.floor(Math.random() * 10000);

// === TOPICS (phải trùng với ESP) ===
const TOPIC_STATUS_TEMP = "fishfeeder/status/temperature";
// const TOPIC_STATUS_FOOD_LEVEL = "fishfeeder/status/foodlevel"; // <<< KHÔNG CẦN NỮA
const TOPIC_STATUS_FEED_COUNT = "fishfeeder/status/feedcount";
const TOPIC_STATUS_SCHEDULE_LIST = "fishfeeder/status/schedulelist";

const TOPIC_CMD_FEED_NOW = "fishfeeder/command/feednow";
const TOPIC_CMD_AUTO_MODE = "fishfeeder/command/automode";
const TOPIC_CMD_SERVO_DURATION = "fishfeeder/command/servoduration";
const TOPIC_CMD_ROTATIONS = "fishfeeder/command/rotations";
const TOPIC_CMD_SCHEDULE_UPDATE = "fishfeeder/command/scheduleupdate";

// === DOM ===
const connectionStatusEl = document.getElementById("connection-status");
const temperatureEl = document.getElementById("temperature");
const foodLevelEl = document.getElementById("food-level");
const feedCountEl = document.getElementById("feed-count");
const feedNowBtn = document.getElementById("feed-now-btn");
const autoModeToggle = document.getElementById("auto-mode-toggle");
const feedAmountSlider = document.getElementById("feed-amount-slider");
const sliderValueEl = document.getElementById("slider-value");
const rotationsInput = document.getElementById("feed-rotations-input");
const addScheduleBtn = document.getElementById("add-schedule-btn");
const newScheduleTimeInput = document.getElementById("new-schedule-time");
const scheduleListEl = document.getElementById("schedule-list");
const refillBtn = document.getElementById("refill-btn"); // <<< MỚI: Nút làm đầy

// Thêm topic GET (request)
const TOPIC_CMD_GET_STATUS = "fishfeeder/command/getstatus";
const TOPIC_CMD_GET_SCHEDULE = "fishfeeder/command/getschedule";

// Các flag trạng thái chờ nhận dữ liệu (mở đầu)
let tempReceived = false;
let feedCountReceived = false;
let scheduleReceived = false;

// === thêm topic status cho các tham số khác ===
const TOPIC_STATUS_AUTO_MODE = "fishfeeder/status/automode";
const TOPIC_STATUS_ROTATIONS = "fishfeeder/status/rotations";
const TOPIC_STATUS_SERVO_DURATION = "fishfeeder/status/servoduration";
const TOPIC_STATUS_FOOD_LEVEL = "fishfeeder/status/foodlevel"; // nếu ESP publish food level

// (flag nhận dữ liệu)
let autoModeReceived = false;
let rotationsReceived = false;
let servoDurationReceived = false;
let foodLevelReceived = false;

// === State mô phỏng mức thức ăn === // <<< LOGIC MỚI
let currentFoodLevel = 100.0; // Bắt đầu ở 100%
const FOOD_CONSUMPTION_BASE = 2.5; // Lượng thức ăn tiêu thụ cơ bản cho 1 lần xoay ở mức 1000ms (tính bằng %)

// === MQTT Client (Paho over WebSockets) ===
const client = new Paho.MQTT.Client(MQTT_HOST, Number(MQTT_PORT), CLIENT_ID);

client.onConnectionLost = onConnectionLost;
client.onMessageArrived = onMessageArrived;

const connectOptions = {
  useSSL: true,
  userName: MQTT_USER,
  password: MQTT_PASS,
  onSuccess: onConnect,
  onFailure: onFailure,
  cleanSession: true,
  timeout: 10,
};

// Connect
console.log("Attempting to connect to MQTT broker via WebSocket...");
client.connect(connectOptions);

// === Chart.js init ===
const MAX_POINTS = 24;
const ctx = document.getElementById("tempChart").getContext("2d");
const tempChart = new Chart(ctx, {
  type: "line",
  data: {
    labels: [],
    datasets: [
      {
        label: "Nhiệt độ (°C)",
        data: [],
        borderColor: "rgba(0, 123, 255, 1)",
        backgroundColor: "rgba(0, 123, 255, 0.08)",
        borderWidth: 2,
        fill: true,
        tension: 0.3,
        pointRadius: 2,
        pointHoverRadius: 4,
      },
    ],
  },
  options: {
    responsive: true,
    maintainAspectRatio: false,
    animation: { duration: 300 },
    scales: {
      x: { display: true, ticks: { autoSkip: true, maxTicksLimit: 8 } },
      y: { beginAtZero: false },
    },
    plugins: { legend: { display: true, position: "top" } },
  },
});

// === Helpers mới: request status / schedule with small delay + UI hint ===
function showTemporaryStatus(el, text, timeout = 2000) {
  if (!el) return;
  const prev = el.textContent;
  const prevOpacity = el.style.opacity;
  el.textContent = text;
  el.style.opacity = 0.7;
  setTimeout(() => {
    el.textContent = prev;
    el.style.opacity = prevOpacity || 1;
  }, timeout);
}

function requestStatus(delay = 300) {
  // Gọi GET để ESP publish lại các topic status
  setTimeout(() => {
    publishMessage(TOPIC_CMD_GET_STATUS, "GET");
  }, delay);
}

function requestSchedule(delay = 300) {
  setTimeout(() => {
    publishMessage(TOPIC_CMD_GET_SCHEDULE, "GET");
  }, delay);
}

// Debounce helper để tránh spam GET requests (ví dụ nhiều thay đổi nhanh)
let lastGetTimestamp = 0;
function debouncedRequestStatus(minInterval = 400) {
  const now = Date.now();
  if (now - lastGetTimestamp > minInterval) {
    requestStatus(200);
    lastGetTimestamp = now;
  } else {
    // schedule later
    setTimeout(
      () => requestStatus(200),
      minInterval - (now - lastGetTimestamp)
    );
    lastGetTimestamp = now + (minInterval - (now - lastGetTimestamp));
  }
}

// === MQTT Callbacks ===
function onConnect() {
  console.log("✅ Web MQTT connected");
  if (connectionStatusEl) {
    connectionStatusEl.textContent = "Đã kết nối";
    connectionStatusEl.style.backgroundColor = "rgba(40,167,69,0.15)";
  }

  // Subscribe tất cả status topics (QoS 1)
  client.subscribe(TOPIC_STATUS_TEMP, { qos: 1 });
  client.subscribe(TOPIC_STATUS_FEED_COUNT, { qos: 1 });
  client.subscribe(TOPIC_STATUS_SCHEDULE_LIST, { qos: 1 });

  // Subscribe các status bổ sung
  client.subscribe(TOPIC_STATUS_AUTO_MODE, { qos: 1 });
  client.subscribe(TOPIC_STATUS_ROTATIONS, { qos: 1 });
  client.subscribe(TOPIC_STATUS_SERVO_DURATION, { qos: 1 });
  client.subscribe(TOPIC_STATUS_FOOD_LEVEL, { qos: 1 });

  console.log("Subscribed to status topics (requesting retained if any)");

  // Reset flags & UI chờ
  tempReceived = feedCountReceived = scheduleReceived = false;
  autoModeReceived =
    rotationsReceived =
    servoDurationReceived =
    foodLevelReceived =
      false;

  // UI loading placeholders
  if (temperatureEl) {
    temperatureEl.textContent = "—";
    temperatureEl.style.opacity = 1;
  }
  if (feedCountEl) {
    feedCountEl.textContent = "—";
    feedCountEl.style.opacity = 1;
  }
  if (scheduleListEl) scheduleListEl.innerHTML = "<li>Đang tải...</li>";
  // các UI mới:
  if (sliderValueEl && feedAmountSlider)
    sliderValueEl.textContent = `${feedAmountSlider.value}ms`;
  // auto toggle không có text nhưng ta có thể làm opacity tạm
  if (autoModeToggle) autoModeToggle.checked = false;
  if (rotationsInput) rotationsInput.value = rotationsInput.value || 1;
  if (foodLevelEl) {
    foodLevelEl.textContent = "—";
    foodLevelEl.style.opacity = 1;
  }
  flushQueueOnConnect();
  // Gửi GET request để ESP trả lại trạng thái (nếu ESP hỗ trợ)
  // Gọi debounced để tránh spam nhiều lần (nêú onConnect được gọi lại)
  debouncedRequestStatus();
  requestSchedule(300);

  // Timeout cảnh báo nếu không nhận dữ liệu
  setTimeout(() => {
    if (!tempReceived && temperatureEl) {
      temperatureEl.textContent = "Không có dữ liệu";
      temperatureEl.style.opacity = 0.7;
    }
    if (!feedCountReceived && feedCountEl) {
      feedCountEl.textContent = "Không có dữ liệu";
      feedCountEl.style.opacity = 0.7;
    }
    if (!scheduleReceived && scheduleListEl) {
      scheduleListEl.innerHTML = "<li>Không có dữ liệu lịch</li>";
    }
    if (!autoModeReceived && autoModeToggle) {
      autoModeToggle.style.opacity = 0.6;
    }
    if (!rotationsReceived && rotationsInput) {
      rotationsInput.style.opacity = 0.6;
    }
    if (!servoDurationReceived && sliderValueEl) {
      sliderValueEl.textContent = `${
        feedAmountSlider ? feedAmountSlider.value : "—"
      }ms (chưa xác nhận)`;
      sliderValueEl.style.opacity = 0.7;
    }
    if (!foodLevelReceived && foodLevelEl) {
      foodLevelEl.textContent = "Không có dữ liệu";
      foodLevelEl.style.opacity = 0.7;
    }
  }, 4000);
}

function onFailure(err) {
  console.error("❌ Web MQTT connect failed:", err);
  if (connectionStatusEl) {
    connectionStatusEl.textContent = "Kết nối thất bại!";
    connectionStatusEl.style.backgroundColor = "rgba(220,53,69,0.12)";
  }
}

function onConnectionLost(resp) {
  console.warn("🔌 Web MQTT connection lost", resp);
  if (connectionStatusEl) {
    connectionStatusEl.textContent = "Mất kết nối";
    connectionStatusEl.style.backgroundColor = "rgba(255,193,7,0.12)";
  }
}

function onMessageArrived(message) {
  const topic = message.destinationName;
  const payload = message.payloadString;
  console.log("📥", topic, payload);

  switch (topic) {
    case TOPIC_STATUS_TEMP: {
      const tempNum = parseFloat(payload);
      if (!isNaN(tempNum) && temperatureEl) {
        temperatureEl.textContent = tempNum.toFixed(1);
        addDataToChart(new Date().toLocaleTimeString(), tempNum);
        tempReceived = true;
        temperatureEl.style.opacity = 1;
      }
      break;
    }

    case TOPIC_STATUS_FEED_COUNT: {
      if (feedCountEl) {
        feedCountEl.textContent = payload;
        feedCountReceived = true;
        feedCountEl.style.opacity = 1;
      }
      // Khi ESP báo đã cho ăn, chúng ta sẽ trừ thức ăn
      consumeFood();
      break;
    }

    // --- các status mới ---
    case TOPIC_STATUS_AUTO_MODE: {
      // payload có thể là "ON"/"OFF" hoặc "1"/"0"
      const p = String(payload).trim().toUpperCase();
      if (p === "ON" || p === "1" || p === "TRUE") {
        if (autoModeToggle) {
          autoModeToggle.checked = true;
          autoModeToggle.style.opacity = 1;
        }
      } else {
        if (autoModeToggle) {
          autoModeToggle.checked = false;
          autoModeToggle.style.opacity = 1;
        }
      }
      autoModeReceived = true;
      break;
    }

    case TOPIC_STATUS_ROTATIONS: {
      const n = parseInt(payload);
      if (!isNaN(n) && rotationsInput) {
        rotationsInput.value = n;
        rotationsReceived = true;
        rotationsInput.style.opacity = 1;
      }
      break;
    }

    case TOPIC_STATUS_SERVO_DURATION: {
      const ms = parseInt(payload);
      if (!isNaN(ms) && feedAmountSlider && sliderValueEl) {
        // update slider and text
        feedAmountSlider.value = ms;
        sliderValueEl.textContent = `${ms}ms`;
        servoDurationReceived = true;
        sliderValueEl.style.opacity = 1;
      }
      break;
    }

    case TOPIC_STATUS_FOOD_LEVEL: {
      // payload mong đợi là số phần trăm, ví dụ "75.4"
      const lvl = parseFloat(payload);
      if (!isNaN(lvl)) {
        currentFoodLevel = Math.max(0, Math.min(100, lvl));
        updateFoodLevelUI();
        foodLevelReceived = true;
      } else {
        // Nếu payload là text như "FULL"/"LOW", show trực tiếp
        if (foodLevelEl) {
          foodLevelEl.textContent = payload;
          foodLevelReceived = true;
        }
      }
      break;
    }

    case TOPIC_STATUS_SCHEDULE_LIST:
      try {
        const arr = JSON.parse(payload);
        updateScheduleListUI(arr);
        scheduleReceived = true;
      } catch (e) {
        console.error("Failed to parse schedule list:", e);
      }
      break;

    default:
      console.log("Không xử lý topic:", topic);
  }
}

// === UI events -> publish ===
// === Publish queue & publish-status element (paste near top of file) ===
const messageQueue = []; // { topic, payload }

function ensurePublishStatusEl() {
  let el = document.getElementById("publish-status");
  if (!el) {
    el = document.createElement("div");
    el.id = "publish-status";
    // minimal styling so it doesn't clash with connectionStatusEl
    el.style.position = "fixed";
    el.style.right = "12px";
    el.style.bottom = "12px";
    el.style.padding = "6px 10px";
    el.style.borderRadius = "6px";
    el.style.background = "rgba(0,0,0,0.6)";
    el.style.color = "#fff";
    el.style.fontSize = "13px";
    el.style.zIndex = 9999;
    el.style.display = "none";
    document.body.appendChild(el);
  }
  return el;
}

function showPublishMessage(text, ms = 1500) {
  const el = ensurePublishStatusEl();
  el.textContent = text;
  el.style.display = "block";
  setTimeout(() => {
    el.style.display = "none";
  }, ms);
}

// Call this from onConnect so queued messages are flushed
function flushQueueOnConnect() {
  if (!client || !client.isConnected || !client.isConnected()) return;
  while (messageQueue.length > 0) {
    const item = messageQueue.shift();
    const msg = new Paho.MQTT.Message(String(item.payload));
    msg.destinationName = item.topic;
    try {
      client.send(msg);
      console.log("📤 Flushed queued", item.topic, item.payload);
    } catch (e) {
      console.error("Failed to flush queued message:", e);
      // put it back and stop trying now
      messageQueue.unshift(item);
      break;
    }
  }
}

// === REPLACE publishMessage with this improved version ===
function publishMessage(topic, payload, { queueIfDisconnected = true } = {}) {
  try {
    if (client && client.isConnected && client.isConnected()) {
      const msg = new Paho.MQTT.Message(String(payload));
      msg.destinationName = topic;
      client.send(msg);
      console.log("📤 Sent", topic, payload);
      return true;
    } else {
      // don't overwrite connectionStatusEl; use a separate publish-status
      console.warn("MQTT not connected — cannot publish now:", topic, payload);
      if (queueIfDisconnected) {
        // push to queue for retry
        messageQueue.push({ topic, payload });
        showPublishMessage("Tạm thời chưa gửi — sẽ thử lại", 1200);
      } else {
        showPublishMessage("Không gửi được (chưa kết nối)", 1200);
      }
      return false;
    }
  } catch (err) {
    console.error("publishMessage error:", err);
    showPublishMessage("Lỗi khi gửi lệnh", 1200);
    return false;
  }
}

// <<< MỚI: Hàm tính toán lượng thức ăn tiêu thụ
function consumeFood() {
  const rotations = parseInt(rotationsInput ? rotationsInput.value : 1) || 1;
  const duration =
    parseInt(feedAmountSlider ? feedAmountSlider.value : 1000) || 1000;

  // Lượng tiêu thụ tỉ lệ với số lần xoay và thời gian xoay
  // (duration / 1000) là hệ số điều chỉnh, 1000ms là mức chuẩn (1x)
  const consumedAmount = rotations * FOOD_CONSUMPTION_BASE * (duration / 1000);

  currentFoodLevel -= consumedAmount;

  // Đảm bảo không âm
  if (currentFoodLevel < 0) {
    currentFoodLevel = 0;
  }

  console.log(
    `Consumed: ${consumedAmount.toFixed(
      2
    )}%. New food level: ${currentFoodLevel.toFixed(2)}%`
  );
  updateFoodLevelUI();
}

// <<< MỚI: Hàm cập nhật giao diện mức thức ăn
function updateFoodLevelUI() {
  if (!foodLevelEl) return;
  foodLevelEl.textContent = `${currentFoodLevel.toFixed(1)}%`;

  // Đổi màu nếu sắp hết
  if (currentFoodLevel < 20) {
    foodLevelEl.className = "food-low";
  } else {
    foodLevelEl.className = "food-ok";
  }
  foodLevelEl.style.opacity = 1;
}

// === Event listeners with refresh behaviour ===
if (feedNowBtn) {
  feedNowBtn.addEventListener("click", () => {
    publishMessage(TOPIC_CMD_FEED_NOW, "1");
    showTemporaryStatus(feedCountEl, "Đang kích hoạt...", 1200);
    // sau khi ESP thực hiện việc cho ăn, nó sẽ publish feed_count; nhưng ta vẫn yêu cầu trạng thái sau 600ms
    debouncedRequestStatus(600);
  });
}

if (autoModeToggle) {
  autoModeToggle.addEventListener("change", (ev) => {
    const payload = ev.target.checked ? "ON" : "OFF";
    publishMessage(TOPIC_CMD_AUTO_MODE, payload);
    // feedback nhỏ cho user
    showTemporaryStatus(connectionStatusEl, "Cập nhật chế độ tự động...", 900);
    debouncedRequestStatus(500);
  });
}

if (feedAmountSlider) {
  feedAmountSlider.addEventListener("input", () => {
    if (sliderValueEl)
      sliderValueEl.textContent = `${feedAmountSlider.value}ms`;
  });
  feedAmountSlider.addEventListener("change", () => {
    publishMessage(TOPIC_CMD_SERVO_DURATION, feedAmountSlider.value);
    // UI hint: chưa xác nhận
    if (sliderValueEl) {
      sliderValueEl.textContent = `${feedAmountSlider.value}ms (đang cập nhật...)`;
      sliderValueEl.style.opacity = 0.7;
    }
    debouncedRequestStatus(500);
  });
}

if (rotationsInput) {
  rotationsInput.addEventListener("change", () => {
    publishMessage(TOPIC_CMD_ROTATIONS, rotationsInput.value);
    // UI feedback
    rotationsInput.style.opacity = 0.6;
    setTimeout(() => (rotationsInput.style.opacity = 1), 800);
    debouncedRequestStatus(500);
  });
}

if (addScheduleBtn) {
  addScheduleBtn.addEventListener("click", () => {
    const time = newScheduleTimeInput ? newScheduleTimeInput.value : "";
    if (!time) return;
    publishMessage(TOPIC_CMD_SCHEDULE_UPDATE, `add:${time}`);
    // optimistic UI: show updating text
    if (scheduleListEl)
      scheduleListEl.innerHTML = "<li>Đang cập nhật lịch...</li>";
    if (newScheduleTimeInput) newScheduleTimeInput.value = "";
    // request schedule from ESP after small delay
    requestSchedule(500);
  });
}

// Schedule list delete (event delegation)
if (scheduleListEl) {
  scheduleListEl.addEventListener("click", (ev) => {
    if (ev.target && ev.target.matches("button.delete-btn")) {
      const time = ev.target.dataset.time;
      if (!time) return;
      publishMessage(TOPIC_CMD_SCHEDULE_UPDATE, `remove:${time}`);
      // show updating placeholder
      scheduleListEl.innerHTML = "<li>Đang cập nhật lịch...</li>";
      requestSchedule(400);
    }
  });
}

// <<< MỚI: Event listener cho nút làm đầy
if (refillBtn) {
  refillBtn.addEventListener("click", () => {
    console.log("Refilling food (local)...");
    currentFoodLevel = 100.0;
    updateFoodLevelUI();
    // UI hint
    showTemporaryStatus(foodLevelEl, "Đã làm đầy (chờ xác nhận)...", 1500);
    // Nếu ESP có topic refill, publish tại đây (ví dụ: "fishfeeder/command/refill")
    publishMessage("fishfeeder/command/refill", "1");
    // Yêu cầu ESP gửi lại status để đồng bộ
    debouncedRequestStatus(500);
  });
}

// === Helpers UI / Chart / Schedule ===
function addDataToChart(label, data) {
  const value = Number(data);
  if (isNaN(value)) return;
  tempChart.data.labels.push(label);
  tempChart.data.datasets[0].data.push(value);
  while (tempChart.data.labels.length > MAX_POINTS) {
    tempChart.data.labels.shift();
    tempChart.data.datasets[0].data.shift();
  }
  tempChart.update();
}

function updateScheduleListUI(schedules) {
  if (!scheduleListEl) return;
  scheduleListEl.innerHTML = "";
  if (!schedules || schedules.length === 0) {
    scheduleListEl.innerHTML = "<li>Chưa có lịch nào.</li>";
    return;
  }
  schedules.forEach((t) => {
    const li = document.createElement("li");
    const btn = document.createElement("button");
    btn.className = "delete-btn";
    btn.dataset.time = t;
    btn.textContent = "Xóa";
    li.textContent = t + " ";
    li.appendChild(btn);
    scheduleListEl.appendChild(li);
  });
}

// <<< MỚI: Khởi tạo hiển thị mức thức ăn khi tải trang
document.addEventListener("DOMContentLoaded", () => {
  updateFoodLevelUI();

  // set initial UI placeholders if elements exist
  if (sliderValueEl && feedAmountSlider)
    sliderValueEl.textContent = `${feedAmountSlider.value}ms`;
  if (rotationsInput && !rotationsInput.value) rotationsInput.value = 1;

  // optionally request status/schedule on page load
  debouncedRequestStatus(400);
  requestSchedule(500);
});
