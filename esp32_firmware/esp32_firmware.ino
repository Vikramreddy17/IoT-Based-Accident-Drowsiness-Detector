#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "I2Cdev.h"
#include "MPU6050.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <TinyGPS++.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// ===== OLED =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ===== WIFI =====
const char* ssid     = "Vikram";
const char* password = "12345678";

#define BOT_TOKEN "8739700282:AAFT9rQx8TKM0D3jJWsW5ZpQeizjfItBAOQ"
#define CHAT_ID   "5762845982"

WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

// ===== WEB SERVER =====
WebServer server(80);

// ===== GPS =====
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);

// ===== PINS =====
#define MQ3_PIN    34
#define BUZZER_PIN 26
#define BUTTON_PIN 25
#define IN1  5
#define IN2  4
#define ENA  13

#define ALCOHOL_THRESHOLD 850
#define RESTART_TIME      30

// ===== STATES =====
#define STATE_NORMAL   0
#define STATE_ALCOHOL  1
#define STATE_ACCIDENT 2
#define SUB_DETECT     0
#define SUB_SEND       1
#define SUB_RESTART    2

int systemState = STATE_NORMAL;
int subState    = SUB_DETECT;

// ===== GLOBALS =====
MPU6050 mpu;
int16_t ax, ay, az, gx, gy, gz;
unsigned long subTimer   = 0;
bool alertSent           = false;
bool drowsyTrigger       = false;
bool motorRunning        = false;
bool lastButtonState     = HIGH;
bool telegramSent        = false;
int  alcoholValue        = 0;

// ===== MOTOR =====
void motorOn()  { digitalWrite(IN1,HIGH); digitalWrite(IN2,LOW);  digitalWrite(ENA,HIGH); motorRunning=true;  }
void motorOff() { digitalWrite(IN1,LOW);  digitalWrite(IN2,LOW);  digitalWrite(ENA,LOW);  motorRunning=false; }

// ===== BUTTON =====
bool buttonPressed(){
  bool current = digitalRead(BUTTON_PIN);
  if(current == LOW && lastButtonState == HIGH){
    delay(50);
    if(digitalRead(BUTTON_PIN)==LOW){ lastButtonState=LOW; return true; }
  }
  if(current == HIGH) lastButtonState = HIGH;
  return false;
}

// ===== BUZZER =====
void buzzerOn()  { digitalWrite(BUZZER_PIN, LOW);  }
void buzzerOff() { digitalWrite(BUZZER_PIN, HIGH); }

// ===== ALCOHOL =====
int readAlcohol(){
  long total = 0;
  for(int i=0;i<10;i++){ total += analogRead(MQ3_PIN); delay(5); }
  return total / 10;
}

// ===== TELEGRAM =====
void sendTelegramAlert(String type){
  if(WiFi.status() != WL_CONNECTED) return;
  String msg = "";
  if(type=="ACCIDENT")     msg += "🚨 ACCIDENT DETECTED\n\n";
  else if(type=="ALCOHOL") msg += "🍺 ALCOHOL DETECTED\n\n";
  else                     msg += "😴 DROWSINESS DETECTED\n\n";
  if(gps.location.isValid()){
    float lat = gps.location.lat();
    float lon = gps.location.lng();
    msg += "📍 Location:\n";
    msg += String(lat,6) + "," + String(lon,6) + "\n";
    msg += "https://maps.google.com/?q=" + String(lat,6) + "," + String(lon,6);
  } else {
    msg += "📍 GPS: No fix yet";
  }
  bot.sendMessage(CHAT_ID, msg, "");
  telegramSent = true;
}

// ===== TRIGGER ALERT — stops motor then sets state =====
void triggerAlert(int newState){
  motorOff();   // ← stop motor on ANY alert
  buzzerOn();
  systemState = newState;
  subState    = SUB_DETECT;
  subTimer    = millis();
}

// ===== WEB SERVER: /data API =====
void handleData(){
  StaticJsonDocument<512> doc;
  doc["alcohol"]         = alcoholValue;
  doc["ax"] = ax; doc["ay"] = ay; doc["az"] = az;
  doc["gx"] = gx; doc["gy"] = gy; doc["gz"] = gz;
  doc["drowsy"]          = drowsyTrigger;
  doc["accident"]        = (systemState == STATE_ACCIDENT && !drowsyTrigger);
  doc["alcoholDetected"] = (systemState == STATE_ALCOHOL);
  doc["telegram"]        = telegramSent;
  doc["motor"]           = motorRunning;
  doc["state"]           = systemState;
  if(gps.location.isValid()){
    doc["lat"] = gps.location.lat();
    doc["lon"] = gps.location.lng();
  } else {
    doc["lat"] = 0; doc["lon"] = 0;
  }
  String json;
  serializeJson(doc, json);
  server.sendHeader("Access-Control-Allow-Origin","*");
  server.send(200,"application/json",json);
}

