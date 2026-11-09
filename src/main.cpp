// GeekMagic OS - experimental HelloCubic-Lite hardware prototype.
// LCD pinout and vendor sequence are informed by Times-Z/GeekMagic-Open-Firmware.
// Target: ESP8266 / ST7789 / 240x240. Hardware revisions can differ.
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <time.h>
#include <math.h>
#include <memory>
#include "control_panel.h"

constexpr char VERSION[]="0.5.0";

namespace {
constexpr uint8_t PIN_DC=0, PIN_RST=2, PIN_BL=5;
constexpr uint16_t BLACK=0x0822, WHITE=0xFFFF, TRACK=0x2948, BLUE=0x665F;
constexpr uint16_t PANEL=0x1086;
constexpr int DISPLAY_SIZE=240, CAPTURE_ROWS=8;
// The prism needs a vertical flip relative to the direct-view 0xC0 setting.
constexpr uint8_t PANEL_MADCTL=0x40;
constexpr int PANEL_ROW_OFFSET=(PANEL_MADCTL&0x80)?320-DISPLAY_SIZE:0;
constexpr uint32_t FRAME_PERIOD_MS=350;
constexpr uint32_t WEATHER_MAX_AGE_MS=60*60*1000;
constexpr int STYLE_ADDRESS=32;
constexpr uint32_t STYLE_MAGIC=0x43554231;

struct DisplaySettings{
  uint32_t magic=STYLE_MAGIC;
  int16_t utcMinutes=0;
  uint8_t palette=0,clock=0,brightness=100,cycleSeconds=0;
};
DisplaySettings settings;
static_assert(STYLE_ADDRESS+sizeof(DisplaySettings)<=64,"Display settings fit the existing EEPROM");
constexpr uint16_t PALETTES[][2]={{0xB53F,0x77FD},{0xFE45,0xFF9B},{0x7E1F,0xAFBF},{0xFFFF,0xCE59}};
constexpr char SETTINGS_HEADER[]="X-CubicOS-Setup";

class CrystalDisplay final : public Adafruit_GFX {
  uint8_t* capturePixels=nullptr;
  int captureY=0;
public:
  CrystalDisplay():Adafruit_GFX(DISPLAY_SIZE,DISPLAY_SIZE){}

  bool capturing() const {return capturePixels!=nullptr;}
  void capture(uint8_t* pixels,int top=0){capturePixels=pixels;captureY=top;}

  void command(uint8_t cmd){digitalWrite(PIN_DC,LOW);SPI.transfer(cmd);}
  void bytes(const uint8_t *values,size_t n){digitalWrite(PIN_DC,HIGH);for(size_t i=0;i<n;i++)SPI.transfer(values[i]);}
  void data(uint8_t value){bytes(&value,1);}
  void init(){
    pinMode(PIN_DC,OUTPUT); pinMode(PIN_RST,OUTPUT); pinMode(PIN_BL,OUTPUT);
    digitalWrite(PIN_BL,HIGH); digitalWrite(PIN_DC,HIGH);
    SPI.begin();
    SPI.beginTransaction(SPISettings(40000000,MSBFIRST,SPI_MODE3));
    digitalWrite(PIN_RST,HIGH);delay(120);digitalWrite(PIN_RST,LOW);delay(120);digitalWrite(PIN_RST,HIGH);delay(120);
    command(0x11);delay(120);  // Sleep out
    const uint8_t porch[]={0x1F,0x1F,0x00,0x33,0x33};command(0xB2);bytes(porch,sizeof(porch));
    command(0x35);data(0x00);
    command(0x36);data(PANEL_MADCTL);
    command(0x3A);data(0x05); // RGB565
    command(0xB7);data(0x00);command(0xBB);data(0x36);command(0xC0);data(0x2C);
    command(0xC2);data(0x01);command(0xC3);data(0x13);command(0xC4);data(0x20);
    command(0xC6);data(0x13);const uint8_t power[]={0xA4,0xA1};command(0xD0);bytes(power,2);command(0xD6);data(0xA1);
    const uint8_t gammaP[]={0xD0,0x04,0x0D,0x11,0x13,0x2B,0x3F,0x54,0x4C,0x18,0x0D,0x0B,0x1F,0x23};
    const uint8_t gammaN[]={0xD0,0x04,0x0C,0x11,0x13,0x2C,0x3F,0x44,0x51,0x2F,0x1F,0x1F,0x20,0x23};
    command(0xE0);bytes(gammaP,sizeof(gammaP));command(0xE1);bytes(gammaN,sizeof(gammaN));
    const uint8_t gammaCtrl[]={0x1D,0,0};command(0xE4);bytes(gammaCtrl,3);
    command(0x21);command(0x29);delay(20);digitalWrite(PIN_BL,LOW);
    fillScreen(BLACK);setTextWrap(false);
  }

