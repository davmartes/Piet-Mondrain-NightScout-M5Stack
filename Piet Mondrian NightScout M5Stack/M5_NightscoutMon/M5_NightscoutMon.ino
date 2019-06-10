/*  M5Stack Nightscout monitor
    Copyright (C) 2018, 2019 Martin Lukasek <martin@lukasek.cz>
    CUSTOMIZED VERSION WITH BIGGER TIME AGO, DELTA, CLOCK. NO STARTUP MUSIC. NO BACKGROUND NOISE. SMALLER ARROW. SNOOZE BAR DIFFERENT LOCATION. 
    
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>. 
    
    This software uses some 3rd party libraries:
    IniFile by Steve Marple <stevemarple@googlemail.com> (GNU LGPL v2.1)
    ArduinoJson by Benoit BLANCHON (MIT License) 
    IoT Icon Set by Artur Funk (GPL v3)

    PIET MONDRAIN ARTWORK CUSTOMIZATION

*/


#include <M5Stack.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include "time.h"
#define ARDUINOJSON_USE_LONG_LONG 1
#include <ArduinoJson.h>
#include "Free_Fonts.h"
#include "IniFile.h"

struct tConfig {
  char url[32];
  char bootPic[64];
  char userName[32];
  int timeZone = 3600; // time zone offset in hours, must be corrected for internatinal use and DST
  int dst = 0; // DST time offset in hours, must be corrected for internatinal use and DST
  int show_mgdl = 1;
  float yellow_low = 60;
  float yellow_high = 180;
  float red_low = 40;
  float red_high = 200;
  float snd_warning = 50;
  float snd_alarm = 40;
  uint8_t brightness1, brightness2, brightness3;
  char wlan1ssid[32];
  char wlan1pass[32];
  char wlan2ssid[32];
  char wlan2pass[32];
  char wlan3ssid[32];
  char wlan3pass[32];
} ;

tConfig cfg;

extern const unsigned char gImage_logoM5[];
extern const unsigned char m5stack_startup_music[];

const char* ntpServer = "pool.ntp.org";

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

WiFiMulti WiFiMulti;
unsigned long msCount;
static uint8_t lcdBrightness = 2;
static char *iniFilename = "/M5NS.INI";

DynamicJsonDocument JSONdoc(16384);
float last10sgv[10];

void startupLogo() {
  static uint8_t brightness, pre_brightness;
  uint32_t length = strlen((char*)m5stack_startup_music);
  M5.Lcd.setBrightness(0);
  if (cfg.bootPic[0] == 0)
    M5.Lcd.pushImage(0, 0, 320, 240, (uint16_t *)gImage_logoM5);
  else
    M5.Lcd.drawJpgFile(SD, cfg.bootPic);
  M5.Lcd.setBrightness(100);
  M5.update();
//  M5.Speaker.playMusic(m5stack_startup_music, 25000);
  delay(1000);
  /*
    for(int i=0; i<length; i++) {
      dacWrite(SPEAKER_PIN, m5stack_startup_music[i]>>2);
      delayMicroseconds(40);
      brightness = (i/157);
      if(pre_brightness != brightness) {
          pre_brightness = brightness;
          M5.Lcd.setBrightness(brightness);
      }
    }

    for(int i=255; i>=0; i--) {
      M5.Lcd.setBrightness(i);
      if(i<=32) {
          // dacWrite(SPEAKER_PIN, i);
      }
      delay(2);
    }
  */
  M5.Lcd.fillScreen(BLACK);
  delay(800);
  for (int i = 0; i >= 100; i++) {
    M5.Lcd.setBrightness(i);
    delay(2);
  }
}

void printLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("NO TIME");
    M5.Lcd.println("NO TIME");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  M5.Lcd.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

void sndAlarm() {
  M5.Speaker.setVolume(8);
  // M5.Speaker.beep(); //beep
  M5.Speaker.update();
  for (int j = 0; j < 6; j++) {
    M5.Speaker.tone(660, 400);
    for (int i = 0; i < 600; i++) {
      delay(1);
      M5.update();
    }
  }
  //M5.Speaker.playMusic(m5stack_startup_music, 25000);
  M5.Speaker.mute();
  M5.Speaker.update();
}

void sndWarning() {
  M5.Speaker.setVolume(4);
  M5.Speaker.update();
  M5.Speaker.tone(3000, 100);
  for (int i = 0; i < 400; i++) {
    delay(1);
    M5.update();
  }
  M5.Speaker.tone(3000, 100);
  for (int i = 0; i < 400; i++) {
    delay(1);
    M5.update();
  }
  M5.Speaker.tone(3000, 100);
  for (int i = 0; i < 400; i++) {
    delay(1);
    M5.update();
  }
  M5.Speaker.mute();
  M5.Speaker.update();
}

void buttons_test() {
  if (M5.BtnA.wasPressed()) {
    // M5.Lcd.printf("A");
    Serial.printf("A");
    // sndWarning();
  }

  if (M5.BtnB.wasPressed()) {
    // M5.Lcd.printf("B");
    Serial.printf("B");
    // sndAlarm();
    if (lcdBrightness == cfg.brightness1)
      lcdBrightness = cfg.brightness2;
    else if (lcdBrightness == cfg.brightness2)
      lcdBrightness = cfg.brightness3;
    else
      lcdBrightness = cfg.brightness1;
    M5.Lcd.setBrightness(lcdBrightness);
  }

  if (M5.BtnC.wasPressed()) {
    // M5.Lcd.printf("C");
    Serial.printf("C");
    M5.setWakeupButton(BUTTON_A_PIN);
    M5.powerOFF();
  }
}

