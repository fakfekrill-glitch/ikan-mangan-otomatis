#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Servo.h>
#include <LiquidCrystal_I2C.h>
#include <time.h>
#include <EEPROM.h>
#include <ArduinoOTA.h>

// ==================== KONFIGURASI DEFAULT ====================
const char* DEFAULT_SSID = "josjis";
const char* DEFAULT_PASS = "rusakkabeh";
const char* DEFAULT_WEBHOOK = "https://discord.com/api/webhooks/1476830362012221565/v-gVjPU5pAmts2zDN1iUs-p1Zg_pQkjWRf4QNvgTSIwbE1xZjO0wyeFbrnWygfui4O9W";
const int MAX_SCHEDULES = 5;

// ==================== PIN & HARDWARE ====================
const int servoPin = 2; // D4
Servo feederServo;
LiquidCrystal_I2C lcd(0x27, 16, 2);
ESP8266WebServer server(80);

// ==================== STRUKTUR DATA ====================
struct Schedule {
  uint8_t hour;
  uint8_t minute;
  uint8_t portion;
  bool enabled;
};

struct Settings {
  Schedule schedules[MAX_SCHEDULES];
  bool autoFeedEnabled;
  uint8_t servoAngle;
  uint16_t durasiBuka;
  uint8_t jedaPorsi;
  uint8_t porsiAutoDefault;
  int16_t sisaMakanan;
  bool discordNotify;
  char webhook[200]; 
  uint8_t notifyEvents;
  int8_t timezone;
  char ntpServer[32];
  uint32_t magic;
} settings;

// ==================== VARIABEL GLOBAL ====================
int feedsToday = 0;
int lastDay = -1;
int lastFedMinute = -1;
unsigned long lastLcdUpdate = 0;
bool timeSynced = false;
unsigned long lastWiFiCheck = 0;
unsigned long lastNTPCheck = 0;

// ==================== ALAMAT EEPROM ====================
#define EEPROM_SIZE 512
#define EEPROM_MAGIC 0xFEED1237 
#define EEPROM_ADDR 0

// Alamat memori khusus (di luar Settings) untuk mencatat status pintu
#define EEPROM_DOOR_STATE_ADDR 500 

// ==================== FORWARD DECLARATION FUNGSI ====================
void setupWiFi();
bool ensureWiFi();
bool ensureNTP();
void loadSettings();
void saveSettings();
void resetSettingsToDefault();
void updateLCD(struct tm* p_tm);
String getNextFeedTime();
void beriMakanIkan(String metode, String deviceInfo, int porsi);
void kirimNotifDiscord(String title, String metode, String deviceInfo, int color, int porsi, bool isWarning = false);
void kirimNotifStartup(bool calibrated);
void kirimRingkasanHarian();
void handleRoot();
void handleStatusJSON();
void handleManualFeed();
void handleTestJeda();
void handleManualCall();
void handleRefill();
void handleSetSisa();
void handleSaveSettings();
void handleResetDaily();
void handleResetDefaults();
void handleReboot();
void handleCalibrate();
void handleTestWebhook();
String getUptime();
String getWiFiGrade();