  void setWindow(int16_t x,int16_t y,int16_t w,int16_t h){
    y+=PANEL_ROW_OFFSET;
    command(0x2A);const uint8_t col[]={uint8_t(x>>8),uint8_t(x),uint8_t((x+w-1)>>8),uint8_t(x+w-1)};bytes(col,4);
    command(0x2B);const uint8_t row[]={uint8_t(y>>8),uint8_t(y),uint8_t((y+h-1)>>8),uint8_t(y+h-1)};bytes(row,4);
    command(0x2C);digitalWrite(PIN_DC,HIGH);
  }

  void drawPixel(int16_t x,int16_t y,uint16_t color) override {fillRect(x,y,1,1,color);}

  void fillRect(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t color) override {
    if(w<=0||h<=0||x>=240||y>=240||x+w<=0||y+h<=0)return;
    int16_t x2=(int16_t)min(239,(int)(x+w-1)),y2=(int16_t)min(239,(int)(y+h-1));
    x=(int16_t)max(0,(int)x);y=(int16_t)max(0,(int)y);
    if(capturing()){
      const int first=max(int(y),captureY), last=min(int(y2),captureY+CAPTURE_ROWS-1);
      const uint8_t r=(color>>11)&31, g=(color>>5)&63, b=color&31;
      for(int row=first;row<=last;row++){
        uint8_t* pixel=capturePixels+3*((row-captureY)*DISPLAY_SIZE+x);
        for(int col=x;col<=x2;col++){
          *pixel++=(b<<3)|(b>>2);
          *pixel++=(g<<2)|(g>>4);
          *pixel++=(r<<3)|(r>>2);
        }
      }
      return;
    }
    setWindow(x,y,x2-x+1,y2-y+1);
    uint32_t n=uint32_t(x2-x+1)*uint32_t(y2-y+1);
    while(n--){SPI.transfer16(color);if((n&2047)==0)yield();}
  }
};

CrystalDisplay lcd;
ESP8266WebServer web(80);
ESP8266HTTPUpdateServer httpUpdater;
uint8_t page=0;
uint16_t accent=PALETTES[0][0], accent2=PALETTES[0][1];
String apName,apPassword;
String hostName,pendingSSID,pendingPassword;
String previousSSID,previousPassword;
uint32_t wifiSetupAt=0;
enum class WiFiTrial : uint8_t { Idle, Queued, Connecting, Saved, Failed };
WiFiTrial wifiTrial=WiFiTrial::Idle;
IPAddress lanIP;
bool mdnsActive=false;
struct Metrics{int cpu=-1,gpu=-1,ram=-1,gpuTemp=-1;uint32_t updated=0;} metrics;
struct Weather{
  char place[19]{};
  int16_t temperature=0;
  uint8_t humidity=0,code=0;
  uint32_t updated=0;
} weather;
uint8_t spectrum[16]{};
uint32_t spectrumAt=0,lastFrame=0,pageSince=0,wifiScanAt=0;
struct PaintState{
  uint8_t page=255;
  int clockMinute=-1,clockSecond=-1;
  Metrics metrics;
  bool metricsValid=false;
  uint8_t audioHeight[16]{};
  bool audioLive=false;
  IPAddress aboutIP;
  uint32_t weatherDrawn=0;
  bool weatherValid=false;
} paint;
uint32_t paintMs=0;
time_t paintTime=0;

void textAt(int x,int y,const String& value,uint16_t color=WHITE,uint8_t scale=1){
  lcd.setTextSize(scale);lcd.setTextColor(color);lcd.setCursor(x,y);lcd.print(value);
}

void centered(int y,const String& value,uint16_t color=WHITE,uint8_t scale=2){
  textAt((DISPLAY_SIZE-int(value.length())*6*scale)/2,y,value,color,scale);
}

void frame(const char* title){
  if(paint.page==page)return;
  lcd.fillScreen(BLACK);
  lcd.fillRect(0,0,154,3,accent);lcd.fillRect(154,0,86,3,accent2);
  textAt(14,17,title,WHITE,3);
  lcd.drawFastHLine(14,48,212,TRACK);
  for(uint8_t i=0;i<5;i++)lcd.fillCircle(88+16*i,226,3,i==page?accent2:TRACK);
  paint.page=page;
}

void bar(int x,int y,int width,int percent,uint16_t color){
  lcd.fillRoundRect(x,y,width,10,4,TRACK);
  if(percent>=0)lcd.fillRoundRect(x,y,max(4,width*min(100,percent)/100),10,4,color);
}

void renderClock(bool fresh){
  frame("CLOCK");time_t raw=paintTime;
  if(raw<1700000000){
    if(!fresh)return;
    centered(66,"CONNECT TO WI-FI",accent,2);
    centered(99,apName);
    centered(132,lcd.capturing()?"PASS ON CUBE":apPassword,accent2);
    centered(171,"192.168.4.1");
    paint.clockMinute=-1;
    return;
  }
  raw+=long(settings.utcMinutes)*60;
  tm local{};gmtime_r(&raw,&local);
  char hhmm[6],date[20];strftime(hhmm,sizeof(hhmm),"%H:%M",&local);strftime(date,sizeof(date),"%d %b %Y",&local);
  int minute=local.tm_hour*60+local.tm_min;
  const uint16_t face=settings.clock==0?PANEL:BLACK;
  const int clockScale=settings.clock==0?6:7;
  if(fresh||paint.clockMinute<0){
    lcd.fillRect(12,55,216,157,BLACK);
    if(settings.clock==0)lcd.fillRoundRect(12,60,216,148,12,face);
    lcd.fillRect(30,187,180,6,TRACK);
  }
  if(fresh||minute!=paint.clockMinute){
    lcd.setTextSize(clockScale);lcd.setTextColor(WHITE,face);lcd.setCursor((240-30*clockScale)/2,79);lcd.print(hhmm);
    lcd.setTextSize(3);lcd.setTextColor(accent2,face);lcd.setCursor(21,151);lcd.print(date);
    paint.clockMinute=minute;
  }
  if(fresh||paint.clockSecond!=local.tm_sec){
    const int width=3*(local.tm_sec+1);
    const int previous=fresh||paint.clockSecond<0?0:3*(paint.clockSecond+1);
    if(width<previous)lcd.fillRect(30+width,187,previous-width,6,TRACK);
    if(width>previous)lcd.fillRect(30+previous,187,width-previous,6,accent2);
    paint.clockSecond=local.tm_sec;
  }
}

void renderMetrics(bool fresh){
  frame("PC METRICS");
  const bool valid=metrics.updated!=0&&paintMs-metrics.updated<=10000;
  const bool layoutChanged=fresh||paint.metricsValid!=valid;
  if(!fresh&&layoutChanged)lcd.fillRect(12,65,217,148,BLACK);
  paint.metricsValid=valid;
  if(!valid){
    if(layoutChanged){
      centered(91,"WAITING FOR PC",WHITE,2);
      centered(139,"START PC FEED",accent2,2);
    }
    return;
  }
  struct Item{const char* label;int value;int previous;uint16_t color;};
  const Item rows[]={
    {"CPU",metrics.cpu,paint.metrics.cpu,accent},
    {"GPU",metrics.gpu,paint.metrics.gpu,accent2},
    {"RAM",metrics.ram,paint.metrics.ram,BLUE}
  };
  for(int i=0;i<3;i++){
    const int y=59+i*47;
    const Item& row=rows[i];
    if(layoutChanged)textAt(18,y,row.label,WHITE,2);
    if(layoutChanged||row.value!=row.previous){
      lcd.fillRect(151,y,72,24,BLACK);
      const String number=String(row.value)+"%";
      textAt(223-int(number.length())*18,y,number,row.color,3);
      bar(18,y+27,204,row.value,row.color);
    }
  }
  if(layoutChanged||metrics.gpuTemp!=paint.metrics.gpuTemp){
    lcd.fillRect(18,197,204,16,BLACK);
    centered(197,"GPU TEMP "+String(metrics.gpuTemp)+" C",WHITE);
  }
  paint.metrics=metrics;
}

void renderAudio(bool fresh){
  frame("SPECTRUM");
  const bool live=spectrumAt!=0&&paintMs-spectrumAt<3500;
  if(fresh){memset(paint.audioHeight,0,sizeof(paint.audioHeight));paint.audioLive=!live;}
  for(int i=0;i<16;i++){
    uint8_t val=live?spectrum[i]:uint8_t(constrain(int(35+22*sin(paintMs*.002+i*.5)+20*sin(paintMs*.003-i)),0,100));
    int h=12+min(105,(int)val*105/100);
    int old=paint.audioHeight[i];
    if(h>old)lcd.fillRect(14+i*13,183-h,8,h-old,(i%3==0)?accent2:accent);
    else if(h<old)lcd.fillRect(14+i*13,183-old,8,old-h,BLACK);
    paint.audioHeight[i]=h;
  }
  if(fresh||paint.audioLive!=live){
    lcd.fillRect(14,188,200,17,BLACK);
    centered(191,live?"LIVE AUDIO":"DEMO AUDIO",WHITE);
    paint.audioLive=live;
  }
}

void renderWeather(bool fresh){
  frame("WEATHER");
  const bool valid=weather.updated&&paintMs-weather.updated<WEATHER_MAX_AGE_MS;
  if(!fresh&&paint.weatherDrawn==weather.updated&&paint.weatherValid==valid)return;
  if(!fresh)lcd.fillRect(12,56,216,155,BLACK);
  paint.weatherDrawn=weather.updated;
  paint.weatherValid=valid;
  if(!valid){
    centered(93,"NO WEATHER DATA");
    centered(143,"START WEATHER FEED",accent2);
    return;
  }
  centered(62,weather.place,accent);
  centered(91,String(weather.temperature)+" C",WHITE,5);
  const char* conditions=weather.code==0?"CLEAR":weather.code<=3?"CLOUDY":weather.code<=48?"FOG":weather.code<=67?"RAIN":weather.code<=77?"SNOW":weather.code<=82?"SHOWERS":weather.code<=86?"SNOW":"STORM";
  centered(152,conditions,accent2);
  centered(182,"HUMIDITY "+String(weather.humidity)+"%");
}

void renderAbout(bool fresh){
  if(fresh){
    frame("DEVICE");
    centered(63,String("OS ")+VERSION,accent2,3);
    centered(104,apName);
    centered(132,lcd.capturing()?"PASS ON CUBE":apPassword);
  }
  if(fresh||lanIP!=paint.aboutIP){
    lcd.fillRect(12,163,216,44,BLACK);
    centered(164,lanIP.isSet()?"LOCAL NETWORK":"SETUP NETWORK",accent2);
    centered(190,lanIP.isSet()?lanIP.toString():"192.168.4.1");
    paint.aboutIP=lanIP;
  }
}

void draw(uint32_t now,time_t localTime){
  paintMs=now;
  paintTime=localTime;
  const bool fresh=paint.page!=page;
  switch(page){case 0:renderClock(fresh);break;case 1:renderMetrics(fresh);break;case 2:renderAudio(fresh);break;case 3:renderWeather(fresh);break;default:renderAbout(fresh);break;}
}

void sendScreen(){
  constexpr size_t stride=DISPLAY_SIZE*3;
  constexpr size_t imageSize=stride*DISPLAY_SIZE;
  static_assert(stride%4==0&&DISPLAY_SIZE%CAPTURE_ROWS==0,"BMP tile alignment");
  std::unique_ptr<uint8_t[]> tile(new(std::nothrow) uint8_t[stride*CAPTURE_ROWS]);
  if(!tile){web.send(503,"text/plain","Not enough memory to capture display");return;}
  uint8_t header[54]={'B','M'};
  const auto put32=[&header](size_t offset,uint32_t value){
    for(size_t i=0;i<4;i++)header[offset+i]=uint8_t(value>>(i*8));
  };
  put32(2,sizeof(header)+imageSize);
  put32(10,sizeof(header));put32(14,40);
  put32(18,DISPLAY_SIZE);put32(22,uint32_t(-DISPLAY_SIZE)); // Top-down BGR.
  header[26]=1;header[28]=24;put32(34,imageSize);

  web.sendHeader("Cache-Control","no-store");
  web.setContentLength(sizeof(header)+imageSize);
  web.send(200,"image/bmp","");
  auto& client=web.client();
  if(client.write(header,sizeof(header))!=sizeof(header))return;
  const PaintState previousPaint=paint;
  const uint32_t now=millis();
  const time_t localTime=time(nullptr);
  for(int top=0;top<DISPLAY_SIZE&&client.connected();top+=CAPTURE_ROWS){
    lcd.capture(tile.get(),top);
    paint.page=255;
    draw(now,localTime);
    lcd.capture(nullptr);
    if(client.write(tile.get(),stride*CAPTURE_ROWS)!=stride*CAPTURE_ROWS)break;
    yield();
  }
  paint=previousPaint;
}

void loadAccessPointPassword(){
  // Keep the AP password unchanged after a reboot; store it in ESP8266 EEPROM emulation.
  constexpr uint32_t MAGIC=0x474D4F32;
  EEPROM.begin(64);
  uint32_t marker=0;EEPROM.get(0,marker);
  char saved[24]{};
  if(marker==MAGIC){for(size_t i=0;i<sizeof(saved);i++)saved[i]=char(EEPROM.read(4+i));saved[23]='\0';}
  if(marker!=MAGIC||strncmp(saved,"OS-",3)!=0||strlen(saved)<12){
    randomSeed(ESP.getCycleCount()^micros()^(uint32_t(analogRead(A0))<<12));
    snprintf(saved,sizeof(saved),"OS-%08lX%08lX",(unsigned long)random(0x7FFFFFFF),(unsigned long)random(0x7FFFFFFF));
    EEPROM.put(0,MAGIC);
    for(size_t i=0;i<sizeof(saved);i++)EEPROM.write(4+i,uint8_t(saved[i]));
    EEPROM.commit();
  }
  apPassword=String(saved);
  DisplaySettings stored;
  EEPROM.get(STYLE_ADDRESS,stored);
  if(stored.magic==STYLE_MAGIC&&stored.palette<4&&stored.clock<2&&
     stored.brightness>=15&&stored.brightness<=100&&
     stored.utcMinutes>=-720&&stored.utcMinutes<=840&&
     (stored.cycleSeconds==0||(stored.cycleSeconds>=5&&stored.cycleSeconds<=120)))settings=stored;
  EEPROM.end();
}

void jsonReply(int code,JsonDocument& doc){String result;serializeJson(doc,result);web.sendHeader("Cache-Control","no-store");web.send(code,"application/json",result);}
int percent(JsonVariantConst v){return v.is<int>()?constrain(v.as<int>(),0,100):-1;}

void applyDisplaySettings(){
  accent=PALETTES[settings.palette][0];
  accent2=PALETTES[settings.palette][1];
  analogWriteRange(100);
  analogWrite(PIN_BL,100-settings.brightness);
  paint.page=255;
  lastFrame=0;
  pageSince=millis();
}

void selectPage(uint8_t selected){
  page=selected;
  lastFrame=0;
  pageSince=millis();
}

bool requireAdmin(){
  if(web.authenticate("admin",apPassword.c_str()))return true;
  web.requestAuthentication();
  return false;
}

bool requireSetupHeader(){
  if(web.header(SETTINGS_HEADER)=="1")return true;
  web.send(403,"text/plain","Use the control panel");
  return false;
}

void serviceWiFi(){
  // Let the form response go out before STA scanning can change the AP channel.
  if(wifiTrial==WiFiTrial::Queued&&millis()-wifiSetupAt>=750){
    previousSSID=WiFi.SSID();
    previousPassword=WiFi.psk();
    WiFi.disconnect(false,false);
    WiFi.begin(pendingSSID,pendingPassword);
    wifiTrial=WiFiTrial::Connecting;
    wifiSetupAt=millis();
  }
  if(wifiTrial==WiFiTrial::Connecting){
    if(WiFi.status()==WL_CONNECTED&&WiFi.SSID()==pendingSSID){
      WiFi.persistent(true);
      WiFi.begin(pendingSSID,pendingPassword);
      WiFi.persistent(false);
      wifiTrial=WiFiTrial::Saved;
    }else if(millis()-wifiSetupAt>=25000){
      WiFi.disconnect(false,false);
      if(previousSSID.length())WiFi.begin(previousSSID,previousPassword);
      wifiTrial=WiFiTrial::Failed;
    }
  }
  if(wifiTrial==WiFiTrial::Saved||wifiTrial==WiFiTrial::Failed){
    pendingSSID.clear();
    pendingPassword.clear();
    previousSSID.clear();
    previousPassword.clear();
  }
  if(wifiScanAt&&millis()-wifiScanAt>20000&&WiFi.scanComplete()!=WIFI_SCAN_RUNNING){
    WiFi.scanDelete();
    wifiScanAt=0;
  }
  const IPAddress current=WiFi.status()==WL_CONNECTED?WiFi.localIP():IPAddress();
  if(current!=lanIP){
    if(mdnsActive)MDNS.close();
    lanIP=current;
    mdnsActive=lanIP.isSet()&&MDNS.begin(hostName);
    if(mdnsActive)MDNS.addService("http","tcp",80);
    if(lanIP.isSet())configTime(0,0,"pool.ntp.org","time.google.com");
  }
  if(mdnsActive)MDNS.update();
}

void setupWiFiPage(){
  web.collectHeaders(SETTINGS_HEADER);
  web.on("/wifi",HTTP_GET,[]{
    if(!requireAdmin())return;
    static const char html[] PROGMEM=R"rawliteral(<!doctype html>
<html lang="ru"><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>Wi-Fi · GeekMagic OS</title>
<style>body{font:16px system-ui;background:#0b0d16;color:#eee;max-width:500px;margin:40px auto;padding:18px}label{display:block;margin:12px 0}input,button{font:inherit;padding:8px}a{color:#b7b2ff}#status{overflow-wrap:anywhere}</style>
<h1>Домашний Wi-Fi</h1>
<p>Откройте эту форму из сети CubicOS. Укажите точное имя сети 2,4 ГГц и пароль.</p>
<p><button id="scan" type="button">Найти сети</button> <span id="scan-status" role="status"></span></p>
<label>Доступные сети <select id="networks" hidden aria-label="Доступные сети"></select></label>
<form id="wifi">
<label>Имя сети <input name="ssid" required maxlength="32" autocomplete="off"></label>
<label>Пароль <input name="password" type="password" maxlength="64" autocomplete="new-password"></label>
<button>Сохранить и подключиться</button>
</form>
<p id="result" role="status"></p><p id="status"></p><a href="/">Веб-панель</a>
<script>
const form=document.getElementById('wifi'), result=document.getElementById('result'), statusLine=document.getElementById('status');
async function status(){
  try{
    const s=await (await fetch('/api/status',{cache:'no-store'})).json();
    const messages={queued:'Начинаю подключение…',connecting:'Проверяю подключение…',saved:'Настройки сохранены.',failed:'Подключиться не удалось. Проверьте имя и пароль.'};
    statusLine.textContent=(messages[s.wifi_setup]||'')+' '+(s.wifi_connected?'LAN: '+s.ip+' · '+s.host:'Домашняя сеть пока не подключена.');
  }catch{statusLine.textContent='Связь с кубиком потеряна. Проверьте подключение к CubicOS.';}
}
form.onsubmit=async e=>{
  e.preventDefault();
  const button=form.querySelector('button');
  button.disabled=true;
  try{
    const response=await fetch('/wifi',{method:'POST',headers:{'X-CubicOS-Setup':'1'},body:new URLSearchParams(new FormData(form))});
    result.textContent=await response.text();
    if(response.ok)form.elements.password.value='';
  }catch{result.textContent='Связь прервалась. Подключитесь к CubicOS и проверьте статус.';}
  finally{button.disabled=false;}
};
const scan=document.getElementById('scan'), scanStatus=document.getElementById('scan-status'), networks=document.getElementById('networks');
scan.onclick=async()=>{
  scan.disabled=true;scanStatus.textContent='Ищу…';
  try{
    const start=await fetch('/api/wifi/scan',{method:'POST',headers:{'X-CubicOS-Setup':'1'}});
    if(!start.ok)throw Error();
    for(let i=0;i<16;i++){
      await new Promise(resolve=>setTimeout(resolve,500));
      const reply=await fetch('/api/wifi/scan',{cache:'no-store'});
      if(reply.status===202)continue;
      if(!reply.ok)throw Error();
      const found=await reply.json();
      networks.replaceChildren(new Option('Выберите сеть',''),...found.map(n=>new Option(n.ssid+' ('+n.rssi+' dBm)',n.ssid)));
      networks.hidden=!found.length;
      scanStatus.textContent=found.length?'Выберите сеть или введите имя.':'Сети не найдены.';
      return;
    }
    throw Error();
  }catch{scanStatus.textContent='Поиск не завершился. Попробуйте ещё раз.';}
  finally{scan.disabled=false;}
};
networks.onchange=()=>{if(networks.value)form.elements.ssid.value=networks.value;};
status();setInterval(status,2500);
</script>)rawliteral";
    web.sendHeader("Cache-Control","no-store");
    web.send_P(200,"text/html; charset=utf-8",html);
  });
  web.on("/wifi",HTTP_POST,[]{
    if(web.client().localIP()!=WiFi.softAPIP()){
      web.send(403,"text/plain; charset=utf-8","Откройте форму из сети CubicOS.");return;
    }
    if(!requireAdmin()||!requireSetupHeader())return;
    if(pendingSSID.length()||WiFi.scanComplete()==WIFI_SCAN_RUNNING){web.send(409,"text/plain; charset=utf-8","Подождите окончания поиска или подключения.");return;}
    const String ssid=web.arg("ssid"), password=web.arg("password");
    const size_t n=password.length();
    const bool hexKey=n==64&&strspn(password.c_str(),"0123456789abcdefABCDEF")==64;
    if(!web.hasArg("password")||!ssid.length()||ssid.length()>32||strlen(ssid.c_str())!=ssid.length()||
       strlen(password.c_str())!=n||(n!=0&&(n<8||n>63)&&!hexKey)){
      web.send(400,"text/plain; charset=utf-8","SSID: 1–32 байта. Пароль: 8–63 символа или 64 шестнадцатеричных знака.");return;
    }
    pendingSSID=ssid;
    pendingPassword=password;
    wifiSetupAt=millis();
    wifiTrial=WiFiTrial::Queued;
    web.send(202,"text/plain; charset=utf-8","Кубик подключается. Если сеть CubicOS пропадёт на несколько секунд, подключитесь к ней снова.");
  });
  web.on("/api/wifi/scan",HTTP_POST,[]{
    if(!requireAdmin()||!requireSetupHeader())return;
    if(pendingSSID.length()){web.send(409,"text/plain","Wi-Fi setup in progress");return;}
    if(WiFi.scanComplete()!=WIFI_SCAN_RUNNING){
      WiFi.scanDelete();
      WiFi.scanNetworks(true,false);
      wifiScanAt=millis();
    }
    web.send(202,"text/plain","Scanning");
  });
  web.on("/api/wifi/scan",HTTP_GET,[]{
    if(!requireAdmin())return;
    const int count=WiFi.scanComplete();
    if(count==WIFI_SCAN_RUNNING){web.send(202,"text/plain","Scanning");return;}
    if(count<0){web.send(404,"text/plain","No scan results");return;}
    JsonDocument doc;
    JsonArray networks=doc.to<JsonArray>();
    for(int i=0;i<count&&networks.size()<24;i++){
      const String name=WiFi.SSID(i);
      if(!name.length()||name==apName)continue;
      JsonObject match;
      for(JsonObject network:networks){
        if(network["ssid"].as<String>()==name){match=network;break;}
      }
      const int rssi=WiFi.RSSI(i);
      if(match.isNull())match=networks.add<JsonObject>();
      else if(match["rssi"].as<int>()>=rssi)continue;
      match["ssid"]=name;
      match["rssi"]=rssi;
    }
    jsonReply(200,doc);
  });
}

void setupWeb(){
  setupWiFiPage();
  web.on("/debug/screen.bmp",HTTP_GET,sendScreen);
  web.on("/",HTTP_GET,[]{
    web.sendHeader("Cache-Control","no-store");
    web.send_P(200,"text/html; charset=utf-8",CONTROL_PANEL);
  });
  web.on("/studio",HTTP_GET,[]{
    if(!requireAdmin())return;
    web.sendHeader("Cache-Control","no-store");
    web.send_P(200,"text/html; charset=utf-8",CONTROL_PANEL);
  });
  web.on("/api/display",HTTP_GET,[]{
    if(!requireAdmin())return;
    JsonDocument doc;
    doc["palette"]=settings.palette;
    doc["clock"]=settings.clock;
    doc["brightness"]=settings.brightness;
    doc["cycle_seconds"]=settings.cycleSeconds;
    doc["utc_minutes"]=settings.utcMinutes;
    jsonReply(200,doc);
  });
  web.on("/api/display",HTTP_POST,[]{
    if(!requireAdmin()||!requireSetupHeader())return;
    JsonDocument doc;
    const auto invalid=[](){web.send(400,"text/plain","Invalid display settings");};
    if(deserializeJson(doc,web.arg("plain"))||!doc["palette"].is<int>()||
       !doc["clock"].is<int>()||!doc["brightness"].is<int>()||
       !doc["cycle_seconds"].is<int>()||!doc["utc_minutes"].is<int>()){invalid();return;}
    const int palette=doc["palette"], clock=doc["clock"], brightness=doc["brightness"];
    const int cycle=doc["cycle_seconds"], utc=doc["utc_minutes"];
    if(palette<0||palette>=4||clock<0||clock>=2||brightness<15||brightness>100||
       (cycle!=0&&(cycle<5||cycle>120))||utc<-720||utc>840){invalid();return;}
    if(settings.palette==palette&&settings.clock==clock&&settings.brightness==brightness&&
       settings.cycleSeconds==cycle&&settings.utcMinutes==utc){web.send(200,"text/plain","OK");return;}
    DisplaySettings next;
    next.palette=palette;next.clock=clock;next.brightness=brightness;
    next.cycleSeconds=cycle;next.utcMinutes=utc;
    EEPROM.begin(64);
    EEPROM.put(STYLE_ADDRESS,next);
    const bool saved=EEPROM.commit();
    EEPROM.end();
    if(!saved){web.send(503,"text/plain","Display settings could not be saved");return;}
    settings=next;
    applyDisplaySettings();
    web.send(200,"text/plain","OK");
  });
  web.on("/api/status",HTTP_GET,[]{
    JsonDocument doc;
    doc["version"]=VERSION;
    doc["firmware_md5"]=ESP.getSketchMD5();
    doc["host"]=hostName+".local";
    doc["wifi_connected"]=lanIP.isSet();
    doc["ip"]=lanIP.isSet()?lanIP.toString():WiFi.softAPIP().toString();
    doc["page"]=page;
    doc["uptime_ms"]=millis();
    constexpr const char* wifiStates[]={"idle","queued","connecting","saved","failed"};
    doc["wifi_setup"]=wifiStates[static_cast<uint8_t>(wifiTrial)];
    doc["heap_bytes"]=ESP.getFreeHeap();
    if(metrics.updated)doc["metrics_age_ms"]=millis()-metrics.updated;
    else doc["metrics_age_ms"]=nullptr;
    jsonReply(200,doc);
  });
  web.on("/api/page",HTTP_POST,[]{
    const String value=web.arg("page");
    if(value.length()!=1||value[0]<'0'||value[0]>'4'){web.send(400,"text/plain","page must be 0..4");return;}
    selectPage(value[0]-'0');
    web.send(200,"text/plain","OK");
  });
  web.on("/api/metrics",HTTP_POST,[]{
    JsonDocument doc;auto err=deserializeJson(doc,web.arg("plain"));if(err){web.send(400,"text/plain","Invalid JSON");return;}
    int cpu=percent(doc["cpu"]),gpu=percent(doc["gpu"]),ram=percent(doc["ram"]),temp=percent(doc["gpu_temp"]);
    if(cpu<0||gpu<0||ram<0||temp<0){web.send(400,"text/plain","Expected cpu,gpu,ram,gpu_temp 0..100");return;}
    metrics={cpu,gpu,ram,temp,millis()};web.send(200,"text/plain","OK");
  });
  web.on("/api/spectrum",HTTP_POST,[]{
    JsonDocument doc;if(deserializeJson(doc,web.arg("plain"))||!doc["bands"].is<JsonArray>()||doc["bands"].as<JsonArray>().size()!=16){web.send(400,"text/plain","Expected 16 bands array");return;}
    uint8_t next[16];
    for(size_t i=0;i<16;i++){
      const auto value=doc["bands"][i];
      if(!value.is<int>()||value.as<int>()<0||value.as<int>()>100){web.send(400,"text/plain","Each band must be 0..100");return;}
      next[i]=value.as<int>();
    }
    memcpy(spectrum,next,sizeof(spectrum));
    spectrumAt=millis();web.send(200,"text/plain","OK");
  });
  web.on("/api/weather",HTTP_POST,[]{
    JsonDocument doc;
    if(deserializeJson(doc,web.arg("plain"))||!doc["place"].is<const char*>()||
       !doc["temperature"].is<int>()||!doc["humidity"].is<int>()||!doc["code"].is<int>()){
      web.send(400,"text/plain","Expected place, temperature, humidity and code");return;
    }
    const char* place=doc["place"];
    const int temperature=doc["temperature"], humidity=doc["humidity"], code=doc["code"];
    if(!place[0]||strlen(place)>=sizeof(weather.place)||strspn(place,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 .'-")!=strlen(place)||
       temperature<-99||temperature>99||humidity<0||humidity>100||code<0||code>99){
      web.send(400,"text/plain","Weather values are out of range");return;
    }
    snprintf(weather.place,sizeof(weather.place),"%s",place);
    weather.temperature=temperature;weather.humidity=humidity;weather.code=code;
    weather.updated=millis();
    web.send(200,"text/plain","OK");
  });
  httpUpdater.setup(&web,"/update","admin",apPassword);
  web.begin();
}
}

void setup(){
  Serial.begin(115200);lcd.init();
  apName="CubicOS-"+String(ESP.getChipId(),HEX);
  hostName=apName;hostName.toLowerCase();
  loadAccessPointPassword();
  applyDisplaySettings();
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);WiFi.softAP(apName.c_str(),apPassword.c_str());
  WiFi.hostname(hostName);
  WiFi.setAutoReconnect(true);
  if(WiFi.SSID().length())WiFi.begin();
  setupWeb();draw(millis(),time(nullptr));
  Serial.print("AP SSID: ");Serial.println(apName);
  Serial.print("AP Password: ");Serial.println(apPassword);
}

void loop(){
  web.handleClient();
  serviceWiFi();
  if(settings.cycleSeconds&&millis()-pageSince>=uint32_t(settings.cycleSeconds)*1000)selectPage((page+1)%5);
  uint32_t period=page==2?180:FRAME_PERIOD_MS;
  if(lastFrame==0||millis()-lastFrame>=period){lastFrame=millis();draw(lastFrame,time(nullptr));}
  delay(2);
}