// ===== BUILD HTML PARTS =====
String getHTMLPart1(){
  String h = "";
  h += "<!DOCTYPE html><html lang='en'><head>";
  h += "<meta charset='UTF-8'>";
  h += "<meta name='viewport' content='width=device-width,initial-scale=1.0'>";
  h += "<title>AI Driver Safety</title>";
  h += "<link href='https://fonts.googleapis.com/css2?family=Sora:wght@300;400;600;700&family=JetBrains+Mono:wght@400;600&display=swap' rel='stylesheet'>";
  h += "<link rel='stylesheet' href='https://unpkg.com/leaflet@1.9.4/dist/leaflet.css'/>";
  h += "<style>";
  h += ":root{--bg:#f4f6fb;--card:#fff;--border:#e2e8f4;--text:#1a202c;--muted:#64748b;";
  h += "--accent:#2563eb;--green:#16a34a;--green-light:#f0fdf4;";
  h += "--red:#dc2626;--red-light:#fef2f2;--yellow:#d97706;--yellow-light:#fffbeb;";
  h += "--shadow:0 1px 3px rgba(0,0,0,.06),0 4px 16px rgba(0,0,0,.04);--radius:16px;}";
  h += "*{margin:0;padding:0;box-sizing:border-box;}";
  h += "body{font-family:'Sora',sans-serif;background:var(--bg);color:var(--text);min-height:100vh;}";
  h += "header{background:var(--card);border-bottom:1px solid var(--border);padding:14px 20px;display:flex;align-items:center;justify-content:space-between;position:sticky;top:0;z-index:100;box-shadow:0 1px 8px rgba(0,0,0,.05);}";
  h += ".logo{display:flex;align-items:center;gap:10px;}";
  h += ".logo-icon{width:36px;height:36px;background:var(--accent);border-radius:9px;display:flex;align-items:center;justify-content:center;font-size:17px;}";
  h += ".logo-text{font-size:15px;font-weight:700;}";
  h += ".logo-sub{font-size:10px;color:var(--muted);}";
  h += ".hdr-right{display:flex;align-items:center;gap:10px;}";
  h += ".live-badge{display:flex;align-items:center;gap:6px;background:var(--green-light);color:var(--green);font-size:11px;font-weight:600;padding:5px 11px;border-radius:20px;border:1px solid #bbf7d0;}";
  h += ".live-dot{width:7px;height:7px;background:var(--green);border-radius:50%;animation:pulse 1.4s infinite;}";
  h += "@keyframes pulse{0%,100%{opacity:1;transform:scale(1);}50%{opacity:.4;transform:scale(.75);}}";
  h += ".esp-tag{font-family:'JetBrains Mono',monospace;font-size:11px;color:var(--muted);background:var(--bg);padding:4px 9px;border-radius:7px;border:1px solid var(--border);}";
  h += "main{max-width:1200px;margin:0 auto;padding:20px 16px;display:grid;gap:16px;}";
  h += ".status-row{display:grid;grid-template-columns:repeat(4,1fr);gap:12px;}";
  h += ".sc{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:16px;box-shadow:var(--shadow);display:flex;align-items:center;gap:12px;position:relative;overflow:hidden;}";
  h += ".sc::before{content:'';position:absolute;top:0;left:0;right:0;height:3px;background:var(--border);transition:background .3s;}";
  h += ".sc.alert::before{background:var(--red);}";
  h += ".sc.ok::before{background:var(--green);}";
  h += ".sc.warn::before{background:var(--yellow);}";
  h += ".sc-icon{width:42px;height:42px;border-radius:11px;display:flex;align-items:center;justify-content:center;font-size:20px;flex-shrink:0;background:var(--bg);}";
  h += ".sc.alert .sc-icon{background:var(--red-light);}";
  h += ".sc.ok .sc-icon{background:var(--green-light);}";
  h += ".sc.warn .sc-icon{background:var(--yellow-light);}";
  h += ".sc-label{font-size:10px;color:var(--muted);font-weight:600;text-transform:uppercase;letter-spacing:.5px;}";
  h += ".sc-value{font-size:13px;font-weight:700;margin-top:2px;}";
  h += ".sc.alert .sc-value{color:var(--red);}";
  h += ".sc.ok .sc-value{color:var(--green);}";
  h += ".sc.warn .sc-value{color:var(--yellow);}";
  h += ".grid2{display:grid;grid-template-columns:1fr 1fr;gap:16px;}";
  h += ".fw{grid-column:1/-1;}";
  h += ".card{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);box-shadow:var(--shadow);overflow:hidden;}";
  h += ".ch{padding:14px 18px 12px;border-bottom:1px solid var(--border);display:flex;align-items:center;justify-content:space-between;}";
  h += ".ct{font-size:13px;font-weight:700;display:flex;align-items:center;gap:7px;}";
  h += ".ct span{font-size:15px;}";
  h += ".cs{font-size:10px;color:var(--muted);margin-top:2px;}";
  h += ".cb{padding:16px 18px;}";
  h += ".upd{font-family:'JetBrains Mono',monospace;font-size:10px;color:var(--muted);}";
  h += ".gw{display:flex;align-items:center;gap:20px;}";
  h += ".gi{flex:1;}";
  h += ".gnum{font-family:'JetBrains Mono',monospace;font-size:38px;font-weight:700;line-height:1;}";
  h += ".gunit{font-size:11px;color:var(--muted);margin-top:3px;}";
  h += ".gbw{margin-top:12px;background:var(--bg);border-radius:7px;height:7px;overflow:hidden;border:1px solid var(--border);}";
  h += ".gb{height:100%;border-radius:7px;transition:width .5s ease,background .3s;background:var(--green);}";
  h += ".gticks{display:flex;justify-content:space-between;margin-top:4px;font-size:9px;color:var(--muted);font-family:'JetBrains Mono',monospace;}";
  h += ".thr{font-size:10px;color:var(--muted);margin-top:8px;display:flex;align-items:center;gap:5px;}";
  h += ".thrdot{width:7px;height:7px;border-radius:50%;background:var(--red);flex-shrink:0;}";
  h += ".astatus{margin-top:9px;padding:6px 11px;border-radius:7px;font-size:11px;font-weight:600;border:1px solid var(--border);background:var(--bg);display:inline-block;}";
  h += ".mpugrid{display:grid;grid-template-columns:repeat(3,1fr);gap:9px;}";
  h += ".mpuitem{background:var(--bg);border:1px solid var(--border);border-radius:11px;padding:11px;text-align:center;}";
  h += ".mpuaxis{font-size:10px;font-weight:700;color:var(--muted);text-transform:uppercase;letter-spacing:.5px;}";
  h += ".mpuval{font-family:'JetBrains Mono',monospace;font-size:16px;font-weight:600;margin-top:4px;color:var(--text);transition:color .3s;}";
  h += ".mpuval.danger{color:var(--red);}";
  h += ".mpubw{margin-top:6px;background:var(--border);border-radius:3px;height:3px;overflow:hidden;}";
  h += ".mpub{height:100%;border-radius:3px;transition:width .4s ease;}";
  h += ".msec{font-size:10px;color:var(--muted);font-weight:600;text-transform:uppercase;letter-spacing:.5px;margin-bottom:7px;}";
  h += "#map-wrap{width:100%;height:300px;border-radius:11px;overflow:hidden;background:#e8edf5;position:relative;}";
  h += "#map{width:100%;height:100%;z-index:1;}";
  h += ".no-gps{position:absolute;inset:0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:7px;color:var(--muted);font-size:12px;z-index:2;background:#e8edf5;}";
  h += ".no-gps .bi{font-size:36px;opacity:.3;}";
  h += ".gcoords{font-family:'JetBrains Mono',monospace;font-size:10px;color:var(--muted);background:var(--bg);padding:4px 9px;border-radius:6px;border:1px solid var(--border);}";
  h += ".loglist{display:flex;flex-direction:column;gap:6px;max-height:180px;overflow-y:auto;}";
  h += ".logitem{display:flex;align-items:flex-start;gap:8px;padding:8px 12px;border-radius:9px;background:var(--bg);border:1px solid var(--border);animation:logIn .3s ease;}";
  h += "@keyframes logIn{from{opacity:0;transform:translateY(-4px);}to{opacity:1;transform:translateY(0);}}";
  h += ".ldot{width:7px;height:7px;border-radius:50%;margin-top:3px;flex-shrink:0;}";
  h += ".ldot.red{background:var(--red);}";
  h += ".ldot.yellow{background:var(--yellow);}";
  h += ".ldot.green{background:var(--green);}";
  h += ".ldot.blue{background:var(--accent);}";
  h += ".ltxt{font-size:11px;color:var(--text);flex:1;}";
  h += ".ltime{font-family:'JetBrains Mono',monospace;font-size:9px;color:var(--muted);}";
  h += ".clrbtn{font-size:10px;color:var(--muted);background:var(--bg);border:1px solid var(--border);border-radius:7px;padding:4px 10px;cursor:pointer;font-family:Sora,sans-serif;}";
  h += "@media(max-width:750px){.status-row{grid-template-columns:repeat(2,1fr);}.grid2{grid-template-columns:1fr;}}";
  h += "</style></head><body>";
  return h;
}