void wifi_connect() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  Serial.println("Conexion a Internet:");
  M5.Lcd.println("Conexion a Internet:");

  // We start by connecting to a WiFi network
  if (cfg.wlan1ssid[0] != 0)
    WiFiMulti.addAP(cfg.wlan1ssid, cfg.wlan1pass);
  if (cfg.wlan2ssid[0] != 0)
    WiFiMulti.addAP(cfg.wlan2ssid, cfg.wlan2pass);
  if (cfg.wlan3ssid[0] != 0)
    WiFiMulti.addAP(cfg.wlan3ssid, cfg.wlan3pass);

  Serial.println();
  M5.Lcd.println("");
  Serial.print("Buscando WiFi...                   ");
  M5.Lcd.print("Buscando WiFi...                   ");

  while (WiFiMulti.run() != WL_CONNECTED) {
    Serial.print(".");
    M5.Lcd.print(".");
    delay(500);
  }

  Serial.println("");
  M5.Lcd.println("");
  Serial.println("CONECTADO                              ");
  M5.Lcd.println("CONECTADO                              ");
  Serial.println("IP: ");
  M5.Lcd.println("IP: ");
  Serial.println(WiFi.localIP());
  M5.Lcd.println(WiFi.localIP());

  configTime(cfg.timeZone, cfg.dst, ntpServer);
  delay(1000);
  printLocalTime();

  Serial.println("CONECTADO                              ");
  M5.Lcd.println("CONECTADO                              ");
}

void printErrorMessage(uint8_t e, bool eol = true)
{
  switch (e) {
    case IniFile::errorNoError:
      Serial.print("OK");
      break;
    case IniFile::errorFileNotFound:
      Serial.print("ARCHIVO NO ENCONTRADO");
      break;
    case IniFile::errorFileNotOpen:
      Serial.print("ARCHIVO NO ABIERTO");
      break;
    case IniFile::errorBufferTooSmall:
      Serial.print("BUFFER PEQUEÑO");
      break;
    case IniFile::errorSeekError:
      Serial.print("BUSCAR ERROR");
      break;
    case IniFile::errorSectionNotFound:
      Serial.print("SECCION NO ENCONTRADA");
      break;
    case IniFile::errorKeyNotFound:
      Serial.print("CONTRASEÑA NO ENCONTRADA");
      break;
    case IniFile::errorEndOfFile:
      Serial.print("FIN DE ARCHIVO");
      break;
    case IniFile::errorUnknownError:
      Serial.print("ERROR DESCONOCIDO");
      break;
    default:
      Serial.print("VALOR DE ERROR DESCONOCIDO");
      break;
  }
  if (eol)
    Serial.println();
}