// ==================== PROGMEM HTML HEADER ====================
const char PAGE_HEAD[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>
<title>Smart Feeder Pro</title><link href='https://fonts.googleapis.com/css2?family=Poppins:wght@400;600&display=swap' rel='stylesheet'>
<style>
  body { font-family: 'Poppins', sans-serif; background: #0f172a; color: white; margin: 0; padding: 15px; display: flex; align-items: center; justify-content: center; min-height: 100vh;}
  .card { background: rgba(30, 41, 59, 0.7); backdrop-filter: blur(15px); padding: 25px; border-radius: 20px; border: 1px solid rgba(255,255,255,0.1); width: 100%; max-width: 400px; text-align: center; box-shadow: 0 10px 30px rgba(0,0,0,0.5);}
  .live-clock { font-size: 36px; font-weight: 600; color: #38bdf8; margin-bottom: 10px; }
  .info-box { font-size: 13px; color: #cbd5e1; margin-bottom: 15px; background: #1e293b; padding: 15px; border-radius: 12px; line-height: 1.6; text-align: left; border: 1px solid #334155;}
  .btn-main { background: linear-gradient(135deg, #f97316 0%, #ea580c 100%); color: white; border-radius: 10px; font-weight: 600; font-size: 14px; border: none; cursor: pointer; box-shadow: 0 5px 15px rgba(234, 88, 12, 0.3); transition: 0.2s; padding: 10px 20px;}
  .btn-main:active { transform: scale(0.95); }
  .btn-call { background: linear-gradient(135deg, #3b82f6 0%, #2563eb 100%); color: white; padding: 12px; border-radius: 10px; font-weight: 600; font-size: 14px; border: none; width: 100%; cursor: pointer; box-shadow: 0 5px 15px rgba(37, 99, 235, 0.3); margin-bottom: 10px; transition: 0.2s;}
  .btn-test { background: linear-gradient(135deg, #8b5cf6 0%, #6d28d9 100%); color: white; padding: 12px; border-radius: 10px; font-weight: 600; font-size: 14px; border: none; width: 100%; cursor: pointer; box-shadow: 0 5px 15px rgba(109, 40, 217, 0.3); margin-bottom: 10px; transition: 0.2s;}
  .btn-refill { background: #10b981; color: white; padding: 10px; border-radius: 8px; font-size: 12px; border: none; width: 100%; cursor: pointer; margin-bottom: 15px; transition: 0.2s;}
  .btn-save { background: #4ecca3; color: #0f172a; padding: 12px; border: none; border-radius: 10px; font-weight: 600; cursor: pointer; width:100%; margin-top:15px; transition: 0.2s;}
  .btn-reset { background: #ef4444; color: white; padding: 8px; border-radius: 6px; font-size: 12px; border: none; cursor: pointer; margin-top: 10px; width: 100%; transition: 0.2s;}
  input[type='number'], input[type='text'], input[type='time'] { background: #0f172a; border: 1px solid #475569; color: white; padding: 6px; border-radius: 6px; text-align: center; width: 50px; font-size: 14px; }
  input[type='text'] { width: 100%; box-sizing: border-box; margin-bottom: 10px; }
  select { background: #0f172a; color: white; border: 1px solid #475569; padding: 8px; border-radius: 6px; font-size: 13px; outline: none; }
  .row { display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px; font-size: 13px; }
  .schedule-row { display: flex; justify-content: space-between; align-items: center; background: #0f172a; padding: 8px 10px; border-radius: 8px; margin-bottom: 8px; border: 1px solid #1e293b; }
  .schedule-row input[type='time'] { width: 90px; }
  .schedule-row input[type='number'] { width: 45px; }
  .schedule-row label { display: flex; align-items: center; gap: 5px; font-size: 13px; cursor: pointer; }
  .action-group { display: flex; gap: 10px; margin-bottom: 10px; }
  hr { border: 1px solid #334155; margin: 20px 0; }
  .warning { color: #f97316; }
  .good { color: #4ecca3; }
</style>
<script>
  let srvTime = new Date();
  let ticker = null;

  function updateStatus() {
    fetch('/status')
      .then(response => response.json())
      .then(data => {
        let pts = data.time.split(':');
        srvTime.setHours(pts[0], pts[1], pts[2]);
        
        if(!ticker) {
          ticker = setInterval(function(){
            srvTime.setSeconds(srvTime.getSeconds() + 1);
            let h = String(srvTime.getHours()).padStart(2, '0');
            let m = String(srvTime.getMinutes()).padStart(2, '0');
            let s = String(srvTime.getSeconds()).padStart(2, '0');
            document.getElementById('clock').innerHTML = h + ':' + m + ':' + s;
          }, 1000);
        }

        document.getElementById('sisa').innerHTML = data.sisa === -1 ? 'Tak terbatas' : data.sisa + ' Porsi';
        document.getElementById('sisa').className = data.sisaWarning ? 'warning' : 'good';
        document.getElementById('feedsToday').innerHTML = data.feedsToday + ' Porsi';
        document.getElementById('nextFeed').innerHTML = data.nextFeed;
        document.getElementById('wifiGrade').innerHTML = data.wifiGrade;
        document.getElementById('wifiGrade').className = data.wifiColor;
        document.getElementById('uptime').innerHTML = data.uptime;
      });
  }
  
  setInterval(updateStatus, 5000);
  window.onload = updateStatus;
  
  function getDevice() { return "Web UI"; }
  
  function disableAllButtons(clickedBtn, loadingText) {
    let btns = document.querySelectorAll('button');
    btns.forEach(b => {
      b.style.pointerEvents = 'none';
      b.style.opacity = '0.5';
    });
    clickedBtn.innerHTML = loadingText;
  }
  
  function triggerFeed(btn) {
    disableAllButtons(btn, 'MEMPROSES... ⏳');
    let porsi = document.getElementById('porsiManual').value;
    window.location.href = '/feed?dev=' + encodeURIComponent(getDevice()) + '&porsi=' + porsi;
  }
  function triggerTestJeda(btn) { 
    disableAllButtons(btn, 'MENGUJI... ⏳');
    window.location.href = '/testjeda?dev=' + encodeURIComponent(getDevice()); 
  }
  function triggerCall(btn) { 
    disableAllButtons(btn, 'MEMANGGIL... 🐟');
    window.location.href = '/call?dev=' + encodeURIComponent(getDevice()); 
  }
  function confirmRefill(btn) {
    if(confirm('Isi ulang pakan penuh (30 Porsi)?')) {
      disableAllButtons(btn, 'MENGISI TANGKI... 📦');
      window.location.href='/refill';
    }
  }
  function disableSubmit(btn) {
    disableAllButtons(btn, 'MENYIMPAN... ⚙️');
  }
</script>
</head><body>
)rawliteral";


// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  
  loadSettings();
  
  lcd.init();
  lcd.backlight();
  
  // ========================================================
  // FITUR: SMART AUTO KALIBRASI BERDASARKAN MEMORI EEPROM
  // ========================================================
  bool didCalibrate = false;
  uint8_t doorState = EEPROM.read(EEPROM_DOOR_STATE_ADDR);
  
  // Jika nilai di memori adalah 1 (Pintu terbuka/nyangkut saat mati listrik)
  if (doorState == 1) {
    lcd.setCursor(0, 0);
    lcd.print("Auto Calibrate..");
    feederServo.attach(servoPin);
    feederServo.write(0); // Tutup paksa
    delay(1000);          
    feederServo.detach(); 
    
    // Perbarui catatan: Pintu sudah aman tertutup (0)
    EEPROM.write(EEPROM_DOOR_STATE_ADDR, 0);
    EEPROM.commit();
    didCalibrate = true;
  }
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Connecting ");
  
  setupWiFi();
  
  if (WiFi.status() == WL_CONNECTED) {
    configTime(settings.timezone * 3600, 0, settings.ntpServer, "pool.ntp.org");
    time_t now = time(nullptr);
    int ntpTimeout = 10;
    while (now < 1600000000 && ntpTimeout-- > 0) {
      delay(500);
      now = time(nullptr);
    }
    timeSynced = (now > 1600000000);
    if (timeSynced) {
      struct tm* p_tm = localtime(&now);
      lastDay = p_tm->tm_mday;
    }
  } else {
    timeSynced = false;
  }
  
  ArduinoOTA.setHostname("SmartFeeder-Pro");
  ArduinoOTA.begin();
  
  server.on("/", handleRoot);
  server.on("/status", handleStatusJSON);
  server.on("/feed", handleManualFeed);
  server.on("/testjeda", handleTestJeda);
  server.on("/call", handleManualCall);
  server.on("/refill", handleRefill);
  server.on("/setsisa", handleSetSisa);
  server.on("/save", handleSaveSettings);
  server.on("/resetdaily", handleResetDaily);
  server.on("/reset", handleResetDefaults);
  server.on("/reboot", handleReboot);
  server.on("/calibrate", handleCalibrate);
  server.on("/testwebhook", handleTestWebhook);
  
  server.begin();
  
  String bootMsg = "System Online!";
  while(bootMsg.length() < 16) bootMsg += " ";
  lcd.clear();
  lcd.setCursor(0, 0);
  if (timeSynced) lcd.print(bootMsg);
  else lcd.print("Time Not Synced ");
  
  // Kirim notifikasi Startup dengan menyertakan status kalibrasi
  kirimNotifStartup(didCalibrate);
  
  delay(1000);
  lcd.clear();
}

// ==================== LOOP ====================
void loop() {
  ESP.wdtFeed();
  server.handleClient();
  ArduinoOTA.handle();
  
  if (millis() - lastWiFiCheck > 30000) {
    lastWiFiCheck = millis();
    ensureWiFi();
  }
  
  if (millis() - lastNTPCheck > 3600000) {
    lastNTPCheck = millis();
    ensureNTP();
  }
  
  time_t now = time(nullptr);
  if (now > 1600000000) {
    struct tm* p_tm = localtime(&now);
    
    if (p_tm->tm_mday != lastDay) {
      feedsToday = 0;
      lastDay = p_tm->tm_mday;
      if (settings.discordNotify && (settings.notifyEvents & (1 << 3))) {
        kirimRingkasanHarian();
      }
    }
    
    if (millis() - lastLcdUpdate > 1000) {
      updateLCD(p_tm);
      lastLcdUpdate = millis();
    }
    
    if (settings.autoFeedEnabled) {
      for (int i = 0; i < MAX_SCHEDULES; i++) {
        Schedule& s = settings.schedules[i];
        if (s.enabled && p_tm->tm_hour == s.hour && p_tm->tm_min == s.minute && p_tm->tm_min != lastFedMinute) {
          if (settings.sisaMakanan > 0 || settings.sisaMakanan == -1) {
            beriMakanIkan("Jadwal " + String(i+1), "Sistem Internal", s.portion);
          } else {
            if (settings.discordNotify && (settings.notifyEvents & (1 << 2))) {
              kirimNotifDiscord("🚨 GAGAL MEMBERI MAKAN", "Jadwal " + String(i+1), "Sistem Internal", 16711680, 0, true);
            }
          }
          lastFedMinute = p_tm->tm_min;
          break;
        }
      }
    }
  }
}

// ==================== FUNGSI WIFI & NTP ====================
void setupWiFi() {
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  WiFi.begin(DEFAULT_SSID, DEFAULT_PASS);
  
  int timeout = 20;
  while (WiFi.status() != WL_CONNECTED && timeout-- > 0) {
    ESP.wdtFeed();
    delay(500);
  }
}

bool ensureWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    lcd.setCursor(0, 1);
    lcd.print("WiFi Reconnect..");
    WiFi.reconnect();
    int retry = 20;
    while (WiFi.status() != WL_CONNECTED && retry-- > 0) {
      delay(500);
      ESP.wdtFeed();
    }
    if (WiFi.status() == WL_CONNECTED) {
      lcd.setCursor(0, 1);
      lcd.print("WiFi Connected  ");
      return true;
    } else {
      lcd.setCursor(0, 1);
      lcd.print("WiFi Failed!    ");
      return false;
    }
  }
  return true;
}

bool ensureNTP() {
  if (WiFi.status() != WL_CONNECTED) return false;
  configTime(settings.timezone * 3600, 0, settings.ntpServer, "pool.ntp.org");
  time_t now = time(nullptr);
  int ntpTimeout = 10;
  while (now < 1600000000 && ntpTimeout-- > 0) {
    delay(500);
    now = time(nullptr);
  }
  timeSynced = (now > 1600000000);
  return timeSynced;
}

// ==================== EEPROM & SETTINGS ====================
void loadSettings() {
  EEPROM.get(EEPROM_ADDR, settings);
  
  if (settings.magic != EEPROM_MAGIC) {
    resetSettingsToDefault();
  } else {
    if (settings.servoAngle < 10 || settings.servoAngle > 180) settings.servoAngle = 90;
    if (settings.durasiBuka < 50 || settings.durasiBuka > 2000) settings.durasiBuka = 150;
    if (settings.jedaPorsi != 0 && settings.jedaPorsi != 5 && settings.jedaPorsi != 10) settings.jedaPorsi = 5;
    if (settings.porsiAutoDefault < 1 || settings.porsiAutoDefault > 5) settings.porsiAutoDefault = 1;
    if (settings.sisaMakanan < -1 || settings.sisaMakanan > 50) settings.sisaMakanan = 30;
    if (settings.timezone < -12 || settings.timezone > 14) settings.timezone = 7;
    if (strlen(settings.ntpServer) == 0) strcpy(settings.ntpServer, "pool.ntp.org");
    for (int i = 0; i < MAX_SCHEDULES; i++) {
      if (settings.schedules[i].hour > 23) settings.schedules[i].hour = 7;
      if (settings.schedules[i].minute > 59) settings.schedules[i].minute = 0;
      if (settings.schedules[i].portion < 1 || settings.schedules[i].portion > 5) settings.schedules[i].portion = 1;
    }
  }
  settings.webhook[199] = '\0';
}

void saveSettings() {
  settings.magic = EEPROM_MAGIC;
  EEPROM.put(EEPROM_ADDR, settings);
  EEPROM.commit();
}

void resetSettingsToDefault() {
  for (int i = 0; i < MAX_SCHEDULES; i++) {
    settings.schedules[i].enabled = (i < 2);
    settings.schedules[i].portion = 1;
  }
  settings.schedules[0].hour = 7; settings.schedules[0].minute = 0;
  settings.schedules[1].hour = 17; settings.schedules[1].minute = 0;
  for (int i = 2; i < MAX_SCHEDULES; i++) {
    settings.schedules[i].hour = 12; settings.schedules[i].minute = 0;
    settings.schedules[i].enabled = false;
  }
  
  settings.autoFeedEnabled = true;
  settings.servoAngle = 90;
  settings.durasiBuka = 150;
  settings.jedaPorsi = 5;
  settings.porsiAutoDefault = 1;
  settings.sisaMakanan = 30;
  settings.discordNotify = true;
  strcpy(settings.webhook, DEFAULT_WEBHOOK);
  settings.notifyEvents = 0b00111111;
  settings.timezone = 7;
  strcpy(settings.ntpServer, "pool.ntp.org");
  settings.magic = EEPROM_MAGIC;
  
  saveSettings();
}

// ==================== FUNGSI LCD ====================
void updateLCD(struct tm* p_tm) {
  char timeStr[20];
  sprintf(timeStr, "Time: %02d:%02d:%02d", p_tm->tm_hour, p_tm->tm_min, p_tm->tm_sec);
  String line1 = String(timeStr);
  while(line1.length() < 16) line1 += " "; 
  
  lcd.setCursor(0, 0);
  lcd.print(line1);
  
  String line2 = "";
  int mode = (p_tm->tm_sec / 5) % 3;
  if (mode == 0) {
    line2 = WiFi.localIP().toString();
  } else if (mode == 1) {
    line2 = "Sisa: ";
    if (settings.sisaMakanan == -1) line2 += "unlim";
    else line2 += String(settings.sisaMakanan);
    line2 += " Porsi";
  } else {
    line2 = "Next: " + getNextFeedTime();
  }
  
  while(line2.length() < 16) line2 += " "; 
  
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

String getNextFeedTime() {
  if (!settings.autoFeedEnabled) return "Disabled";
  time_t now = time(nullptr);
  if (now < 1600000000) return "Syncing...";
  struct tm* p_tm = localtime(&now);
  int currentMinutes = p_tm->tm_hour * 60 + p_tm->tm_min;
  
  int nextMinutes = -1;
  int nextHour = 0, nextMin = 0;
  
  for (int i = 0; i < MAX_SCHEDULES; i++) {
    if (!settings.schedules[i].enabled) continue;
    int schedMinutes = settings.schedules[i].hour * 60 + settings.schedules[i].minute;
    if (schedMinutes > currentMinutes) {
      if (nextMinutes == -1 || schedMinutes < nextMinutes) {
        nextMinutes = schedMinutes;
        nextHour = settings.schedules[i].hour;
        nextMin = settings.schedules[i].minute;
      }
    }
  }
  
  if (nextMinutes == -1) {
    for (int i = 0; i < MAX_SCHEDULES; i++) {
      if (!settings.schedules[i].enabled) continue;
      int schedMinutes = settings.schedules[i].hour * 60 + settings.schedules[i].minute;
      if (nextMinutes == -1 || schedMinutes < nextMinutes) {
        nextMinutes = schedMinutes + 24*60;
        nextHour = settings.schedules[i].hour;
        nextMin = settings.schedules[i].minute;
      }
    }
  }
  
  if (nextMinutes == -1) return "No Sched";
  
  char buf[6];
  sprintf(buf, "%02d:%02d", nextHour, nextMin);
  return String(buf);
}

// ==================== FUNGSI MAKAN ====================
void beriMakanIkan(String metode, String deviceInfo, int porsi) {
  if (settings.sisaMakanan == 0) return;
  
  String feedMsg = ">>> FEEDING <<< ";
  while(feedMsg.length() < 16) feedMsg += " ";
  lcd.setCursor(0,0);
  lcd.print(feedMsg);
  
  // Mencatat bahwa servo sedang mulai bekerja (Pintu Terbuka)
  EEPROM.write(EEPROM_DOOR_STATE_ADDR, 1);
  EEPROM.commit();
  
  feederServo.attach(servoPin);
  
  for(int i = 0; i < porsi; i++) {
    if (settings.sisaMakanan > 0) {
      settings.sisaMakanan--;
      saveSettings();
    }
    feedsToday++;
    
    feederServo.write(settings.servoAngle);
    delay(settings.durasiBuka);
    feederServo.write(0);
    
    if (i < porsi - 1 && settings.jedaPorsi > 0) {
      for(int d = settings.jedaPorsi; d > 0; d--) {
        lcd.setCursor(0,1);
        char buf[17];
        sprintf(buf, "Mengunyah: %02ds  ", d);
        String kunyah = String(buf);
        while(kunyah.length() < 16) kunyah += " ";
        lcd.print(kunyah);
        delay(1000);
        ESP.wdtFeed();
      }
      String dropMsg = "Dropping food!  ";
      while(dropMsg.length() < 16) dropMsg += " ";
      lcd.setCursor(0,1);
      lcd.print(dropMsg);
    } else {
      delay(1000);
    }
  }
  feederServo.detach();
  
  // Mencatat bahwa servo sudah selesai dan pintu sudah tertutup aman
  EEPROM.write(EEPROM_DOOR_STATE_ADDR, 0);
  EEPROM.commit();
  
  bool isWarning = (settings.sisaMakanan >= 0 && settings.sisaMakanan < 5);
  int color = isWarning ? 16711680 : (metode.indexOf("Manual") > -1 ? 3066993 : 3447003);
  String title = isWarning ? "🚨 LAPORAN MAKAN & PERINGATAN" : "🐟 LAPORAN MAKAN";
  
  if (settings.discordNotify) {
    if (isWarning && (settings.notifyEvents & (1 << 2))) {
      kirimNotifDiscord(title, metode, deviceInfo, color, porsi, true);
    } else if (!isWarning && (settings.notifyEvents & (1 << 1))) {
      kirimNotifDiscord(title, metode, deviceInfo, color, porsi, false);
    }
  }
  
  String doneMsg = "Done!           ";
  lcd.setCursor(0,0);
  lcd.print(doneMsg);
  String blankMsg = "                ";
  lcd.setCursor(0,1);
  lcd.print(blankMsg);
  delay(1500);
}

// ==================== FUNGSI NOTIF DISCORD ====================
void kirimNotifDiscord(String title, String metode, String deviceInfo, int color, int porsi, bool isWarning) {
  if (!settings.discordNotify) return;
  
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (http.begin(client, settings.webhook)) {
    http.addHeader("Content-Type", "application/json");
    
    String teksPorsi = (porsi > 0) ? String(porsi) + " Porsi" : "0 Porsi";
    String warningFood = (settings.sisaMakanan >= 0 && settings.sisaMakanan < 5) ? " ⚠️ HAMPIR HABIS!" : "";
    String stok = (settings.sisaMakanan == -1) ? "Tak terbatas" : String(settings.sisaMakanan) + " Porsi" + warningFood;
    String ip = WiFi.localIP().toString();
    
    String payload = "{\"embeds\": [{\"title\": \"" + title + "\",\"color\": " + String(color) + ",\"fields\": [";
    payload += "{\"name\": \"🛠️ Metode\", \"value\": \"`" + metode + "`\", \"inline\": true},";
    payload += "{\"name\": \"🍽️ Porsi Keluar\", \"value\": \"`" + teksPorsi + "`\", \"inline\": true},";
    payload += "{\"name\": \"📦 Sisa Di Wadah\", \"value\": \"`" + stok + "`\", \"inline\": false}";
    payload += "],\"footer\": {\"text\": \"IP: " + ip + "\"} }]}";
    
    http.POST(payload);
    http.end();
  }
}

// === FUNGSI NOTIF STARTUP / REBOOT SYSTEM (DIPERBAIKI DENGAN CEK MEMORI) ===
void kirimNotifStartup(bool calibrated) {
  if (!settings.discordNotify || !(settings.notifyEvents & (1 << 0))) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (http.begin(client, settings.webhook)) {
    http.addHeader("Content-Type", "application/json");

    String resetReason = ESP.getResetReason();
    String ip = WiFi.localIP().toString();

    int color = 3066993; 
    String status = "🔄 Restart Manual dari Web / Normal";
    String iconTitle = "🚀 SYSTEM STARTUP";

    // Mendeteksi Mati Listrik / Colok Listrik
    if (resetReason.indexOf("Power on") >= 0 || resetReason.indexOf("External System") >= 0) {
      status = "🔌 Mati Listrik / Colok Listrik Baru";
    }
    // Mendeteksi Error / Crash pada sistem
    else if (resetReason.indexOf("Watchdog") >= 0 || resetReason.indexOf("Exception") >= 0 || resetReason.indexOf("Panic") >= 0) {
      color = 16711680; 
      status = "⚠️ Sistem Error / Crash Recovery";
      iconTitle = "🚨 SYSTEM REBOOT WARNING";
    } 

    // Menampilkan status pintu (apakah dikalibrasi atau aman)
    String actionInfo = calibrated ? "✅ Auto Kalibrasi (Menutup paksa pintu yang macet)" : "✅ Aman (Pintu sudah dalam keadaan tertutup)";

    String payload = "{\"embeds\": [{\"title\": \"" + iconTitle + "\",\"color\": " + String(color) + ",\"description\": \"Sistem Smart Feeder baru saja menyala.\",\"fields\": [";
    payload += "{\"name\": \"📝 Penyebab Restart\", \"value\": \"`" + status + "`\", \"inline\": false},";
    payload += "{\"name\": \"⚙️ Tindakan Pintu Servo\", \"value\": \"`" + actionInfo + "`\", \"inline\": false},";
    payload += "{\"name\": \"🔍 Info Detail Sistem\", \"value\": \"`" + resetReason + "`\", \"inline\": false},";
    payload += "{\"name\": \"🌐 IP Address\", \"value\": \"`" + ip + "`\", \"inline\": false}";
    payload += "],\"footer\": {\"text\": \"Smart Feeder System\"} }]}";

    http.POST(payload);
    http.end();
  }
}

void kirimRingkasanHarian() {
  if (!settings.discordNotify || !(settings.notifyEvents & (1 << 3))) return;
  
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (http.begin(client, settings.webhook)) {
    http.addHeader("Content-Type", "application/json");
    
    String ip = WiFi.localIP().toString();
    String nextFeed = getNextFeedTime();
    String stok = (settings.sisaMakanan == -1) ? "Tak terbatas" : String(settings.sisaMakanan) + " Porsi";
    
    String payload = "{\"embeds\": [{\"title\": \"📊 RINGKASAN HARIAN\",\"color\": 15844367,\"fields\": [";
    payload += "{\"name\": \"🍽️ Total Porsi Hari Ini\", \"value\": \"`" + String(feedsToday) + " Porsi`\", \"inline\": true},";
    payload += "{\"name\": \"⏭️ Jadwal Berikutnya\", \"value\": \"`" + nextFeed + "`\", \"inline\": true},";
    payload += "{\"name\": \"📦 Sisa Pakan\", \"value\": \"`" + stok + "`\", \"inline\": true}";
    payload += "],\"footer\": {\"text\": \"Laporan otomatis harian\"} }]}";
    
    http.POST(payload);
    http.end();
  }
}

String getUptime() {
  unsigned long sec = millis() / 1000;
  int d = sec / 86400;
  int h = (sec % 86400) / 3600;
  int m = (sec % 3600) / 60;
  return String(d) + "d " + String(h) + "h " + String(m) + "m";
}

String getWiFiGrade() {
  int32_t rssi = WiFi.RSSI();
  if (rssi >= -55) return "A (Kuat)";
  else if (rssi >= -70) return "B (Bagus)";
  else return "C (Lemah)";
}

// ==================== HANDLER WEB ====================
void handleRoot() {
  String html;
  html.reserve(6000); 
  
  html += FPSTR(PAGE_HEAD); 

  html += "<div class='card'>\n";
  html += "<div id=\"clock\" class=\"live-clock\">--:--:--</div>\n";
  
  html += "<div class='info-box'>\n";
  html += "📦 Sisa Pakan: <span id=\"sisa\">" + String(settings.sisaMakanan == -1 ? "Tak terbatas" : String(settings.sisaMakanan) + " Porsi") + "</span><br>\n";
  html += "🍽️ Keluar Hari Ini: <span id=\"feedsToday\">" + String(feedsToday) + " Porsi</span><br>\n";
  html += "⏭️ Next Jadwal: <span id=\"nextFeed\">" + getNextFeedTime() + "</span><br>\n";
  html += "📶 Sinyal WiFi: <span id=\"wifiGrade\">" + getWiFiGrade() + "</span><br>\n";
  html += "⏱️ Uptime: <span id=\"uptime\">" + getUptime() + "</span>\n";
  html += "</div>\n";
  
  html += "<div class=\"action-group\">\n";
  html += "<select id=\"porsiManual\" style=\"width:40%;\">\n";
  html += "<option value=\"1\">1 Porsi</option><option value=\"2\">2 Porsi</option><option value=\"3\">3 Porsi</option>\n";
  html += "</select>\n";
  html += "<button class=\"btn-main\" style=\"width:60%;\" onclick=\"triggerFeed(this)\">BERI MAKAN</button>\n";
  html += "</div>\n";
  
  html += "<button class=\"btn-test\" onclick=\"triggerTestJeda(this)\">🧪 TEST JEDA (2 Porsi)</button>\n";
  html += "<button class=\"btn-call\" onclick=\"triggerCall(this)\">PANGGIL IKAN</button>\n";
  html += "<button class=\"btn-refill\" onclick=\"confirmRefill(this)\">🔁 Saya Baru Isi Ulang</button>\n";
  
  html += "<div style=\"background: #1e293b; padding: 10px; border-radius: 10px; margin-bottom: 15px;\">\n";
  html += "<form action='/setsisa' method='GET' style=\"display:flex; gap:5px;\" onsubmit=\"disableAllButtons(this.querySelector('button'), 'MENYIMPAN... ⚙️')\">\n";
  html += "<input type='number' name='sisa' value='" + String(settings.sisaMakanan) + "' min='-1' max='50' style=\"width:80px;\">\n";
  html += "<button type='submit' class=\"btn-save\" style=\"margin:0; padding:10px;\">Set Stok</button>\n";
  html += "</form>\n";
  html += "<small style=\"color:#94a3b8;\">-1 = tak terbatas</small>\n";
  html += "</div>\n";
  
  html += "<div style=\"background: rgba(15, 23, 42, 0.5); padding: 15px; border-radius: 12px; text-align: left; border: 1px solid #334155;\">\n";
  html += "<form action='/save' method='GET' onsubmit=\"disableSubmit(this.querySelector('.btn-save'))\">\n";
  html += "<div style=\"text-align:center; font-weight:bold; margin-bottom:15px; color:#38bdf8;\">⚙️ PENGATURAN SISTEM</div>\n";
  
  html += "<div style=\"margin-bottom:15px;\">\n";
  html += "<div style=\"font-weight:bold; margin-bottom:5px;\">📅 Jadwal Makan (maks 5):</div>\n";
  
  for (int i = 0; i < MAX_SCHEDULES; i++) {
    Schedule& s = settings.schedules[i];
    char buf[250];
    snprintf(buf, sizeof(buf),
      "<div class='schedule-row'>"
      "<input type='time' name='sched%d_time' value='%02d:%02d'>"
      "<input type='number' name='sched%d_porsi' value='%d' min='1' max='5'>"
      "<label><input type='checkbox' name='sched%d_en' value='1' %s> Aktif</label>"
      "</div>\n",
      i, s.hour, s.minute, i, s.portion, i, (s.enabled ? "checked" : ""));
    html += buf;
  }
  html += "</div>\n";
  
  html += "<div class=\"row\"><span>Porsi Default Jadwal:</span><input type='number' name='porsiauto' value='" + String(settings.porsiAutoDefault) + "' min='1' max='5'></div>\n";
  html += "<div class=\"row\"><span>Durasi Buka (ms):</span><input type='number' name='durasi' value='" + String(settings.durasiBuka) + "' min='50' max='2000'></div>\n";
  html += "<div class=\"row\"><span>Sudut Buka (°):</span><input type='number' name='angle' value='" + String(settings.servoAngle) + "' max='180' min='10'></div>\n";
  
  html += "<div class=\"row\"><span>Jeda Mengunyah:</span>\n";
  html += "<select name='jeda'>\n";
  html += "<option value='0' " + String(settings.jedaPorsi == 0 ? "selected" : "") + ">0 Detik</option>\n";
  html += "<option value='5' " + String(settings.jedaPorsi == 5 ? "selected" : "") + ">5 Detik</option>\n";
  html += "<option value='10' " + String(settings.jedaPorsi == 10 ? "selected" : "") + ">10 Detik</option>\n";
  html += "</select></div>\n";
  
  html += "<div class=\"row\"><span>Jadwal Auto:</span>\n";
  html += "<select name='auto'>\n";
  html += "<option value='1' " + String(settings.autoFeedEnabled ? "selected" : "") + ">Aktif</option>\n";
  html += "<option value='0' " + String(!settings.autoFeedEnabled ? "selected" : "") + ">Mati</option>\n";
  html += "</select></div>\n";
  
  html += "<hr>\n";
  html += "<div style=\"text-align:center; font-weight:bold; margin-bottom:15px; color:#38bdf8;\">🔔 DISCORD</div>\n";
  html += "<div class=\"row\"><span>Aktifkan Notifikasi:</span>\n";
  html += "<input type='checkbox' name='discord_enable' value='1' " + String(settings.discordNotify ? "checked" : "") + "></div>\n";
  
  html += "<div class=\"row\" style=\"flex-direction:column; align-items:stretch;\">\n";
  html += "<span style=\"margin-bottom:5px;\">Webhook URL:</span>\n";
  html += "<input type='text' name='webhook' value='" + String(settings.webhook) + "' placeholder='https://discord.com/api/webhooks/...' style=\"width:100%;\"></div>\n";
  
  html += "<div class=\"row\"><span>Event Startup:</span> <input type='checkbox' name='ev_startup' value='1' " + String((settings.notifyEvents & (1<<0)) ? "checked" : "") + "></div>\n";
  html += "<div class=\"row\"><span>Event Feeding:</span> <input type='checkbox' name='ev_feeding' value='1' " + String((settings.notifyEvents & (1<<1)) ? "checked" : "") + "></div>\n";
  html += "<div class=\"row\"><span>Event Warning:</span> <input type='checkbox' name='ev_warning' value='1' " + String((settings.notifyEvents & (1<<2)) ? "checked" : "") + "></div>\n";
  html += "<div class=\"row\"><span>Event Harian:</span> <input type='checkbox' name='ev_daily' value='1' " + String((settings.notifyEvents & (1<<3)) ? "checked" : "") + "></div>\n";
  html += "<div class=\"row\"><span>Event Panggil Ikan:</span> <input type='checkbox' name='ev_call' value='1' " + String((settings.notifyEvents & (1<<4)) ? "checked" : "") + "></div>\n";
  html += "<div class=\"row\"><span>Event Refill:</span> <input type='checkbox' name='ev_refill' value='1' " + String((settings.notifyEvents & (1<<5)) ? "checked" : "") + "></div>\n";
  
  html += "<button type='button' class=\"btn-test\" style=\"padding:5px;\" onclick=\"disableAllButtons(this, 'MENGIRIM... 📨'); window.location.href='/testwebhook'\">📨 Test Webhook</button>\n";
  
  html += "<hr>\n";
  html += "<div style=\"text-align:center; font-weight:bold; margin-bottom:15px; color:#38bdf8;\">🌐 ZONA WAKTU & NTP</div>\n";
  html += "<div class=\"row\"><span>Timezone (UTC+):</span><input type='number' name='tz' value='" + String(settings.timezone) + "' min='-12' max='14'></div>\n";
  html += "<div class=\"row\"><span>NTP Server:</span><input type='text' name='ntp' value='" + String(settings.ntpServer) + "' style=\"width:100%;\"></div>\n";
  
  html += "<hr>\n";
  html += "<div style=\"text-align:center; font-weight:bold; margin-bottom:15px; color:#38bdf8;\">🔧 KALIBRASI SERVO</div>\n";
  html += "<div class=\"row\"><span>Uji Sudut:</span>\n";
  html += "<input type='number' id='calAngle' value='90' min='10' max='180' style=\"width:60px;\">\n";
  html += "<button type='button' class=\"btn-call\" style=\"width:auto; padding:5px 10px; margin:0;\" onclick=\"disableAllButtons(this, 'MENGUJI... ⚙️'); window.location.href='/calibrate?angle='+document.getElementById('calAngle').value\">GERAKKAN</button>\n";
  html += "</div>\n";
  
  html += "<button type='submit' class='btn-save'>SIMPAN SEMUA PENGATURAN</button>\n";
  html += "</form>\n";
  
  html += "<div style=\"display: flex; gap: 5px; margin-top: 10px;\">\n";
  html += "<button class=\"btn-reset\" style=\"width:50%;\" onclick=\"if(confirm('Reset hitungan hari ini?')) { disableAllButtons(this, 'MERESET...'); window.location.href='/resetdaily'; }\">Reset Harian</button>\n";
  html += "<button class=\"btn-reset\" style=\"width:50%;\" onclick=\"if(confirm('Kembalikan semua ke default pabrik?')) { disableAllButtons(this, 'MERESET...'); window.location.href='/reset'; }\">Reset Default</button>\n";
  html += "</div>\n";
  html += "<br><a href='/reboot' onclick=\"return confirm('Restart Alat?')\" style='color:#94a3b8; font-size:11px; text-decoration:none; display:block; text-align:center;'>Restart System</a>\n";
  
  html += "</div></div></body></html>";
  
  server.send(200, "text/html", html); 
}

void handleStatusJSON() {
  time_t now = time(nullptr);
  struct tm* p_tm = localtime(&now);
  char timeStr[20];
  sprintf(timeStr, "%02d:%02d:%02d", p_tm->tm_hour, p_tm->tm_min, p_tm->tm_sec);
  
  bool sisaWarning = (settings.sisaMakanan >= 0 && settings.sisaMakanan < 5);
  String wifiGrade = getWiFiGrade();
  String wifiColor = (WiFi.RSSI() >= -65) ? "good" : (WiFi.RSSI() >= -75 ? "warning" : "bad");
  
  String json = "{";
  json += "\"time\":\"" + String(timeStr) + "\",";
  json += "\"sisa\":" + String(settings.sisaMakanan) + ",";
  json += "\"sisaWarning\":" + String(sisaWarning ? "true" : "false") + ",";
  json += "\"feedsToday\":" + String(feedsToday) + ",";
  json += "\"nextFeed\":\"" + getNextFeedTime() + "\",";
  json += "\"wifiGrade\":\"" + wifiGrade + "\",";
  json += "\"wifiColor\":\"" + wifiColor + "\",";
  json += "\"uptime\":\"" + getUptime() + "\"";
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleManualFeed() {
  String dev = server.hasArg("dev") ? server.arg("dev") : "Browser";
  int porsi = server.hasArg("porsi") ? server.arg("porsi").toInt() : 1;
  if (porsi < 1) porsi = 1;
  if (porsi > 5) porsi = 5;
  
  if (settings.sisaMakanan == 0) {
    server.send(200, "text/html", "<h2>Stok habis!</h2><meta http-equiv='refresh' content='2;url=/'>");
    return;
  }
  
  beriMakanIkan("Manual UI", dev, porsi);
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleTestJeda() {
  String dev = server.hasArg("dev") ? server.arg("dev") : "Browser";
  int porsiTest = 2;
  
  if (settings.sisaMakanan == 0) {
    server.send(200, "text/html", "<h2>Stok habis!</h2><meta http-equiv='refresh' content='2;url=/'>");
    return;
  }
  
  beriMakanIkan("Test Jeda UI", dev, porsiTest);
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleManualCall() {
  String dev = server.hasArg("dev") ? server.arg("dev") : "Browser";
  
  String callMsg = "Calling Fish... ";
  while(callMsg.length() < 16) callMsg += " ";
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(callMsg);
  
  // Mencatat ke memori sebelum menggerakkan servo
  EEPROM.write(EEPROM_DOOR_STATE_ADDR, 1);
  EEPROM.commit();
  
  feederServo.attach(servoPin);
  for(int j=0; j<5; j++) {
    feederServo.write(20);
    delay(150);
    feederServo.write(0);
    delay(150);
  }
  feederServo.detach();
  
  // Membersihkan catatan setelah selesai dan aman
  EEPROM.write(EEPROM_DOOR_STATE_ADDR, 0);
  EEPROM.commit();
  
  if (settings.discordNotify && (settings.notifyEvents & (1 << 4))) {
    kirimNotifDiscord("🔔 IKAN DIPANGGIL", "Manual UI", dev, 3447003, 0);
  }
  
  String doneMsg = "System Online!  ";
  while(doneMsg.length() < 16) doneMsg += " ";
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(doneMsg);
  
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleRefill() {
  settings.sisaMakanan = 30;
  saveSettings();
  if (settings.discordNotify && (settings.notifyEvents & (1 << 5))) {
    kirimNotifDiscord("🔄 TANKI PAKAN DIISI ULANG", "Manual UI", "Sistem", 3066993, 0);
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSetSisa() {
  if (server.hasArg("sisa")) {
    int sisa = server.arg("sisa").toInt();
    if (sisa < -1) sisa = -1;
    if (sisa > 50) sisa = 50;
    settings.sisaMakanan = sisa;
    saveSettings();
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSaveSettings() {
  for (int i = 0; i < MAX_SCHEDULES; i++) {
    String timeArg = "sched" + String(i) + "_time";
    String porsiArg = "sched" + String(i) + "_porsi";
    String enArg = "sched" + String(i) + "_en";
    
    if (server.hasArg(timeArg)) {
      String timestr = server.arg(timeArg);
      int hour = timestr.substring(0,2).toInt();
      int minute = timestr.substring(3,5).toInt();
      settings.schedules[i].hour = constrain(hour, 0, 23);
      settings.schedules[i].minute = constrain(minute, 0, 59);
    }
    if (server.hasArg(porsiArg)) {
      settings.schedules[i].portion = constrain(server.arg(porsiArg).toInt(), 1, 5);
    }
    settings.schedules[i].enabled = server.hasArg(enArg);
  }
  
  if (server.hasArg("porsiauto")) settings.porsiAutoDefault = constrain(server.arg("porsiauto").toInt(), 1, 5);
  if (server.hasArg("durasi")) settings.durasiBuka = constrain(server.arg("durasi").toInt(), 50, 2000);
  if (server.hasArg("angle")) settings.servoAngle = constrain(server.arg("angle").toInt(), 10, 180);
  if (server.hasArg("jeda")) {
    int j = server.arg("jeda").toInt();
    if (j == 0 || j == 5 || j == 10) settings.jedaPorsi = j;
  }
  settings.autoFeedEnabled = (server.arg("auto").toInt() == 1);
  
  settings.discordNotify = server.hasArg("discord_enable");
  if (server.hasArg("webhook")) {
    String w = server.arg("webhook");
    strncpy(settings.webhook, w.c_str(), 199);
    settings.webhook[199] = '\0';
  }
  
  settings.notifyEvents = 0;
  if (server.hasArg("ev_startup")) settings.notifyEvents |= (1<<0);
  if (server.hasArg("ev_feeding")) settings.notifyEvents |= (1<<1);
  if (server.hasArg("ev_warning")) settings.notifyEvents |= (1<<2);
  if (server.hasArg("ev_daily")) settings.notifyEvents |= (1<<3);
  if (server.hasArg("ev_call")) settings.notifyEvents |= (1<<4);
  if (server.hasArg("ev_refill")) settings.notifyEvents |= (1<<5);
  
  if (server.hasArg("tz")) settings.timezone = constrain(server.arg("tz").toInt(), -12, 14);
  if (server.hasArg("ntp")) {
    String ntp = server.arg("ntp");
    strncpy(settings.ntpServer, ntp.c_str(), 31);
    settings.ntpServer[31] = '\0';
  }
  
  saveSettings();
  
  if (WiFi.status() == WL_CONNECTED) {
    configTime(settings.timezone * 3600, 0, settings.ntpServer, "pool.ntp.org");
  }
  
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleResetDaily() {
  feedsToday = 0;
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleResetDefaults() {
  resetSettingsToDefault();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleReboot() {
  server.send(200, "text/html", "<h2>Restarting...</h2><meta http-equiv='refresh' content='5;url=/'>");
  delay(1000);
  ESP.restart();
}

void handleCalibrate() {
  if (!server.hasArg("angle")) {
    server.send(400, "text/plain", "Parameter angle diperlukan");
    return;
  }
  int angle = server.arg("angle").toInt();
  if (angle < 10 || angle > 180) angle = 90;
  
  String calMsg = "Calibrate " + String(angle) + "deg";
  while(calMsg.length() < 16) calMsg += " ";
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(calMsg);
  
  // Mencatat ke memori sebelum menggerakkan servo
  EEPROM.write(EEPROM_DOOR_STATE_ADDR, 1);
  EEPROM.commit();
  
  feederServo.attach(servoPin);
  feederServo.write(angle);
  delay(2000);
  feederServo.write(0);
  delay(500);
  feederServo.detach();
  
  // Membersihkan catatan setelah selesai dan aman
  EEPROM.write(EEPROM_DOOR_STATE_ADDR, 0);
  EEPROM.commit();
  
  String doneMsg = "Calibration done";
  while(doneMsg.length() < 16) doneMsg += " ";
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(doneMsg);
  delay(1000);
  
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleTestWebhook() {
  bool temp = settings.discordNotify;
  settings.discordNotify = true; 
  kirimNotifDiscord("🧪 TEST NOTIFIKASI", "Test", "Web UI", 10181046, 1, false);
  settings.discordNotify = temp;
  
  server.sendHeader("Location", "/");
  server.send(303);
}