String getHTMLPart2(){
  String h = "";
  h += "<header>";
  h += "<div class='logo'><div class='logo-icon'>&#128737;</div>";
  h += "<div><div class='logo-text'>AI Driver Safety</div>";
  h += "<div class='logo-sub'>Live Monitoring Dashboard</div></div></div>";
  h += "<div class='hdr-right'>";
  h += "<div class='live-badge'><div class='live-dot'></div>LIVE</div>";
  h += "<div class='esp-tag' id='esp-tag'>Connecting...</div></div></header>";
  h += "<main>";
  h += "<div class='status-row'>";
  h += "<div class='sc ok' id='card-motor'><div class='sc-icon'>&#128663;</div><div><div class='sc-label'>Engine</div><div class='sc-value' id='val-motor'>Stopped</div></div></div>";
  h += "<div class='sc ok' id='card-drowsy'><div class='sc-icon'>&#128564;</div><div><div class='sc-label'>Drowsiness</div><div class='sc-value' id='val-drowsy'>Not Detected</div></div></div>";
  h += "<div class='sc ok' id='card-accident'><div class='sc-icon'>&#128165;</div><div><div class='sc-label'>Accident</div><div class='sc-value' id='val-accident'>Not Detected</div></div></div>";
  h += "<div class='sc ok' id='card-telegram'><div class='sc-icon'>&#9993;</div><div><div class='sc-label'>Telegram</div><div class='sc-value' id='val-telegram'>Standby</div></div></div>";
  h += "</div>";
  h += "<div class='grid2'>";

  // Alcohol card
  h += "<div class='card'><div class='ch'><div><div class='ct'><span>&#127807;</span>MQ-3 Alcohol Sensor</div><div class='cs'>Alert threshold: 850 ADC units</div></div><div class='upd' id='upd-time'>--:--:--</div></div>";
  h += "<div class='cb'><div class='gw'>";
  h += "<svg width='90' height='90' viewBox='0 0 100 100'>";
  h += "<circle cx='50' cy='50' r='40' fill='none' stroke='#e2e8f4' stroke-width='10'/>";
  h += "<circle id='gauge-arc' cx='50' cy='50' r='40' fill='none' stroke='#16a34a' stroke-width='10' stroke-dasharray='251.2' stroke-dashoffset='251.2' stroke-linecap='round' transform='rotate(-90 50 50)' style='transition:stroke-dashoffset .6s ease,stroke .3s'/>";
  h += "<text x='50' y='44' text-anchor='middle' font-size='10' fill='#64748b' font-family='Sora,sans-serif'>ADC</text>";
  h += "<text id='gc' x='50' y='60' text-anchor='middle' font-size='12' font-weight='700' fill='#1a202c' font-family='JetBrains Mono,monospace'>0</text></svg>";
  h += "<div class='gi'><div class='gnum' id='alc-num'>0</div><div class='gunit'>Raw ADC (0-1024)</div>";
  h += "<div class='gbw'><div class='gb' id='alc-bar' style='width:0%'></div></div>";
  h += "<div class='gticks'><span>0</span><span>256</span><span>512</span><span>850</span><span>1024</span></div>";
  h += "<div class='thr'><div class='thrdot'></div>Alert fires above 850</div>";
  h += "<div class='astatus' id='alc-status'>Normal - No alcohol</div></div></div></div></div>";

  // MPU card
  h += "<div class='card'><div class='ch'><div><div class='ct'><span>&#128208;</span>MPU-6050 IMU</div><div class='cs'>Accident triggers at |axis| > 15000/20000</div></div></div>";
  h += "<div class='cb'>";
  h += "<div class='msec'>Accelerometer</div><div class='mpugrid'>";
  h += "<div class='mpuitem'><div class='mpuaxis'>AX</div><div class='mpuval' id='val-ax'>0</div><div class='mpubw'><div class='mpub' id='bar-ax' style='width:50%;background:#2563eb'></div></div></div>";
  h += "<div class='mpuitem'><div class='mpuaxis'>AY</div><div class='mpuval' id='val-ay'>0</div><div class='mpubw'><div class='mpub' id='bar-ay' style='width:50%;background:#2563eb'></div></div></div>";
  h += "<div class='mpuitem'><div class='mpuaxis'>AZ</div><div class='mpuval' id='val-az'>0</div><div class='mpubw'><div class='mpub' id='bar-az' style='width:50%;background:#2563eb'></div></div></div>";
  h += "</div>";
  h += "<div class='msec' style='margin-top:12px'>Gyroscope</div><div class='mpugrid'>";
  h += "<div class='mpuitem'><div class='mpuaxis'>GX</div><div class='mpuval' id='val-gx'>0</div><div class='mpubw'><div class='mpub' id='bar-gx' style='width:50%;background:#8b5cf6'></div></div></div>";
  h += "<div class='mpuitem'><div class='mpuaxis'>GY</div><div class='mpuval' id='val-gy'>0</div><div class='mpubw'><div class='mpub' id='bar-gy' style='width:50%;background:#8b5cf6'></div></div></div>";
  h += "<div class='mpuitem'><div class='mpuaxis'>GZ</div><div class='mpuval' id='val-gz'>0</div><div class='mpubw'><div class='mpub' id='bar-gz' style='width:50%;background:#8b5cf6'></div></div></div>";
  h += "</div></div></div>";

  // GPS map card
  h += "<div class='card fw'><div class='ch'><div><div class='ct'><span>&#128205;</span>Live GPS Location</div><div class='cs'>Neo-6M GPS - updates every second</div></div>";
  h += "<div class='gcoords' id='gcoords'>Waiting for GPS fix...</div></div>";
  h += "<div class='cb' style='padding:12px 18px'>";
  h += "<div id='map-wrap'><div id='map'></div>";
  h += "<div class='no-gps' id='no-gps'><div class='bi'>&#128752;</div><div>Waiting for GPS signal...</div><div style='font-size:10px;opacity:.6'>Place near window for better reception</div></div>";
  h += "</div></div></div>";

  // Log card
  h += "<div class='card fw'><div class='ch'><div><div class='ct'><span>&#128203;</span>Event Log</div><div class='cs'>All detections this session</div></div>";
  h += "<button class='clrbtn' onclick='clearLog()'>Clear</button></div>";
  h += "<div class='cb'><div class='loglist' id='loglist'>";
  h += "<div class='logitem'><div class='ldot green'></div><div class='ltxt'>System initialized - monitoring active</div><div class='ltime' id='init-t'>--:--</div></div>";
  h += "</div></div></div>";

  h += "</div></main>";
  return h;
}