void readConfiguration() {
  const size_t bufferLen = 80;
  char buffer[bufferLen];

  IniFile ini(iniFilename); //(uint8_t)"/M5NS.INI"

  if (!ini.open()) {
    Serial.print("Ini file ");
    Serial.print(iniFilename);
    Serial.println(" does not exist");
    M5.Lcd.println("No INI file");
    // Cannot do anything else
    while (1)
      ;
  }
  Serial.println("Ini file exists");

  // Check the file is valid. This can be used to warn if any lines
  // are longer than the buffer.
  if (!ini.validate(buffer, bufferLen)) {
    Serial.print("ini file ");
    Serial.print(ini.getFilename());
    Serial.print(" not valid: ");
    printErrorMessage(ini.getError());
    // Cannot do anything else
    M5.Lcd.println("ERROR ARCHIVO INI");
    while (1)
      ;
  }

  // Fetch a value from a key which is present
  if (ini.getValue("config", "nightscout", buffer, bufferLen)) {
    Serial.print("section 'config' has an entry 'nightscout' with value ");
    Serial.println(buffer);
    strcpy(cfg.url, buffer);
  }
  else {
    Serial.print("Could not read 'nightscout' from section 'config', error was ");
    printErrorMessage(ini.getError());
    M5.Lcd.println("No URL in INI file");
    while (1)
      ;
  }

  if (ini.getValue("config", "bootpic", buffer, bufferLen)) {
    Serial.print("bootpic = ");
    Serial.println(buffer);
    strcpy(cfg.bootPic, buffer);
  }
  else {
    Serial.println("NO bootpic");
    cfg.bootPic[0] = 0;
  }

  if (ini.getValue("config", "name", buffer, bufferLen)) {
    Serial.print("name = ");
    Serial.println(buffer);
    strcpy(cfg.userName, buffer);
  }
  else {
    Serial.println("NO user name");
    strcpy(cfg.userName, " ");
  }

  if (ini.getValue("config", "time_zone", buffer, bufferLen)) {
    Serial.print("time_zone = ");
    cfg.timeZone = atoi(buffer);
    Serial.println(cfg.timeZone);
  }
  else {
    Serial.println("NO time zone defined -> Central Europe");
    cfg.timeZone = 3600;
  }

  if (ini.getValue("config", "dst", buffer, bufferLen)) {
    Serial.print("dst = ");
    cfg.dst = atoi(buffer);
    Serial.println(cfg.dst);
  }
  else {
    Serial.println("NO DST defined -> summer time");
    cfg.dst = 3600;
  }

  if (ini.getValue("config", "show_mgdl", buffer, bufferLen)) {
    Serial.print("show_mgdl = ");
    cfg.show_mgdl = atoi(buffer);
    Serial.println(cfg.show_mgdl);
  }
  else {
    Serial.println("NO show_mgdl defined -> 0 = show mmol/L");
    cfg.show_mgdl = 0;
  }


  if (ini.getValue("config", "yellow_low", buffer, bufferLen)) {
    Serial.print("yellow_low = ");
    cfg.yellow_low = atof(buffer);
    if ( cfg.show_mgdl )
      cfg.yellow_low /= 18.0;
    Serial.println(cfg.yellow_low);
  }
  else {
    Serial.println("NO yellow_low defined");
    cfg.yellow_low = 4.5;
  }

  if (ini.getValue("config", "yellow_high", buffer, bufferLen)) {
    Serial.print("yellow_high = ");
    cfg.yellow_high = atof(buffer);
    if ( cfg.show_mgdl )
      cfg.yellow_high /= 18.0;
    Serial.println(cfg.yellow_high);
  }
  else {
    Serial.println("NO yellow_high defined");
    cfg.yellow_high = 9.0;
  }

  if (ini.getValue("config", "red_low", buffer, bufferLen)) {
    Serial.print("red_low = ");
    cfg.red_low = atof(buffer);
    if ( cfg.show_mgdl )
      cfg.red_low /= 18.0;
    Serial.println(cfg.red_low);
  }
  else {
    Serial.println("NO red_low defined");
    cfg.red_low = 3.9;
  }

  if (ini.getValue("config", "red_high", buffer, bufferLen)) {
    Serial.print("red_high = ");
    cfg.red_high = atof(buffer);
    if ( cfg.show_mgdl )
      cfg.red_high /= 18.0;
    Serial.println(cfg.red_high);
  }
  else {
    Serial.println("NO red_high defined");
    cfg.red_high = 9.0;
  }

  if (ini.getValue("config", "snd_warning", buffer, bufferLen)) {
    Serial.print("snd_warning = ");
    cfg.snd_warning = atof(buffer);
    if ( cfg.show_mgdl )
      cfg.snd_warning /= 18.0;
    Serial.println(cfg.snd_warning);
  }
  else {
    Serial.println("NO snd_warning defined");
    cfg.snd_warning = 3.7;
  }

  if (ini.getValue("config", "snd_alarm", buffer, bufferLen)) {
    Serial.print("snd_alarm = ");
    cfg.snd_alarm = atof(buffer);
    if ( cfg.show_mgdl )
      cfg.snd_alarm /= 18.0;
    Serial.println(cfg.snd_alarm);
  }
  else {
    Serial.println("NO snd_alarm defined");
    cfg.snd_alarm = 3.0;
  }

  if (ini.getValue("config", "brightness1", buffer, bufferLen)) {
    Serial.print("brightness1 = ");
    Serial.println(buffer);
    cfg.brightness1 = atoi(buffer);
    if (cfg.brightness1 < 1 || cfg.brightness1 > 100)
      cfg.brightness1 = 50;
  }
  else {
    Serial.println("NO brightness1");
    cfg.brightness1 = 50;                                               // brightness 1 = 50%
  }

  if (ini.getValue("config", "brightness2", buffer, bufferLen)) {
    Serial.print("brightness2 = ");
    Serial.println(buffer);
    cfg.brightness2 = atoi(buffer);
    if (cfg.brightness2 < 1 || cfg.brightness2 > 100)
      cfg.brightness2 = 100;                                            // brightness 2 = 100%
  }
  else {
    Serial.println("NO brightness2");
    cfg.brightness2 = 100;
  }
  if (ini.getValue("config", "brightness3", buffer, bufferLen)) {
    Serial.print("brightness3 = ");
    Serial.println(buffer);
    cfg.brightness3 = atoi(buffer);
    if (cfg.brightness3 < 1 || cfg.brightness3 > 100)
      cfg.brightness3 = 2;                                              // brightness 3 = 2%
  }
  else {
    Serial.println("NO brightness3");
    cfg.brightness3 = 2;
  }

  if (ini.getValue("wlan1", "ssid", buffer, bufferLen)) {
    Serial.print("wlan1ssid = ");
    Serial.println(buffer);
    strcpy(cfg.wlan1ssid, buffer);
  }
  else {
    Serial.println("NO wlan1 ssid");
    cfg.wlan1ssid[0] = 0;
  }

  if (ini.getValue("wlan1", "pass", buffer, bufferLen)) {
    Serial.print("wlan1pass = ");
    Serial.println(buffer);
    strcpy(cfg.wlan1pass, buffer);
  }
  else {
    Serial.println("NO wlan1 pass");
    cfg.wlan1pass[0] = 0;
  }

  if (ini.getValue("wlan2", "ssid", buffer, bufferLen)) {
    Serial.print("wlan2ssid = ");
    Serial.println(buffer);
    strcpy(cfg.wlan2ssid, buffer);
  }
  else {
    Serial.println("NO wlan2 ssid");
    cfg.wlan2ssid[0] = 0;
  }

  if (ini.getValue("wlan2", "pass", buffer, bufferLen)) {
    Serial.print("wlan2pass = ");
    Serial.println(buffer);
    strcpy(cfg.wlan2pass, buffer);
  }
  else {
    Serial.println("NO wlan2 pass");
    cfg.wlan2pass[0] = 0;
  }

  if (ini.getValue("wlan3", "ssid", buffer, bufferLen)) {
    Serial.print("wlan3ssid = ");
    Serial.println(buffer);
    strcpy(cfg.wlan3ssid, buffer);
  }
  else {
    Serial.println("NO wlan3 ssid");
    cfg.wlan3ssid[0] = 0;
  }

  if (ini.getValue("wlan3", "pass", buffer, bufferLen)) {
    Serial.print("wlan3pass = ");
    Serial.println(buffer);
    strcpy(cfg.wlan3pass, buffer);
  }
  else {
    Serial.println("NO wlan3 pass");
    cfg.wlan3pass[0] = 0;
  }
}