String getHTMLPart3(){
  String h = "";
  h += "<script src='https://unpkg.com/leaflet@1.9.4/dist/leaflet.js'></script>";
  h += "<script>";
  h += "var API=window.location.origin+'/data';";
  h += "document.getElementById('esp-tag').textContent='ESP32 - '+window.location.hostname;";
  h += "var gmap=null,gmarker=null;";
  h += "var prev={drowsy:false,accident:false,alcoholDetected:false,telegram:false};";
  h += "var DEFAULT_LAT=21.885983,DEFAULT_LON=82.624503;";
  h += "var GPS_TIMEOUT_MS=10*60*1000;";
  h += "var gpsFixTime=null,defaultApplied=false,bootTime=Date.now();";

  h += "function tnow(){return new Date().toLocaleTimeString('en-IN',{hour:'2-digit',minute:'2-digit',second:'2-digit'});}";
  h += "document.getElementById('init-t').textContent=tnow().slice(0,5);";

  h += "function addLog(txt,col){";
  h += "var l=document.getElementById('loglist');";
  h += "var d=document.createElement('div');";
  h += "d.className='logitem';";
  h += "d.innerHTML='<div class=\"ldot '+col+'\"></div><div class=\"ltxt\">'+txt+'</div><div class=\"ltime\">'+tnow().slice(0,5)+'</div>';";
  h += "l.prepend(d);if(l.children.length>40)l.lastChild.remove();}";

  h += "function clearLog(){document.getElementById('loglist').innerHTML='';}";

  h += "function setCard(id,state,txt){";
  h += "document.getElementById('card-'+id).className='sc '+state;";
  h += "document.getElementById('val-'+id).textContent=txt;}";

  h += "function updateAlc(v){";
  h += "var pct=Math.min(v/1024,1);";
  h += "var col=v>850?'#dc2626':v>700?'#d97706':'#16a34a';";
  h += "var st='Normal - No alcohol',sbg='var(--green-light)';";
  h += "if(v>850){st='WARNING - Alcohol detected!';sbg='#fef2f2';}";
  h += "else if(v>700){st='Medium - Caution';sbg='#fffbeb';}";
  h += "else if(v>400){st='Low - Trace detected';sbg='#fefce8';}";
  h += "document.getElementById('alc-num').textContent=v;";
  h += "document.getElementById('gc').textContent=v;";
  h += "var bar=document.getElementById('alc-bar');";
  h += "bar.style.width=(pct*100)+'%';bar.style.background=col;";
  h += "var st_el=document.getElementById('alc-status');";
  h += "st_el.textContent=st;st_el.style.background=sbg;";
  h += "var arc=document.getElementById('gauge-arc');";
  h += "arc.style.strokeDashoffset=251.2-(pct*251.2);arc.style.stroke=col;}";

  h += "function mpuSet(id,v,col){";
  h += "var el=document.getElementById('val-'+id);";
  h += "el.textContent=v;";
  h += "el.className='mpuval'+(Math.abs(v)>15000?' danger':'');";
  h += "var pct=50+(v/65536)*50;";
  h += "var bar=document.getElementById('bar-'+id);";
  h += "bar.style.width=Math.max(0,Math.min(100,pct))+'%';";
  h += "bar.style.background=col;}";

  h += "function initMap(){";
  h += "gmap=L.map('map').setView([DEFAULT_LAT,DEFAULT_LON],15);";
  h += "L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png',{maxZoom:19,attribution:'OpenStreetMap'}).addTo(gmap);";
  h += "var icon=L.divIcon({html:'<div style=\"width:16px;height:16px;background:#2563eb;border:3px solid #fff;border-radius:50%;box-shadow:0 0 6px rgba(0,0,0,.4)\"></div>',iconSize:[16,16],iconAnchor:[8,8],className:''});";
  h += "gmarker=L.marker([DEFAULT_LAT,DEFAULT_LON],{icon:icon}).addTo(gmap);}";
  h += "initMap();";

  h += "function updateMap(lat,lon){";
  h += "if(!gmap||!lat||lat===0)return;";
  h += "var pos=[parseFloat(lat),parseFloat(lon)];";
  h += "gmarker.setLatLng(pos);gmap.panTo(pos);";
  h += "document.getElementById('gcoords').textContent=lat.toFixed(6)+', '+lon.toFixed(6);";
  h += "document.getElementById('no-gps').style.display='none';}";

  h += "function fetchData(){";
  h += "fetch(API).then(function(r){return r.json();}).then(function(d){";
  h += "document.getElementById('upd-time').textContent=tnow();";

  // Motor card — shows stopped if any alert active
  h += "if(d.alcoholDetected){";
  h += "setCard('motor','alert','Stopped - Alcohol!');";
  h += "if(!prev.alcoholDetected)addLog('Alcohol detected! Motor stopped. ADC='+d.alcohol,'red');}";
  h += "else if(d.drowsy){";
  h += "setCard('motor','alert','Stopped - Drowsy!');}";
  h += "else if(d.accident){";
  h += "setCard('motor','alert','Stopped - Accident!');}";
  h += "else{setCard('motor','ok',d.motor?'Running':'Stopped');}";

  h += "updateAlc(d.alcohol);";

  h += "if(d.drowsy){setCard('drowsy','alert','Detected!');if(!prev.drowsy)addLog('Drowsiness! Motor stopped.','yellow');}";
  h += "else{setCard('drowsy','ok','Not Detected');}";

  h += "if(d.accident){setCard('accident','alert','Detected!');if(!prev.accident)addLog('Accident detected! Motor stopped.','red');}";
  h += "else{setCard('accident','ok','Not Detected');}";

  h += "if(d.telegram){setCard('telegram','warn','Alert Sent');if(!prev.telegram)addLog('Telegram alert sent to owner','blue');}";
  h += "else{setCard('telegram','ok','Standby');}";

  h += "mpuSet('ax',d.ax,'#2563eb');mpuSet('ay',d.ay,'#2563eb');mpuSet('az',d.az,'#2563eb');";
  h += "mpuSet('gx',d.gx,'#8b5cf6');mpuSet('gy',d.gy,'#8b5cf6');mpuSet('gz',d.gz,'#8b5cf6');";

  h += "if(d.lat&&d.lat!==0){gpsFixTime=Date.now();defaultApplied=false;updateMap(d.lat,d.lon);}";
  h += "else{if(!defaultApplied&&(Date.now()-bootTime)>=GPS_TIMEOUT_MS){";
  h += "gmarker.setLatLng([DEFAULT_LAT,DEFAULT_LON]);gmap.panTo([DEFAULT_LAT,DEFAULT_LON]);";
  h += "document.getElementById('gcoords').textContent=DEFAULT_LAT.toFixed(6)+', '+DEFAULT_LON.toFixed(6)+' (default)';";
  h += "document.getElementById('no-gps').style.display='none';";
  h += "defaultApplied=true;addLog('GPS no fix after 10 min - showing default location','yellow');}}";

  h += "prev={drowsy:d.drowsy,accident:d.accident,alcoholDetected:d.alcoholDetected,telegram:d.telegram};";
  h += "}).catch(function(){});}";

  h += "fetchData();setInterval(fetchData,1000);";
  h += "</script></body></html>";
  return h;
}

// ===== WEB SERVER: / (Dashboard Page) =====
void handleRoot(){
  String html = getHTMLPart1() + getHTMLPart2() + getHTMLPart3();
  server.send(200, "text/html", html);
}

// ===== OLED BOOT SEQUENCE =====
void bootSequence(){
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(10,8);  display.println("AI Driver Safety");
  display.setCursor(15,20); display.println("Drowsiness System");
  display.drawLine(0,32,128,32,SSD1306_WHITE);
  display.setCursor(10,40); display.println("  Initializing...");
  display.display();
  delay(5000);

  WiFi.begin(ssid, password);
  int dotCount = 0;
  unsigned long wStart = millis();

  while(WiFi.status() != WL_CONNECTED){
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0,5);  display.println("Connecting WiFi...");
    display.setCursor(0,18); display.println(ssid);
    display.setCursor(0,34); display.print("Please wait");
    for(int d=0;d<dotCount;d++) display.print(".");
    int prog = ((millis()-wStart)/100) % 128;
    display.drawRect(0,54,128,8,SSD1306_WHITE);
    display.fillRect(0,54,prog,8,SSD1306_WHITE);
    display.display();
    dotCount++; if(dotCount>3) dotCount=0;
    delay(400);
    if(millis()-wStart > 15000){
      display.clearDisplay();
      display.setCursor(10,20); display.println("WiFi FAILED!");
      display.setCursor(5,36);  display.println("Check credentials");
      display.display();
      delay(3000); break;
    }
  }

  if(WiFi.status() == WL_CONNECTED){
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0,0);  display.println("WiFi Connected!");
    display.drawLine(0,12,128,12,SSD1306_WHITE);
    display.setCursor(0,16); display.println("Open Dashboard:");
    display.setCursor(0,28); display.print("http://");
    display.println(WiFi.localIP().toString());
    display.setCursor(0,42); display.println("Type above URL in");
    display.setCursor(0,54); display.println("phone/PC browser");
    display.display();
    delay(5000);
  }

  for(int i=10; i>=1; i--){
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0,0);  display.println("Sensor Warm-Up");
    display.drawLine(0,12,128,12,SSD1306_WHITE);
    display.setCursor(0,16); display.println("MQ-3 heating up...");
    display.setTextSize(3);
    display.setCursor(i>=10?38:52, 28);
    display.print(i);
    display.setTextSize(1);
    display.setCursor(85,48); display.print("sec");
    int bw = (i*128)/10;
    display.drawRect(0,56,128,7,SSD1306_WHITE);
    display.fillRect(0,56,bw,7,SSD1306_WHITE);
    display.display();
    delay(1000);
  }

  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(20,18); display.println("SYSTEM");
  display.setCursor(30,40); display.println("READY!");
  display.display();
  delay(1500);
}