int8_t getBatteryLevel()                    // battery
{
  Wire.beginTransmission(0x75);
  Wire.write(0x78);
  if (Wire.endTransmission(false) == 0
      && Wire.requestFrom(0x75, 1)) {
    switch (Wire.read() & 0xF0) {
      case 0xE0: return 1;
      case 0xC0: return 2;
      case 0x80: return 3;
      case 0x00: return 4;
      default: return 0;
    }
  }
  return -1;
}



// the setup routine runs once when M5Stack starts up
void setup() {
  // initialize the M5Stack object
  M5.begin();
//  M5.Lcd.setRotation(3);                            // SCREEN ORIENTATION 1=landscape 2=cable at bottom 3=upside down 4=mirror portrait 5=mirror upside down 6=mirror portrait 7=mirror landscape 8=cable on top 9=landscape

  // prevent button A "ghost" random presses
  Wire.begin();



  // M5.Speaker.mute();

  // Lcd display
  M5.Lcd.setBrightness(100);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextSize(2);
  yield();

  Serial.println(ESP.getFreeHeap());

  readConfiguration();
  lcdBrightness = cfg.brightness1;
  M5.Lcd.setBrightness(lcdBrightness);

  startupLogo();
  //M5.Speaker.mute();
  yield();

  M5.Lcd.setBrightness(lcdBrightness);
  wifi_connect();
  yield();

  M5.Lcd.setBrightness(lcdBrightness);
  M5.Lcd.fillScreen(BLACK);

  // update glycemia now
  msCount = millis() - 16000;


}

void arrow(int x, int y, int asize, int aangle, int pwidth, int plength, uint16_t color) {
  float dx = (asize - 10) * cos(aangle - 90) * PI / 180 + x; // calculate X position
  float dy = (asize - 10) * sin(aangle - 90) * PI / 180 + y; // calculate Y position
  float x1 = 0;         float y1 = plength;
  float x2 = pwidth / 2;  float y2 = pwidth / 2;
  float x3 = -pwidth / 2; float y3 = pwidth / 2;
  float angle = aangle * PI / 180 - 135;
  float xx1 = x1 * cos(angle) - y1 * sin(angle) + dx;
  float yy1 = y1 * cos(angle) + x1 * sin(angle) + dy;
  float xx2 = x2 * cos(angle) - y2 * sin(angle) + dx;
  float yy2 = y2 * cos(angle) + x2 * sin(angle) + dy;
  float xx3 = x3 * cos(angle) - y3 * sin(angle) + dx;
  float yy3 = y3 * cos(angle) + x3 * sin(angle) + dy;
  M5.Lcd.fillTriangle(xx1, yy1, xx3, yy3, xx2, yy2, color);
  M5.Lcd.drawLine(x, y, xx1, yy1, color);
  M5.Lcd.drawLine(x + 1, y, xx1 + 1, yy1, color);
  M5.Lcd.drawLine(x, y + 1, xx1, yy1 + 1, color);
  M5.Lcd.drawLine(x - 1, y, xx1 - 1, yy1, color);
  M5.Lcd.drawLine(x, y - 1, xx1, yy1 - 1, color);
  M5.Lcd.drawLine(x + 2, y, xx1 + 2, yy1, color);
  M5.Lcd.drawLine(x, y + 2, xx1, yy1 + 2, color);
  M5.Lcd.drawLine(x - 2, y, xx1 - 2, yy1, color);
  M5.Lcd.drawLine(x, y - 2, xx1, yy1 - 2, color);
}



// GRAPH SECTION