// ===== SETUP =====
void setup(){
  Serial.begin(115200);
  pinMode(BUZZER_PIN,OUTPUT);
  pinMode(BUTTON_PIN,INPUT_PULLUP);
  pinMode(IN1,OUTPUT); pinMode(IN2,OUTPUT); pinMode(ENA,OUTPUT);
  motorOff(); buzzerOff();

  Wire.begin(21,22);
  display.begin(SSD1306_SWITCHCAPVCC,0x3C);
  display.setTextColor(SSD1306_WHITE);

  client.setInsecure();
  gpsSerial.begin(9600,SERIAL_8N1,18,19);
  mpu.initialize();

  bootSequence();

  server.on("/",     handleRoot);
  server.on("/data", handleData);
  server.begin();
}

// ===== LOOP =====
void loop(){
  server.handleClient();

  while(gpsSerial.available()) gps.encode(gpsSerial.read());

  // Drowsiness from Python via USB Serial
  String data="";
  while(Serial.available()){
    char c=Serial.read(); if(c=='\n') break; data+=c;
  }
  data.trim();
  if(data=="DROWSY" && systemState==STATE_NORMAL){
    drowsyTrigger=true;
    triggerAlert(STATE_ACCIDENT);  // stops motor + sets state
  }

  alcoholValue = readAlcohol();
  mpu.getMotion6(&ax,&ay,&az,&gx,&gy,&gz);

  // Button only works in normal state
  if(systemState==STATE_NORMAL){
    if(buttonPressed()){
      motorRunning=!motorRunning;
      if(motorRunning) motorOn(); else motorOff();
    }
  }

  // Accident detection — stops motor immediately
  if((abs(ax)>15000||abs(ay)>15000||abs(az)>20000)&&systemState==STATE_NORMAL){
    triggerAlert(STATE_ACCIDENT);
  }

  // Alcohol detection — stops motor immediately
  if(alcoholValue>ALCOHOL_THRESHOLD&&systemState==STATE_NORMAL){
    triggerAlert(STATE_ALCOHOL);
  }

  // Normal state display
  if(systemState==STATE_NORMAL){
    buzzerOff(); telegramSent=false;
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0,0);  display.println("AI Driver Safety");
    display.drawLine(0,10,128,10,SSD1306_WHITE);
    display.setCursor(0,14); display.print("Alcohol : "); display.println(alcoholValue);
    display.setCursor(0,26);
    if(alcoholValue<400)       display.println("Status  : Normal");
    else if(alcoholValue<700)  display.println("Status  : Low");
    else if(alcoholValue<850)  display.println("Status  : Medium");
    else                       display.println("Status  : HIGH!");
    display.setCursor(0,38);
    display.print("AX:"); display.print(ax);
    display.print(" AY:"); display.println(ay);
    display.setCursor(0,50);
    if(motorRunning) display.println("Engine  : RUNNING");
    else             display.println("Engine  : STOPPED");
    display.display();
  }
  else{
    String type=(systemState==STATE_ALCOHOL)?"ALCOHOL":"ACCIDENT";
    if(drowsyTrigger) type="DROWSY";

    if(subState==SUB_DETECT){
      display.clearDisplay();
      display.setTextSize(1); display.setCursor(0,0);
      if(type=="ALCOHOL")      display.println("!! ALCOHOL !!");
      else if(type=="DROWSY")  display.println("!! DROWSINESS !!");
      else                     display.println("!! ACCIDENT !!");
      display.drawLine(0,10,128,10,SSD1306_WHITE);
      display.setTextSize(2); display.setCursor(5,18); display.println("DETECTED");
      display.setTextSize(1); display.setCursor(0,40); display.println("Motor: STOPPED");
      display.setCursor(0,52); display.println("Preparing alert...");
      display.display();
      if(millis()-subTimer>3000){ subState=SUB_SEND; subTimer=millis(); }
    }
    else if(subState==SUB_SEND){
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0,5);  display.println("Sending Telegram");
      display.setCursor(0,18); display.println("Alert to owner...");
      display.setCursor(0,35); display.println("Please wait.");
      display.display();
      if(!alertSent){ sendTelegramAlert(type); alertSent=true; }
      if(millis()-subTimer>2000){ subState=SUB_RESTART; subTimer=millis(); }
    }
    else{
      int remain=RESTART_TIME-(millis()-subTimer)/1000;
      display.clearDisplay();
      display.setTextSize(1); display.setCursor(0,0); display.println("Alert Sent!");
      display.drawLine(0,10,128,10,SSD1306_WHITE);
      display.setCursor(0,15); display.println("System restart in:");
      display.setTextSize(3);
      display.setCursor(remain>=10?30:45, 28); display.print(remain);
      display.setTextSize(1); display.setCursor(90,46); display.print("sec");
      int bw=(remain*128)/RESTART_TIME;
      display.drawRect(0,56,128,7,SSD1306_WHITE);
      display.fillRect(0,56,bw,7,SSD1306_WHITE);
      display.display();
      if(remain<=0){
        systemState=STATE_NORMAL; alertSent=false;
        drowsyTrigger=false; buzzerOff();
      }
    }
  }

  delay(200);
}