void drawMiniGraph() {
  /*
    // draw help lines
    for(int i=0; i<320; i+=40) {
    M5.Lcd.drawLine(i, 0, i, 240, 0x333333);
    }
    for(int i=0; i<240; i+=30) {
    M5.Lcd.drawLine(0, i, 320, i, 0x333333);
    }
    M5.Lcd.drawLine(0, 120, 320, 120, TFT_DARKGREY);
    M5.Lcd.drawLine(160, 0, 160, 240, TFT_DARKGREY);
  */
  int i;
  float glk;
  uint16_t sgvColor;
  // M5.Lcd.drawLine(231, 110, 319, 110, 0x333333);       // (x start, y start, x finish, y finish)
  // M5.Lcd.drawLine(231, 110, 231, 207, 0x333333);
  // M5.Lcd.drawLine(231, 207, 319, 207, 0x333333);
  // M5.Lcd.drawLine(319, 110, 319, 207, 0x333333);
//  M5.Lcd.drawLine(185, 113, 319, 113, 0x333333);                                                 // TOP LINE
//  M5.Lcd.drawLine(231, 175, 319, 175, 0x333333);                                                 // TARGET LINE
//  M5.Lcd.drawLine(231, 203, 319, 203, 0x333333);                                                 // LOW LINE
  // M5.Lcd.drawLine(185, 230, 319, 230, 0x333333);                                                 // BOTTOM LINE
  // M5.Lcd.drawLine(231, 200 - (4 - 3) * 10 + 3, 319, 200 - (4 - 3) * 10 + 3, TFT_DARKGREY);
  // M5.Lcd.drawLine(215, 200 - (9 - 3) * 10 + 3, 319, 200 - (9 - 3) * 10 + 3, TFT_YELLOW);
//  M5.Lcd.drawLine(231, 134, 319, 134, 0x333333);                                                 // HIGH LINE
  Serial.print("Last 10 values: ");
  for (i = 9; i >= 0; i--) {
    sgvColor = 0x333333;
    glk = *(last10sgv + 9 - i);
    if (glk > 12) {
      glk = 12;
    } else {
      if (glk < 3) {
        glk = 3;
      }
    }
    if (glk < cfg.red_low || glk > cfg.red_high) {
      sgvColor = TFT_DARKGREY;
    } else {
      if (glk < cfg.yellow_low || glk > cfg.yellow_high) {
        sgvColor = 0x333333;
      }
    }
    Serial.print(*(last10sgv + i)); Serial.print(" ");
//    M5.Lcd.fillCircle(234 + i * 9, 203 - (glk - 3.0) * 10.0, 3, sgvColor);
  }
  Serial.println();
}
void update_glycemia() {
  M5.Lcd.setTextDatum(TL_DATUM);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(0, 0);
  // uint16_t maxWidth, uint16_t maxHeight, uint16_t offX, uint16_t offY, jpeg_div_t scale);
  if ((WiFiMulti.run() == WL_CONNECTED)) {

    HTTPClient http;

    Serial.print("[HTTP] begin...\n");
    // configure target server and url
    char NSurl[128];
    strcpy(NSurl, "https://");
    strcat(NSurl, cfg.url);
    strcat(NSurl, "/api/v1/entries.json");
    http.begin(NSurl); //HTTP

    Serial.print("[HTTP] GET...\n");
    // start connection and send HTTP header
    int httpCode = http.GET();

    // httpCode will be negative on error
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      Serial.printf("[HTTP] GET... code: %d\n", httpCode);

      // file found at server
      if (httpCode == HTTP_CODE_OK) {
        String json = http.getString();
        // Serial.println(json);
        // const size_t capacity = JSON_ARRAY_SIZE(10) + 10*JSON_OBJECT_SIZE(19) + 3840;
        // Serial.print("JSON size needed= "); Serial.print(capacity);
        Serial.print("Free Heap = "); Serial.println(ESP.getFreeHeap());
        auto JSONerr = deserializeJson(JSONdoc, json);
        if (JSONerr) {   //Check for errors in parsing
          Serial.println("JSON parsing failed");
          M5.Lcd.setFreeFont(FSSB12);
          M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
          M5.Lcd.drawString("JSON parsing failed", 0, 0, GFXFF);
        } else {
          char sensDev[64];
          strlcpy(sensDev, JSONdoc[0]["device"] | "N/A", 64);
          // char sensDT[64];
          // strlcpy(sensDT, JSONdoc[0]["dateString"] | "N/A", 64);
          uint64_t rawtime = 0;
          rawtime = JSONdoc[0]["date"].as<long long>(); // sensTime is time in milliseconds since 1970, something like 1555229938118
          time_t sensTime = rawtime / 1000; // no milliseconds, since 2000 would be - 946684800, but ok
          char sensDir[32];
          strlcpy(sensDir, JSONdoc[0]["direction"] | "N/A", 32);
          for (int i = 0; i <= 9; i++) {
            last10sgv[i] = JSONdoc[i]["sgv"];
            last10sgv[i] /= 18.0;
          }
          float sensSgv = JSONdoc[0]["sgv"]; //Get value of sensor measurement
          float sensSgvMgDl = sensSgv;
          // internally we work in mmol/L
          sensSgv /= 18.0;

          char tmpstr[255];
          struct tm sensTm;
          localtime_r(&sensTime, &sensTm);

          Serial.print("sensDev = ");
          Serial.println(sensDev);
          Serial.print("sensTime = ");
          Serial.print(sensTime);
          sprintf(tmpstr, " (JSON %lld)", (long long) rawtime);
          Serial.print(tmpstr);
          sprintf(tmpstr, " = %s", ctime(&sensTime));
          Serial.print(tmpstr);
          Serial.print("sensSgv = ");
          Serial.println(sensSgv);
          Serial.print("sensDir = ");
          Serial.println(sensDir);




          // CLOCK SECTION



          // Serial.print(sensTm.tm_year+1900); Serial.print(" / "); Serial.print(sensTm.tm_mon+1); Serial.print(" / "); Serial.println(sensTm.tm_mday);
//          Serial.print("Sensor: "); Serial.print(sensTm.tm_hour); Serial.print(":"); Serial.print(sensTm.tm_min); Serial.print(":"); Serial.print(sensTm.tm_sec); Serial.print(" DST "); Serial.println(sensTm.tm_isdst);

//          M5.Lcd.fillRoundRect(0, 0, 200, 100, 0, TFT_BLACK);      // MASK RECTANGLE  (x, y, widht, height, corner)
//          M5.Lcd.setFreeFont(FSSB24);
//          M5.Lcd.setTextSize(2);
//          M5.Lcd.setTextColor(TFT_ORANGE, TFT_BLACK);
//          char timeStr[30];
//          sprintf(timeStr, "%02d:%02d", sensTm.tm_hour, sensTm.tm_min);
//          M5.Lcd.drawString(timeStr, 2, 0, GFXFF);




//          M5.Lcd.fillRoundRect(0, 0, 250, 100, 0, TFT_BLACK);      // MASK RECTANGLE  (x, y, widht, height, corner)
//          M5.Lcd.setFreeFont(FSSB24);
//          M5.Lcd.setTextSize(2);
//          M5.Lcd.setTextColor(TFT_ORANGE, TFT_BLACK);
//          M5.Lcd.setCursor(0, 60);
//          struct tm timeinfo;
//.          Serial.print("LOCAL: "); Serial.print(timeinfo.tm_year+1900); Serial.print(" / "); Serial.print(timeinfo.tm_mon+1); Serial.print(" / "); Serial.println(timeinfo.tm_mday);
//.          Serial.print("Local: "); Serial.print(timeinfo.tm_hour); Serial.print(":"); Serial.print(timeinfo.tm_min); Serial.print(":"); Serial.print(timeinfo.tm_sec); Serial.print(" DST "); Serial.println(timeinfo.tm_isdst);
//.          sensorDifSec=difftime(mktime(&timeinfo), sensTime);
          
          //M5.Lcd.print("Time: ");
//          char timeStr[30];
          //sprintf(timeStr, "%02d:%02d:%02d", sensTm.tm_hour, sensTm.tm_min, sensTm.tm_sec);
//          sprintf(timeStr, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
//          M5.Lcd.print(timeStr);



          
          // Serial.print(sensTm.tm_year+1900); Serial.print(" / "); Serial.print(sensTm.tm_mon+1); Serial.print(" / "); Serial.println(sensTm.tm_mday);
          //Serial.print("Sensor: "); Serial.print(sensTm.tm_hour); Serial.print(":"); Serial.print(sensTm.tm_min); Serial.print(":"); Serial.print(sensTm.tm_sec); Serial.print(" DST "); Serial.println(sensTm.tm_isdst);

          //char timeStr[30];
          //sprintf(timeStr, "%02d:%02d", sensTm.tm_hour, sensTm.tm_min);
          //M5.Lcd.drawString(timeStr, 0, 0, GFXFF);





// BG SECTION



          uint16_t glColor = TFT_BLACK;
          if (sensSgv < cfg.yellow_low || sensSgv > cfg.yellow_high) {
            glColor = TFT_ORANGE; // warning is YELLOW
          }
          if (sensSgv < cfg.red_low || sensSgv > cfg.red_high) {
            glColor = TFT_RED; // alert is RED
          }

          char glykStr[128];
          sprintf(glykStr, "Glyk: %4.1f %s", sensSgv, sensDir);
          Serial.println(glykStr);
          // M5.Lcd.println(glykStr);

          M5.Lcd.fillRect(0, 0, 500, 120, TFT_WHITE);  // MASK RECTANGLE BG
          M5.Lcd.setTextSize(3);
          M5.Lcd.setTextDatum(TL_DATUM);
          M5.Lcd.setTextColor(glColor, TFT_WHITE);
          char sensSgvStr[30];
          int smaller_font = 0;
          if ( cfg.show_mgdl ) {
            sprintf(sensSgvStr, "%3.0f", sensSgvMgDl);
          } else {
            sprintf(sensSgvStr, "%4.1f", sensSgv);
            if ( sensSgvStr[0] != ' ' )
              smaller_font = 1;
          }
          // Serial.print("SGV string length = "); Serial.print(strlen(sensSgvStr));
          // Serial.print(", smaller_font = "); Serial.println(smaller_font);
          if ( smaller_font ) {
            M5.Lcd.setFreeFont(FSSB18);
            M5.Lcd.drawString(sensSgvStr, 95, 0, GFXFF);     // 50, 25,
          } else {
            M5.Lcd.setFreeFont(FSSB24);
            M5.Lcd.drawString(sensSgvStr, 75, 0, GFXFF);     // 30, 25,
          }
          int tw = M5.Lcd.textWidth(sensSgvStr);
          int th = M5.Lcd.fontHeight(GFXFF);
          // Serial.print("textWidth="); Serial.println(tw);
          // Serial.print("textHeight="); Serial.println(th);

          /*
            M5.Lcd.setTextSize(1);
            M5.Lcd.setFreeFont(FSSB24);
            M5.Lcd.setTextColor(glColor, TFT_BLACK);
            M5.Lcd.setCursor(0, 160+2*24);
            M5.Lcd.println(sensDir);
          */







// TIME AGO SECTION



           // calculate sensor time difference
          int sensorDifSec = 0;
          struct tm timeinfo;
          if (!getLocalTime(&timeinfo)) {
            sensorDifSec = 24 * 60 * 60; // too much
          } else {
            // Serial.print("LOCAL: "); Serial.print(timeinfo.tm_year+1900); Serial.print(" / "); Serial.print(timeinfo.tm_mon+1); Serial.print(" / "); Serial.println(timeinfo.tm_mday);
            Serial.print("Local: "); Serial.print(timeinfo.tm_hour); Serial.print(":"); Serial.print(timeinfo.tm_min); Serial.print(":"); Serial.print(timeinfo.tm_sec); Serial.print(" DST "); Serial.println(timeinfo.tm_isdst);
            sensorDifSec = difftime(mktime(&timeinfo), sensTime);
          }
          Serial.print("Sensor time difference = "); Serial.print(sensorDifSec); Serial.println(" sec");
          unsigned int sensorDifMin = (sensorDifSec + 30) / 60;
          uint16_t tdColor = TFT_BLACK;                                 // RECTANGLE OR VALUE COLOR TIME AGO OK
          if (sensorDifMin > 5) {
            tdColor = TFT_WHITE;                                        // RECTANGLE OR VALUE COLOR BIT TOO LONG AGO
            if (sensorDifMin > 30) {
              tdColor = TFT_RED;                                         // RECTANGLE OR VALUE COLOR LONG LONG AGO 
            }
          }
//          M5.Lcd.fillRoundRect(210, 145, 155, 150, 20, tdColor);       // RECTANGLE  (x, y, widht, height, corner)   
          M5.Lcd.fillRoundRect(60, 120, 105, 200, 0, TFT_YELLOW);          // MASK RECTANGLE
          M5.Lcd.setTextSize(2);
          M5.Lcd.setFreeFont(FSSB24);
          M5.Lcd.setTextDatum(MC_DATUM);
          M5.Lcd.setTextColor(tdColor);                                  // COLOR TIME AGO NUMBERS
          if (sensorDifMin > 99) {
            M5.Lcd.drawString("X", 115, 180, GFXFF);
          } else {
            M5.Lcd.drawNumber(sensorDifMin, 115, 180, GFXFF);
          }






// DELTA SECTION



          M5.Lcd.fillRoundRect(165, 120, 200, 200, 0, TFT_BLUE);      // MASK RECTANGLE  (x, y, widht, height, corner)
//          M5.Lcd.fillRoundRect(100, 130, 55, 55, 0, TFT_BLACK);         // MASK RECTANGLE  (x, y, widht, height, corner)
          M5.Lcd.setFreeFont(FSS24);
          M5.Lcd.setTextSize(2);
          M5.Lcd.setTextColor(TFT_WHITE, TFT_BLUE);
          char diffstr[10];
          if ( cfg.show_mgdl ) {
            sprintf(diffstr, "%+3.0f", (last10sgv[0] - last10sgv[1]) * 18 );
          } else {
            sprintf(diffstr, "%+4.1f", last10sgv[0] - last10sgv[1] );
          }
          M5.Lcd.drawString(diffstr, 255, 180, GFXFF);






// ARROW SECTION



          M5.Lcd.fillRoundRect(0, 0, 60, 500, 0, TFT_RED);      // MASK RECTANGLE  (x, y, widht, height, corner)
          
          int arrowAngle = 180;
          if (strcmp(sensDir, "DoubleDown") == 0)
            arrowAngle = 90;
          else if (strcmp(sensDir, "SingleDown") == 0)
            arrowAngle = 75;
          else if (strcmp(sensDir, "FortyFiveDown") == 0)
            arrowAngle = 45;
          else if (strcmp(sensDir, "Flat") == 0)
            arrowAngle = 0;
          else if (strcmp(sensDir, "FortyFiveUp") == 0)
            arrowAngle = -45;
          else if (strcmp(sensDir, "SingleUp") == 0)
            arrowAngle = -75;
          else if (strcmp(sensDir, "DoubleUp") == 0)
            arrowAngle = -90;
          else if (strcmp(sensDir, "NONE") == 0)
            arrowAngle = 180;
          else if (strcmp(sensDir, "NOT COMPUTABLE") == 0)
            arrowAngle = 180;
          if (arrowAngle != 180)
            arrow(15, 120, 10, arrowAngle + 85, 30, 30, TFT_WHITE);      //  (x, y, ...)  glColor

          //arrow(0+tw+40, 120+40, 10, 45+85, 40, 40, TFT_RED);
          //arrow(0+tw+40, 120+40, 10, -45+85, 40, 40, TFT_BLUE);
          //arrow(0+tw+40, 120+40, 10, 135+85, 40, 40, TFT_BLACK);
          //arrow(0+tw+40, 120+40, 10, -135+85, 40, 40, TFT_ORANGE);

          M5.Lcd.setTextSize(1);
          M5.Lcd.setFreeFont(FSSB12);
          M5.Lcd.setTextColor(TFT_WHITE, TFT_RED);
          char devStr[64];
          Serial.println(last10sgv[0] * 18 - last10sgv[1] * 18);



          /*
            // draw help lines
            for(int i=0; i<320; i+=40) {
            M5.Lcd.drawLine(i, 0, i, 240, 0x333333);
            }
            for(int i=0; i<240; i+=30) {
            M5.Lcd.drawLine(0, i, 320, i, 0x333333);
            }
            M5.Lcd.drawLine(0, 120, 320, 120, TFT_DARKGREY);
            M5.Lcd.drawLine(160, 0, 160, 240, TFT_DARKGREY);
          */

          if (sensorDifSec < 23) {
            if ((sensSgv <= cfg.snd_alarm) && (sensSgv >= 0.1))
              sndAlarm();
            else if ((sensSgv <= cfg.snd_warning) && (sensSgv >= 0.1))
              sndWarning();
          }

          drawMiniGraph();
        }
      } else {
        String errstr = String("[HTTP] GET not ok, error: " + String(httpCode));
        Serial.println(errstr);
        M5.Lcd.setTextSize(1);
        M5.Lcd.setTextColor(DARKGREY);
        M5.Lcd.println(errstr);
      }
    } else {
      String errstr = String("[HTTP] GET failed, error: " + String(http.errorToString(httpCode).c_str()));
      Serial.println(errstr);
      M5.Lcd.setTextSize(1);
      M5.Lcd.setTextColor(DARKGREY);
      M5.Lcd.println(errstr);
    }

    http.end();
  }
}


// the loop routine runs over and over again forever
void loop() {
  delay(10);
  buttons_test();
  // update glycemia every 10s
  if (millis() - msCount > 15000) {
    update_glycemia();
    msCount = millis();




// BATTERY SECTION


//    delay(1000);
//    M5.Lcd.fillRoundRect(0, 220, 250, 30, 0, TFT_BLACK);      // MASK RECTANGLE  (x, y, widht, height, corner)
//    M5.Lcd.setFreeFont(FM18);
    M5.Lcd.setTextColor(TFT_ORANGE);
//    M5.Lcd.setCursor(0, 235);
//    M5.Lcd.setTextSize(1);
//    M5.Lcd.print("REMAIN BATTERY ");
//    M5.Lcd.print(getBatteryLevel());
//    M5.Lcd.setFreeFont(FM9);
//    M5.Lcd.setTextColor(ORANGE);
//    M5.Lcd.print("   HOME WEATHER STATION");                // BOTTOM TITLE
    M5.Lcd.setFreeFont(FMB18);
    M5.Lcd.setCursor(0, 20);
//    M5.Lcd.print(" MADRID   mb/%");                                  // CITY NAME
//    M5.Lcd.fillRect(150, 50, 90, 100, TFT_BLACK);         // MASK RECTANGLE IMAGE
//    M5.Lcd.drawJpgFile(SD, "/CLOUD.jpg", 0, 140);          // IMAGE 1
//    M5.Lcd.drawJpgFile(SD, "/WIND.jpg", 0, 190);           // IMAGE 2
  








//    GRIND SECTION


          M5.Lcd.drawLine(60, 120, 500, 120, TFT_BLACK);          // HORIZONTAL LINE
          M5.Lcd.drawLine(60, 121, 500, 121, TFT_BLACK);          // HORIZONTAL LINE
          M5.Lcd.drawLine(60, 122, 500, 122, TFT_BLACK);          // HORIZONTAL LINE
          M5.Lcd.drawLine(60, 123, 500, 123, TFT_BLACK);          // HORIZONTAL LINE
          M5.Lcd.drawLine(60, 124, 500, 124, TFT_BLACK);          // HORIZONTAL LINE
          M5.Lcd.drawLine(60, 125, 500, 125, TFT_BLACK);          // HORIZONTAL LINE
          M5.Lcd.drawLine(60, 126, 500, 126, TFT_BLACK);          // HORIZONTAL LINE
          M5.Lcd.drawLine(60, 127, 500, 127, TFT_BLACK);          // HORIZONTAL LINE
          M5.Lcd.drawLine(60, 128, 500, 128, TFT_BLACK);          // HORIZONTAL LINE
          M5.Lcd.drawLine(60, 129, 500, 129, TFT_BLACK);          // HORIZONTAL LINE

          M5.Lcd.drawLine(156, 120, 156, 300, TFT_BLACK);           // VERTICAL LINE
          M5.Lcd.drawLine(157, 120, 157, 300, TFT_BLACK);           // VERTICAL LINE
          M5.Lcd.drawLine(158, 120, 158, 300, TFT_BLACK);           // VERTICAL LINE
          M5.Lcd.drawLine(159, 120, 159, 300, TFT_BLACK);           // VERTICAL LINE
          M5.Lcd.drawLine(160, 120, 160, 300, TFT_BLACK);           // VERTICAL LINE
          M5.Lcd.drawLine(161, 120, 161, 300, TFT_BLACK);           // VERTICAL LINE
          M5.Lcd.drawLine(162, 120, 162, 300, TFT_BLACK);           // VERTICAL LINE
          M5.Lcd.drawLine(163, 120, 163, 300, TFT_BLACK);           // VERTICAL LINE
          M5.Lcd.drawLine(164, 120, 164, 300, TFT_BLACK);           // VERTICAL LINE
          M5.Lcd.drawLine(165, 120, 165, 300, TFT_BLACK);           // VERTICAL LINE

          M5.Lcd.drawLine(60, 0, 60, 300, TFT_BLACK);           // VERTICAL LINE
          M5.Lcd.drawLine(61, 0, 61, 300, TFT_BLACK);           // VERTICAL LINE
          M5.Lcd.drawLine(62, 0, 62, 300, TFT_BLACK);           // VERTICAL LINE
          M5.Lcd.drawLine(63, 0, 63, 300, TFT_BLACK);           // VERTICAL LINE
          M5.Lcd.drawLine(64, 0, 64, 300, TFT_BLACK);           // VERTICAL LINE
          M5.Lcd.drawLine(65, 0, 65, 300, TFT_BLACK);           // VERTICAL LINE
          M5.Lcd.drawLine(66, 0, 66, 300, TFT_BLACK);           // VERTICAL LINE
          M5.Lcd.drawLine(67, 0, 67, 300, TFT_BLACK);           // VERTICAL LINE
          M5.Lcd.drawLine(68, 0, 68, 300, TFT_BLACK);           // VERTICAL LINE
          M5.Lcd.drawLine(69, 0, 69, 300, TFT_BLACK);           // VERTICAL LINE





  
  }
  M5.update();
}
