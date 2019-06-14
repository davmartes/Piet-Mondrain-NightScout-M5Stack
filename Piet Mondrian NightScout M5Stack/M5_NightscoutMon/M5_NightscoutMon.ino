/*  M5Stack Nightscout monitor
    Copyright (C) 2018, 2019 Martin Lukasek <martin@lukasek.cz>

    PIET MONDRIAN CUSTOMIZATION 2.0

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
*/

#include <M5Stack.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include "time.h"
// #include <util/eu_dst.h>
#define ARDUINOJSON_USE_LONG_LONG 1
#include <ArduinoJson.h>
#include "Free_Fonts.h"
#include "IniFile.h"
#include "M5NSconfig.h"

tConfig cfg;

extern const unsigned char m5stack_startup_music[];
extern const unsigned char WiFi_symbol[];
extern const unsigned char alarmSndData[];

extern const unsigned char sun_icon16x16[];
extern const unsigned char clock_icon16x16[];
extern const unsigned char timer_icon16x16[];
extern const unsigned char powerbutton_icon16x16[];

extern const unsigned char bat0_icon16x16[];
extern const unsigned char bat1_icon16x16[];
extern const unsigned char bat2_icon16x16[];
extern const unsigned char bat3_icon16x16[];
extern const unsigned char bat4_icon16x16[];
extern const unsigned char plug_icon16x16[];

const char* ntpServer = "pool.ntp.org"; // "time.nist.gov", "time.google.com"
struct tm localTimeInfo;
int MAX_TIME_RETRY = 30;
int lastSec = 61;
int lastMin = 61;
char localTimeStr[30];

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

WiFiMulti WiFiMulti;
unsigned long msCount;
unsigned long msCountLog;
static uint8_t lcdBrightness = 10;
static char *iniFilename = "/M5NS.INI";

DynamicJsonDocument JSONdoc(16384);
float last10sgv[10];
int wasError = 0;
time_t lastAlarmTime = 0;
time_t lastSnoozeTime = 0;
static uint8_t music_data[25000]; // 5s in sample rate 5000 samp/s

void startupLogo() {
  static uint8_t brightness, pre_brightness;
  M5.Lcd.setBrightness(0);
  if (cfg.bootPic[0] == 0) {
    // M5.Lcd.pushImage(0, 0, 320, 240, (uint16_t *)gImage_logoM5);
    M5.Lcd.drawString("M5 Stack", 120, 60, GFXFF);
    M5.Lcd.drawString("Nightscout monitor", 60, 80, GFXFF);
    M5.Lcd.drawString("(c) 2019 Martin Lukasek", 20, 120, GFXFF);
  } else {
    M5.Lcd.drawJpgFile(SD, cfg.bootPic);
  }
  M5.Lcd.setBrightness(100);
  M5.update();
  // M5.Speaker.playMusic(m5stack_startup_music,25000);
  /*
    int avg=0;
    for(uint16_t i=0; i<40000; i++) {
    avg+=m5stack_startup_music[i];
    if(i%4 == 3) {
      music_data[i/4]=avg/4;
      avg=0;
    }
    }
    play_music_data(10000, 100);

    for(int i=0; i>=100; i++) {
      M5.Lcd.setBrightness(i);
      delay(2);
    }
  */
}

void printLocalTime() {
  if (!getLocalTime(&localTimeInfo)) {
    Serial.println("Failed to obtain time");
    M5.Lcd.println("Failed to obtain time");
    return;
  }
  Serial.println(&localTimeInfo, "%A, %B %d %Y %H:%M:%S");
  M5.Lcd.println(&localTimeInfo, "%A, %B %d %Y %H:%M:%S");
}

void play_music_data(uint32_t data_length, uint8_t volume) {
  uint8_t vol;
  if ( volume > 100 )
    vol = 1;
  else
    vol = 101 - volume;
  if (vol != 101) {
    ledcSetup(TONE_PIN_CHANNEL, 0, 13);
    ledcAttachPin(SPEAKER_PIN, TONE_PIN_CHANNEL);
    delay(10);
    for (int i = 0; i < data_length; i++) {
      dacWrite(SPEAKER_PIN, music_data[i] / vol);
      delayMicroseconds(194); // 200 = 1 000 000 microseconds / sample rate 5000
    }
    /* takes too long
      // slowly set DAC to zero from the last value
      for(int t=music_data[data_length-1]; t>=0; t--) {
      dacWrite(SPEAKER_PIN, t);
      delay(2);
      } */
    for (int t = music_data[data_length - 1] / vol; t >= 0; t--) {
      dacWrite(SPEAKER_PIN, t);
      delay(2);
    }
    // dacWrite(SPEAKER_PIN, 0);
    // delay(10);
    ledcAttachPin(SPEAKER_PIN, TONE_PIN_CHANNEL);
    ledcWriteTone(TONE_PIN_CHANNEL, 0);
    CLEAR_PERI_REG_MASK(RTC_IO_PAD_DAC1_REG, RTC_IO_PDAC1_XPD_DAC | RTC_IO_PDAC1_DAC_XPD_FORCE);
  }
}

void play_tone(uint16_t frequency, uint32_t duration, uint8_t volume) {
  // Serial.print("start fill music data "); Serial.println(millis());
  uint32_t data_length = 5000;
  if ( duration * 5 < data_length )
    data_length = duration * 5;
  float interval = 2 * M_PI * float(frequency) / float(5000);
  for (int i = 0; i < data_length; i++) {
    music_data[i] = 127 + 126 * sin(interval * i);
  }
  // Serial.print("finish fill music data "); Serial.println(millis());
  play_music_data(data_length, volume);
}

void sndAlarm() {
  for (int j = 0; j < 6; j++) {
    if ( cfg.dev_mode )
      play_tone(660, 400, 1);
    else
      play_tone(660, 400, cfg.alarm_volume);
    delay(200);
  }
}

void sndWarning() {
  for (int j = 0; j < 3; j++) {
    if ( cfg.dev_mode )
      play_tone(3000, 100, 1);
    else
      play_tone(3000, 100, cfg.warning_volume);
    delay(300);
  }
}

int tmpvol = 1;



void buttons_test() {


  if (M5.BtnA.wasPressed()) {
    // M5.Lcd.printf("A");
    Serial.printf("A");
    // play_tone(1000, 10, 1);
    // sndAlarm();
    if (lcdBrightness == cfg.brightness1)
      lcdBrightness = cfg.brightness2;
    else if (lcdBrightness == cfg.brightness2)
      lcdBrightness = cfg.brightness3;
    else
      lcdBrightness = cfg.brightness1;
    M5.Lcd.setBrightness(lcdBrightness);
  }



  if (M5.BtnB.wasPressed()) {
    Serial.printf("B");
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
      lastSnoozeTime = 0;
    } else {
      lastSnoozeTime = mktime(&timeinfo);
    }
    M5.Lcd.setTextSize(1);
    M5.Lcd.setFreeFont(FSSB12);
    M5.Lcd.fillRect(165, 220, 50, 30, TFT_WHITE);
    M5.Lcd.setTextColor(TFT_BLACK, TFT_WHITE);
    char tmpStr[10];
    sprintf(tmpStr, "%i", cfg.snooze_timeout);
    int txw = M5.Lcd.textWidth(tmpStr);
    Serial.print("Set SNOOZE: "); Serial.println(tmpStr);
    M5.Lcd.drawString(tmpStr, 203 - txw / 2, 229, GFXFF);

    M5.Lcd.drawLine(215, 210, 215, 240, TFT_BLACK);           // VERTICAL LINE
    M5.Lcd.drawLine(216, 210, 216, 240, TFT_BLACK);           // VERTICAL LINE
    M5.Lcd.drawLine(217, 210, 217, 240, TFT_BLACK);           // VERTICAL LINE
    M5.Lcd.drawLine(218, 210, 218, 240, TFT_BLACK);           // VERTICAL LINE
    M5.Lcd.drawLine(219, 210, 219, 240, TFT_BLACK);           // VERTICAL LINE
    M5.Lcd.drawLine(220, 210, 220, 240, TFT_BLACK);           // VERTICAL LINE
    M5.Lcd.drawLine(221, 210, 221, 240, TFT_BLACK);           // VERTICAL LINE
    M5.Lcd.drawLine(222, 210, 222, 240, TFT_BLACK);           // VERTICAL LINE
    M5.Lcd.drawLine(223, 210, 223, 240, TFT_BLACK);           // VERTICAL LINE
    M5.Lcd.drawLine(224, 210, 224, 240, TFT_BLACK);           // VERTICAL LINE
    M5.Lcd.drawLine(165, 210, 215, 210, TFT_BLACK);           // HORIZONTAL LINE
    M5.Lcd.drawLine(165, 211, 215, 211, TFT_BLACK);           // HORIZONTAL LINE
    M5.Lcd.drawLine(165, 212, 215, 212, TFT_BLACK);           // HORIZONTAL LINE
    M5.Lcd.drawLine(165, 213, 215, 213, TFT_BLACK);           // HORIZONTAL LINE
    M5.Lcd.drawLine(165, 214, 215, 214, TFT_BLACK);           // HORIZONTAL LINE
    M5.Lcd.drawLine(165, 215, 215, 215, TFT_BLACK);           // HORIZONTAL LINE
    M5.Lcd.drawLine(165, 216, 215, 216, TFT_BLACK);           // HORIZONTAL LINE
    M5.Lcd.drawLine(165, 217, 215, 217, TFT_BLACK);           // HORIZONTAL LINE
    M5.Lcd.drawLine(165, 218, 215, 218, TFT_BLACK);           // HORIZONTAL LINE
    M5.Lcd.drawLine(165, 219, 215, 219, TFT_BLACK);           // HORIZONTAL LINE
  }



  if (M5.BtnC.wasPressed()) {
    Serial.printf("C");
    //    M5.Lcd.fillScreen(WHITE);


    // BATTERY SECTION



    M5.Lcd.fillRoundRect(165, 130, 200, 200, 0, TFT_WHITE);      // MASK RECTANGLE  (x, y, widht, height, corner)
    M5.Lcd.setFreeFont(FSSB24);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(TFT_BLACK, TFT_WHITE);
    M5.Lcd.setCursor(164, 215);
    //    M5.Lcd.print("BATTERY STATUS: ");
    M5.Lcd.print(getBatteryLevel());
    M5.Lcd.print("%");



    // SSID SECTION


    M5.Lcd.fillRect(60, 0, 500, 56, TFT_RED);  // MASK RECTANGLE BG
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(TFT_WHITE, TFT_RED);
    M5.Lcd.setFreeFont(FSSB12);
    M5.Lcd.setCursor(72, 32);
    M5.Lcd.setTextSize(1);
//    M5.Lcd.print("WiFi:                           ");
    M5.Lcd.print(WiFi.SSID());

    //  USER SECTION

    M5.Lcd.fillRect(60, 65, 500, 55, TFT_BLACK);  // MASK RECTANGLE BG
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Lcd.setFreeFont(FSSB12);
    M5.Lcd.setTextSize(1);
    M5.Lcd.drawString(cfg.userName, 110, 90, GFXFF);    



    M5.Lcd.fillRoundRect(0, 0, 60, 500, 0, TFT_YELLOW);      // MASK RECTANGLE  (x, y, widht, height, corner)

    M5.Lcd.fillRoundRect(60, 120, 105, 200, 0, TFT_BLUE);          // MASK RECTANGLE



    M5.Lcd.drawLine(60, 56, 500, 56, TFT_BLACK);          // HORIZONTAL LINE
    M5.Lcd.drawLine(60, 57, 500, 57, TFT_BLACK);          // HORIZONTAL LINE
    M5.Lcd.drawLine(60, 58, 500, 58, TFT_BLACK);          // HORIZONTAL LINE
    M5.Lcd.drawLine(60, 59, 500, 59, TFT_BLACK);          // HORIZONTAL LINE
    M5.Lcd.drawLine(60, 60, 500, 60, TFT_BLACK);          // HORIZONTAL LINE
    M5.Lcd.drawLine(60, 61, 500, 61, TFT_BLACK);          // HORIZONTAL LINE
    M5.Lcd.drawLine(60, 62, 500, 62, TFT_BLACK);          // HORIZONTAL LINE
    M5.Lcd.drawLine(60, 63, 500, 63, TFT_BLACK);          // HORIZONTAL LINE
    M5.Lcd.drawLine(60, 64, 500, 64, TFT_BLACK);          // HORIZONTAL LINE
    M5.Lcd.drawLine(60, 65, 500, 65, TFT_BLACK);          // HORIZONTAL LINE




    delay(500);

    /*     void drawMiniGraph() {
         M5.Lcd.fillScreen(BLACK);
         int i;
         float glk;
         uint16_t sgvColor;

      M5.Lcd.drawLine(0, 0, 319, 0, TFT_RED);                                                         // TOP LINE
      M5.Lcd.drawLine(0, 25, 319, 25, TFT_YELLOW);                                                    // HIGH URGENT LINE
      M5.Lcd.drawLine(0, 50, 319, 50, TFT_GREEN);                                                     // GREEN ZONE LINE
      M5.Lcd.drawLine(0, 75, 319, 75, TFT_GREEN);                                                     // GREEN ZONE LINE
      M5.Lcd.drawLine(0, 100, 319, 100, TFT_GREEN);                                                   // GREEN ZONE LINE
      M5.Lcd.drawLine(0, 125, 319, 125, TFT_GREEN);                                                   // GREEN ZONE LINE
      M5.Lcd.drawLine(0, 150, 319, 150, TFT_GREEN);                                                   // TARGET LINE
      M5.Lcd.drawLine(0, 175, 319, 175, TFT_GREEN);                                                   // RED ZONE LINE
      M5.Lcd.drawLine(0, 200, 319, 200, TFT_GREEN);                                                   // RED ZONE LINE
      M5.Lcd.drawLine(0, 225, 319, 225, TFT_RED);                                                     // LOW LINE
      M5.Lcd.drawLine(0, 250, 319, 250, TFT_MAGENTA);                                                 // MAGENTA ZONE LINE

      Serial.print("Last 10 values: ");
      for (i = 9; i >= 0; i--) {
        sgvColor = TFT_GREEN;
        glk = *(last10sgv + 9 - i);
        if (glk > 12) {
          glk = 12;
        } else {
          if (glk < 3) {
            glk = 3;
          }
        }
        if (glk < cfg.red_low || glk > cfg.red_high) {
          sgvColor = TFT_RED;
        } else {
          if (glk < cfg.yellow_low || glk > cfg.yellow_high) {
            sgvColor = TFT_YELLOW;
          }
        }
        Serial.print(*(last10sgv + i)); Serial.print(" ");
        M5.Lcd.fillCircle(0 + i * 30, 0 - (glk - 3.0) * 10.0, 3, sgvColor);   // (x start + i * separation, y - (glk - 3.0) * 10.0, radius, color)
      }
      Serial.println();
      }

      void drawIcon(int16_t x, int16_t y, const uint8_t *bitmap, uint16_t color) {
      int16_t w = 16;
      int16_t h = 16;
      int32_t i, j, byteWidth = (w + 7) / 8;
      for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
          if (pgm_read_byte(bitmap + j * byteWidth + i / 8) & (128 >> (i & 7))) {
            M5.Lcd.drawPixel(x + i, y + j, color);

      delay(1000);

          }
        }
      }
      }

    */


  }
}

void wifi_connect() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  Serial.println("WiFi connect start");
  M5.Lcd.println("WiFi connect start");

  // We start by connecting to a WiFi network
  for (int i = 0; i <= 9; i++) {
    if ((cfg.wlanssid[i][0] != 0) && (cfg.wlanpass[i][0] != 0))
      WiFiMulti.addAP(cfg.wlanssid[i], cfg.wlanpass[i]);
  }

  Serial.println();
  M5.Lcd.println("");
  Serial.print("Wait for WiFi... ");
  M5.Lcd.print("Wait for WiFi... ");

  while (WiFiMulti.run() != WL_CONNECTED) {
    Serial.print(".");
    M5.Lcd.print(".");
    delay(500);
  }

  Serial.println("");
  M5.Lcd.println("");
  Serial.print("WiFi connected to SSID "); Serial.println(WiFi.SSID());
  M5.Lcd.print("WiFi SSID "); M5.Lcd.println(WiFi.SSID());
  Serial.println("IP address: ");
  M5.Lcd.println("IP address: ");
  Serial.println(WiFi.localIP());
  M5.Lcd.println(WiFi.localIP());

  configTime(cfg.timeZone, cfg.dst, ntpServer, "time.nist.gov", "time.google.com");
  delay(1000);
  Serial.print("Waiting for time.");
  int i = 0;
  while (!getLocalTime(&localTimeInfo)) {
    Serial.print(".");
    delay(1000);
    i++;
    if (i > MAX_TIME_RETRY) {
      Serial.print("Gave up waiting for time to have a valid value.");
      break;
    }
  }
  Serial.println();
  printLocalTime();

  Serial.println("Connection done");
  M5.Lcd.println("Connection done");
}

int8_t getBatteryLevel()
{
  Wire.beginTransmission(0x75);
  Wire.write(0x78);
  if (Wire.endTransmission(false) == 0
      && Wire.requestFrom(0x75, 1)) {
    int8_t bdata = Wire.read();
    /*
      // write battery info to logfile.txt
      File fileLog = SD.open("/logfile.txt", FILE_WRITE);
      if(!fileLog) {
      Serial.println("Cannot write to logfile.txt");
      } else {
      int pos = fileLog.seek(fileLog.size());
      struct tm timeinfo;
      getLocalTime(&timeinfo);
      fileLog.print(asctime(&timeinfo));
      fileLog.print("   Battery level: "); fileLog.println(bdata, HEX);
      fileLog.close();
      Serial.print("Log file written: "); Serial.print(asctime(&timeinfo));
      }
    */
    switch (bdata & 0xF0) {
      case 0xE0: return 25;
      case 0xC0: return 50;
      case 0x80: return 75;
      case 0x00: return 100;
      default: return 0;
    }
  }
  return -1;
}

// the setup routine runs once when M5Stack starts up
void setup() {
  // initialize the M5Stack object
  M5.begin();
  // prevent button A "ghost" random presses
  Wire.begin();
  SD.begin();

  // M5.Speaker.mute();

  // Lcd display
  M5.Lcd.setBrightness(100);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextSize(2);
  yield();

  Serial.print("Free Heap: "); Serial.println(ESP.getFreeHeap());

  uint8_t cardType = SD.cardType();

  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    M5.Lcd.println("No SD card attached");
    while (1);
  }

  Serial.print("SD Card Type: ");
  M5.Lcd.print("SD Card Type: ");
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
    M5.Lcd.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
    M5.Lcd.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
    M5.Lcd.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
    M5.Lcd.println("UNKNOWN");
  }

  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %llu MB\n", cardSize);
  M5.Lcd.printf("SD Card Size: %llu MB\n", cardSize);

  readConfiguration(iniFilename, &cfg);
  // strcpy(cfg.url, "user.herokuapp.com");
  // cfg.dev_mode = 0;
  // cfg.show_mgdl = 1;
  // cfg.show_COB_IOB = 0;
  // cfg.snd_warning = 5.5;
  // cfg.snd_alarm = 4.5;
  // cfg.snd_warning_high = 9;
  // cfg.snd_alarm_high = 14;
  // cfg.alarm_volume = 0;
  // cfg.warning_volume = 0;
  // cfg.snd_warning_at_startup = 1;
  // cfg.snd_alarm_at_startup = 1;

  // cfg.alarm_repeat = 1;
  // cfg.snooze_timeout = 2;
  // cfg.brightness1 = 0;
  // cfg.info_line = 1;

  lcdBrightness = cfg.brightness1;
  M5.Lcd.setBrightness(lcdBrightness);

  startupLogo();
  yield();

  if (cfg.snd_warning_at_startup) {
    play_tone(3000, 100, cfg.warning_volume);
    delay(500);
  }
  if (cfg.snd_alarm_at_startup) {
    play_tone(660, 400, cfg.alarm_volume);
    delay(500);
  }
  M5.Lcd.fillScreen(BLACK);

  M5.Lcd.setBrightness(lcdBrightness);
  wifi_connect();
  yield();

  M5.Lcd.setBrightness(lcdBrightness);
  M5.Lcd.fillScreen(BLACK);

  // test file with time stamps
  // msCountLog = millis()-6000;

  // update glycemia now
  msCount = millis() - 16000;
}

void drawArrow(int x, int y, int asize, int aangle, int pwidth, int plength, uint16_t color) {
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

void drawMiniGraph() {
  /*
    // draw help lines
    for(int i=0; i<320; i+=40) {
    M5.Lcd.drawLine(i, 0, i, 240, TFT_DARKGREY);
    }
    for(int i=0; i<240; i+=30) {
    M5.Lcd.drawLine(0, i, 320, i, TFT_DARKGREY);
    }
    M5.Lcd.drawLine(0, 120, 320, 120, TFT_LIGHTGREY);
    M5.Lcd.drawLine(160, 0, 160, 240, TFT_LIGHTGREY);
  */
  int i;
  float glk;
  uint16_t sgvColor;
  // M5.Lcd.drawLine(231, 110, 319, 110, TFT_DARKGREY);
  // M5.Lcd.drawLine(231, 110, 231, 207, TFT_DARKGREY);
  // M5.Lcd.drawLine(231, 207, 319, 207, TFT_DARKGREY);
  // M5.Lcd.drawLine(319, 110, 319, 207, TFT_DARKGREY);
  //  M5.Lcd.drawLine(231, 113, 319, 113, TFT_LIGHTGREY);
  //  M5.Lcd.drawLine(231, 203, 319, 203, TFT_LIGHTGREY);
  //  M5.Lcd.drawLine(231, 200-(4-3)*10+3, 319, 200-(4-3)*10+3, TFT_LIGHTGREY);
  //  M5.Lcd.drawLine(231, 200-(9-3)*10+3, 319, 200-(9-3)*10+3, TFT_LIGHTGREY);
  //  Serial.print("Last 10 values: ");
  for (i = 9; i >= 0; i--) {
    sgvColor = TFT_GREEN;
    glk = *(last10sgv + 9 - i);
    if (glk > 12) {
      glk = 12;
    } else {
      if (glk < 3) {
        glk = 3;
      }
    }
    if (glk < cfg.red_low || glk > cfg.red_high) {
      sgvColor = TFT_RED;
    } else {
      if (glk < cfg.yellow_low || glk > cfg.yellow_high) {
        sgvColor = TFT_YELLOW;
      }
    }
    //    Serial.print(*(last10sgv+i)); Serial.print(" ");
    if (*(last10sgv + 9 - i) != 0)
      M5.Lcd.fillCircle(1234 + i * 9, 1203 - (glk - 3.0) * 10.0, 3, sgvColor);
  }
  Serial.println();
}
void drawIcon(int16_t x, int16_t y, const uint8_t *bitmap, uint16_t color) {
  int16_t w = 16;
  int16_t h = 16;
  int32_t i, j, byteWidth = (w + 7) / 8;
  for (j = 0; j < h; j++) {
    for (i = 0; i < w; i++) {
      if (pgm_read_byte(bitmap + j * byteWidth + i / 8) & (128 >> (i & 7))) {
        M5.Lcd.drawPixel(x + i, y + j, color);
      }
    }
  }
}

void update_glycemia() {
  char loopInfoStr[64];
  char basalInfoStr[64];
  char tmpstr[255];

  M5.Lcd.setTextDatum(TL_DATUM);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(0, 0);
  // if there was an error, then clear whole screen, otherwise only graphic updated part
  if ( wasError ) {
    M5.Lcd.fillScreen(BLACK);
  } else {
    //    M5.Lcd.fillRect(230, 110, 90, 100, TFT_BLACK);
  }
  // M5.Lcd.drawJpgFile(SD, "/WiFi_symbol.jpg", 242, 130);
  //  M5.Lcd.drawBitmap(242, 130, 64, 48, (uint16_t *)WiFi_symbol);
  // uint16_t maxWidth, uint16_t maxHeight, uint16_t offX, uint16_t offY, jpeg_div_t scale);
  if ((WiFiMulti.run() == WL_CONNECTED)) {

    HTTPClient http;

    Serial.print("[HTTP] begin...\n");
    // configure target server and url
    char NSurl[128];
    if (strncmp(cfg.url, "http", 4))
      strcpy(NSurl, "https://");
    else
      strcpy(NSurl, "");
    strcat(NSurl, cfg.url);
    strcat(NSurl, "/api/v1/entries.json");

    // begin Peter Leimbach
    if (strlen(cfg.token) > 0) {
      strcat(NSurl, "?token=");
      strcat(NSurl, cfg.token);
    }
    // end Peter Leimbach

    Serial.print("JSON query NSurl = \'"); Serial.print(NSurl); Serial.print("\'\n");
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
        wasError = 0;
        // Serial.println(json);
        // const size_t capacity = JSON_ARRAY_SIZE(10) + 10*JSON_OBJECT_SIZE(19) + 3840;
        // Serial.print("JSON size needed= "); Serial.print(capacity);
        Serial.print("Free Heap = "); Serial.println(ESP.getFreeHeap());
        auto JSONerr = deserializeJson(JSONdoc, json);
        Serial.println("JSON deserialized OK");
        JsonArray arr = JSONdoc.as<JsonArray>();
        Serial.print("JSON array size = "); Serial.println(arr.size());
        if (JSONerr || arr.size() == 0) { //Check for errors in parsing
          if (JSONerr)
            strcpy(tmpstr, "JSON parsing failed");
          else
            strcpy(tmpstr, "No data from Nightscout");
          Serial.println(tmpstr);
          M5.Lcd.setFreeFont(FSSB12);
          M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
          M5.Lcd.drawString(tmpstr, 0, 24, GFXFF);
          wasError = 1;
        } else {
          char sensDev[64];
          uint64_t rawtime = 0;
          char sensDir[32];
          float sensSgv = 0;
          JsonObject obj;
          int sgvindex = 0;
          do {
            obj = JSONdoc[sgvindex].as<JsonObject>();
            sgvindex++;
          } while ((!obj.containsKey("sgv")) && (sgvindex < (arr.size() - 1)));
          sgvindex--;
          if (sgvindex < 0 || sgvindex > (arr.size() - 1))
            sgvindex = 0;
          strlcpy(sensDev, JSONdoc[sgvindex]["device"] | "N/A", 64);
          rawtime = JSONdoc[sgvindex]["date"].as<long long>(); // sensTime is time in milliseconds since 1970, something like 1555229938118
          strlcpy(sensDir, JSONdoc[sgvindex]["direction"] | "N/A", 32);
          sensSgv = JSONdoc[sgvindex]["sgv"]; // get value of sensor measurement
          time_t sensTime = rawtime / 1000; // no milliseconds, since 2000 would be - 946684800, but ok
          for (int i = 0; i <= 9; i++) {
            last10sgv[i] = JSONdoc[i]["sgv"];
            last10sgv[i] /= 18.0;
          }
          float sensSgvMgDl = sensSgv;
          // internally we work in mmol/L
          sensSgv /= 18.0;

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

          // Serial.print(sensTm.tm_year+1900); Serial.print(" / "); Serial.print(sensTm.tm_mon+1); Serial.print(" / "); Serial.println(sensTm.tm_mday);
          Serial.print("Sensor time: "); Serial.print(sensTm.tm_hour); Serial.print(":"); Serial.print(sensTm.tm_min); Serial.print(":"); Serial.print(sensTm.tm_sec); Serial.print(" DST "); Serial.println(sensTm.tm_isdst);

          M5.Lcd.setFreeFont(FSSB12);
          M5.Lcd.setTextSize(1);
          M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
          // char dateStr[30];
          // sprintf(dateStr, "%d.%d.%04d", sensTm.tm_mday, sensTm.tm_mon+1, sensTm.tm_year+1900);
          // M5.Lcd.drawString(dateStr, 0, 48, GFXFF);
          // char timeStr[30];
          // sprintf(timeStr, "%02d:%02d:%02d", sensTm.tm_hour, sensTm.tm_min, sensTm.tm_sec);
          // M5.Lcd.drawString(timeStr, 0, 72, GFXFF);
          //          char datetimeStr[30];
          //          struct tm timeinfo;
          //          if(cfg.show_current_time) {
          //            if(getLocalTime(&timeinfo)) {
          // sprintf(datetimeStr, "%02d:%02d:%02d  %d.%d.  ", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, timeinfo.tm_mday, timeinfo.tm_mon+1);
          //              sprintf(datetimeStr, "%02d:%02d  %d.%d.  ", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_mday, timeinfo.tm_mon+1);
          //            } else {
          // strcpy(datetimeStr, "??:??:??");
          //              strcpy(datetimeStr, "??:??");
          //            }
          //          } else {
          // sprintf(datetimeStr, "%02d:%02d:%02d  %d.%d.  ", sensTm.tm_hour, sensTm.tm_min, sensTm.tm_sec, sensTm.tm_mday, sensTm.tm_mon+1);
          //            sprintf(datetimeStr, "%02d:%02d  %d.%d.  ", sensTm.tm_hour, sensTm.tm_min, sensTm.tm_mday, sensTm.tm_mon+1);
          //          }
          //          M5.Lcd.drawString(datetimeStr, 0, 0, GFXFF);



          // draw battery status
          //          int8_t battLevel = getBatteryLevel();
          //          Serial.print("Battery level: "); Serial.println(battLevel);
          //          // sprintf(tmpstr, "%d", battLevel);
          // M5.Lcd.drawString(tmpstr, 0, 220, GFXFF);
          //          M5.Lcd.fillRect(168, 0, 16, 17, TFT_BLACK);
          //          if(battLevel!=-1) {
          //            switch(battLevel) {
          //              case 0:
          //                drawIcon(168, 1, (uint8_t*)bat0_icon16x16, TFT_RED);
          //                break;
          //              case 25:
          //                drawIcon(168, 1, (uint8_t*)bat1_icon16x16, TFT_YELLOW);
          //                break;
          //              case 50:
          //                drawIcon(168, 1, (uint8_t*)bat2_icon16x16, TFT_WHITE);
          //                break;
          //              case 75:
          //                drawIcon(168, 1, (uint8_t*)bat3_icon16x16, TFT_LIGHTGREY);
          //                break;
          //              case 100:
          //                drawIcon(168, 0, (uint8_t*)plug_icon16x16, TFT_LIGHTGREY);
          //                break;
          //            }
          //          }

          //          M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
          //          M5.Lcd.drawString(cfg.userName, 0, 24, GFXFF);

          /*
            char diffstr[10];
            if( cfg.show_mgdl ) {
              sprintf(diffstr, "%+3.0f", (last10sgv[sgvindex]-last10sgv[sgvindex+1])*18 );
            } else {
              sprintf(diffstr, "%+4.1f", last10sgv[sgvindex]-last10sgv[sgvindex+1] );
            }
            M5.Lcd.fillRect(130,24,69,23,TFT_BLACK);
            M5.Lcd.drawString(diffstr, 130, 24, GFXFF);
          */

          /*
            if( !cfg.dev_mode ) {
            M5.Lcd.drawString("Nightscout", 0, 48, GFXFF);
            } else {
            char heapstr[20];
            sprintf(heapstr, "%i free  ", ESP.getFreeHeap());
            M5.Lcd.drawString(heapstr, 0, 48, GFXFF);
            }
          */

          if (strncmp(cfg.url, "http", 4))
            strcpy(NSurl, "https://");
          else
            strcpy(NSurl, "");
          strcat(NSurl, cfg.url);
          strcat(NSurl, "/api/v2/properties/iob,cob,delta,loop,basal");

          // begin Peter Leimbach
          if (strlen(cfg.token) > 0) {
            strcat(NSurl, "&token=");
            strcat(NSurl, cfg.token);
          }
          // end Peter Leimbach
          // more info at /api/v2/properties

          Serial.print("Properties query NSurl = \'"); Serial.print(NSurl); Serial.print("\'\n");
          http.begin(NSurl); //HTTP
          Serial.print("[HTTP] GET properties...\n");
          int httpCode = http.GET();
          if (httpCode > 0) {
            Serial.printf("[HTTP] GET properties... code: %d\n", httpCode);
            if (httpCode == HTTP_CODE_OK) {
              // const char* propjson = "{\"iob\":{\"iob\":0,\"activity\":0,\"source\":\"OpenAPS\",\"device\":\"openaps://Spike iPhone 8 Plus\",\"mills\":1557613521000,\"display\":\"0\",\"displayLine\":\"IOB: 0U\"},\"cob\":{\"cob\":0,\"source\":\"OpenAPS\",\"device\":\"openaps://Spike iPhone 8 Plus\",\"mills\":1557613521000,\"treatmentCOB\":{\"decayedBy\":\"2019-05-11T23:05:00.000Z\",\"isDecaying\":0,\"carbs_hr\":20,\"rawCarbImpact\":0,\"cob\":7,\"lastCarbs\":{\"_id\":\"5cd74c26156712edb4b32455\",\"enteredBy\":\"Martin\",\"eventType\":\"Carb Correction\",\"reason\":\"\",\"carbs\":7,\"duration\":0,\"created_at\":\"2019-05-11T22:24:00.000Z\",\"mills\":1557613440000,\"mgdl\":67}},\"display\":0,\"displayLine\":\"COB: 0g\"},\"delta\":{\"absolute\":-4,\"elapsedMins\":4.999483333333333,\"interpolated\":false,\"mean5MinsAgo\":69,\"mgdl\":-4,\"scaled\":-0.2,\"display\":\"-0.2\",\"previous\":{\"mean\":69,\"last\":69,\"mills\":1557613221946,\"sgvs\":[{\"mgdl\":69,\"mills\":1557613221946,\"device\":\"MIAOMIAO\",\"direction\":\"Flat\",\"filtered\":92588,\"unfiltered\":92588,\"noise\":1,\"rssi\":100}]}}}";
              String propjson = http.getString();
              const size_t propcapacity = 16300;
              DynamicJsonDocument propdoc(propcapacity);
              Serial.println("Created the second JSON document");
              auto propJSONerr = deserializeJson(propdoc, propjson);
              if (propJSONerr) {
                Serial.println("Properties JSON parsing failed");
                M5.Lcd.fillRect(130, 24, 69, 23, TFT_BLACK);
                M5.Lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
                M5.Lcd.drawString("???", 130, 24, GFXFF);
              } else {
                Serial.println("Deserialized the second JSON and OK");
                JsonObject iob = propdoc["iob"];
                float iob_iob = iob["iob"]; // 0
                const char* iob_display = iob["display"] | "N/A"; // "0"
                const char* iob_displayLine = iob["displayLine"] | "IOB: N/A"; // "IOB: 0U"
                // Serial.println("IOB OK");

                JsonObject cob = propdoc["cob"];
                float cob_cob = cob["cob"]; // 0
                const char* cob_display = cob["display"] | "N/A"; // 0
                const char* cob_displayLine = cob["displayLine"] | "COB: N/A"; // "COB: 0g"
                // Serial.println("COB OK");

                JsonObject delta = propdoc["delta"];
                int delta_absolute = delta["absolute"]; // -4
                float delta_elapsedMins = delta["elapsedMins"]; // 4.999483333333333
                bool delta_interpolated = delta["interpolated"]; // false
                int delta_mean5MinsAgo = delta["mean5MinsAgo"]; // 69
                int delta_mgdl = delta["mgdl"]; // -4
                float delta_scaled = delta["scaled"]; // -0.2
                const char* delta_display = delta["display"] | ""; // "-0.2"
                if (cfg.show_COB_IOB) {
                  // show small delta right from name
                  M5.Lcd.setFreeFont(FSSB12);
                  M5.Lcd.setTextColor(WHITE, BLACK);
                  M5.Lcd.setTextSize(1);
                  M5.Lcd.fillRect(130, 24, 69, 23, TFT_BLACK);
                  M5.Lcd.drawString(delta_display, 130, 24, GFXFF);
                } else {
                  // show BIG delta bellow the name
                  M5.Lcd.setFreeFont(FSSB24);
                  M5.Lcd.setTextColor(TFT_LIGHTGREY, BLACK);
                  M5.Lcd.setTextSize(1);
                  M5.Lcd.fillRect(0, 48 + 10, 199, 47, TFT_BLACK);
                  M5.Lcd.drawString(delta_display, 0, 48 + 10, GFXFF);
                  M5.Lcd.setFreeFont(FSSB12);
                }
                // Serial.println("DELTA OK");

                JsonObject loop_obj = propdoc["loop"];
                JsonObject loop_display = loop_obj["display"];
                const char* loop_display_symbol = loop_display["symbol"] | "?"; // "⌁"
                const char* loop_display_code = loop_display["code"] | "N/A"; // "enacted"
                const char* loop_display_label = loop_display["label"] | "N/A"; // "Enacted"
                // Serial.println("LOOP OK");

                JsonObject basal = propdoc["basal"];
                const char* basal_display = basal["display"] | "N/A"; // "T: 0.950U"
                // Serial.println("BASAL OK");

                strlcpy(loopInfoStr, loop_display_label, 64);
                strlcpy(basalInfoStr, basal_display, 64);
                // Serial.println("LOOP copy string OK");

                if (cfg.show_COB_IOB) {
                  M5.Lcd.fillRect(0, 48, 199, 47, TFT_BLACK);
                  if (iob_iob > 0)
                    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
                  else
                    M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
                  Serial.print("iob_displayLine=\""); Serial.print(iob_displayLine); Serial.println("\"");
                  M5.Lcd.drawString(iob_displayLine, 0, 48, GFXFF);
                  // Serial.println("drawString IOB OK");
                  if (cob_cob > 0)
                    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
                  else
                    M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
                  Serial.print("cob_displayLine=\""); Serial.print(cob_displayLine); Serial.println("\"");
                  M5.Lcd.drawString(cob_displayLine, 0, 72, GFXFF);
                  // Serial.println("drawString COB OK");
                }
              }
            }
          }

          // calculate sensor time difference
          //          int sensorDifSec=0;
          //          if(!getLocalTime(&timeinfo)){
          //            sensorDifSec=24*60*60; // too much
          //          } else {
          //            Serial.print("Local time: "); Serial.print(timeinfo.tm_hour); Serial.print(":"); Serial.print(timeinfo.tm_min); Serial.print(":"); Serial.print(timeinfo.tm_sec); Serial.print(" DST "); Serial.println(timeinfo.tm_isdst);
          //            sensorDifSec=difftime(mktime(&timeinfo), sensTime);
          //          }
          //          Serial.print("Sensor time difference = "); Serial.print(sensorDifSec); Serial.println(" sec");
          //          unsigned int sensorDifMin = (sensorDifSec+30)/60;
          //          uint16_t tdColor = TFT_LIGHTGREY;
          //          if(sensorDifMin>5) {
          //            tdColor = TFT_WHITE;
          //            if(sensorDifMin>15) {
          //              tdColor = TFT_RED;
          //            }
          //          }
          //          M5.Lcd.fillRoundRect(200,0,120,90,15,tdColor);
          //          M5.Lcd.setTextSize(1);
          //          M5.Lcd.setFreeFont(FSSB24);
          //          M5.Lcd.setTextDatum(MC_DATUM);
          //          M5.Lcd.setTextColor(TFT_BLACK, tdColor);
          //          if(sensorDifMin>99) {
          //            M5.Lcd.drawString("Err", 260, 32, GFXFF);
          //          } else {
          //            M5.Lcd.drawNumber(sensorDifMin, 260, 32, GFXFF);
          //          }
          //          M5.Lcd.setTextSize(1);
          //          M5.Lcd.setFreeFont(FSSB12);
          //          M5.Lcd.setTextDatum(MC_DATUM);
          //          M5.Lcd.setTextColor(TFT_BLACK, tdColor);
          //          M5.Lcd.drawString("min", 260, 70, GFXFF);



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
            tdColor = TFT_DARKGREY;                                        // RECTANGLE OR VALUE COLOR BIT TOO LONG AGO
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
            M5.Lcd.drawNumber(sensorDifMin, 114, 180, GFXFF);
          }






          // DELTA SECTION



          M5.Lcd.fillRoundRect(165, 120, 300, 200, 0, TFT_BLUE);      // MASK RECTANGLE  (x, y, widht, height, corner)
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
          M5.Lcd.drawString(diffstr, 245, 180, GFXFF);






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
            drawArrow(15, 120, 10, arrowAngle + 85, 30, 30, TFT_WHITE);      //  (x, y, ...)  glColor

          M5.Lcd.setTextSize(1);
          M5.Lcd.setFreeFont(FSSB12);
          M5.Lcd.setTextColor(TFT_WHITE, TFT_RED);
          char devStr[64];
          Serial.println(last10sgv[0] * 18 - last10sgv[1] * 18);


          // SNOOZE SECTION


          drawMiniGraph();

          // calculate last alarm time difference
          int alarmDifSec = 1000000;
          int snoozeDifSec = 1000000;
          if (!getLocalTime(&timeinfo)) {
            alarmDifSec = 24 * 60 * 60; // too much
            snoozeDifSec = cfg.snooze_timeout * 60; // timeout
          } else {
            alarmDifSec = difftime(mktime(&timeinfo), lastAlarmTime);
            snoozeDifSec = difftime(mktime(&timeinfo), lastSnoozeTime);
            if ( snoozeDifSec > cfg.snooze_timeout * 60 )
              snoozeDifSec = cfg.snooze_timeout * 60; // timeout
          }
          Serial.print("Alarm time difference = "); Serial.print(alarmDifSec); Serial.println(" sec");
          Serial.print("Snooze time difference = "); Serial.print(snoozeDifSec); Serial.println(" sec");
          char tmpStr[10];

          if ( snoozeDifSec < cfg.snooze_timeout * 60 ) {
            sprintf(tmpStr, "%i", (cfg.snooze_timeout * 60 - snoozeDifSec + 59) / 60);
          } else {
            strcpy(tmpStr, "Snooze");
          }
          M5.Lcd.setTextSize(1);
          M5.Lcd.setFreeFont(FSSB12);
          // Serial.print("sensSgv="); Serial.print(sensSgv); Serial.print(", cfg.snd_alarm="); Serial.println(cfg.snd_alarm);
          if ((sensSgv <= cfg.snd_alarm) && (sensSgv >= 0.1)) {
            // red alarm state
            // M5.Lcd.fillRect(110, 220, 100, 20, TFT_RED);
            Serial.println("LOW");
            M5.Lcd.fillRect(165, 220, 50, 30, TFT_RED);
            M5.Lcd.setTextColor(TFT_BLACK, TFT_RED);
            M5.Lcd.drawLine(215, 210, 215, 240, TFT_BLACK);           // VERTICAL LINE
            M5.Lcd.drawLine(216, 210, 216, 240, TFT_BLACK);           // VERTICAL LINE
            M5.Lcd.drawLine(217, 210, 217, 240, TFT_BLACK);           // VERTICAL LINE
            M5.Lcd.drawLine(218, 210, 218, 240, TFT_BLACK);           // VERTICAL LINE
            M5.Lcd.drawLine(219, 210, 219, 240, TFT_BLACK);           // VERTICAL LINE
            M5.Lcd.drawLine(220, 210, 220, 240, TFT_BLACK);           // VERTICAL LINE
            M5.Lcd.drawLine(221, 210, 221, 240, TFT_BLACK);           // VERTICAL LINE
            M5.Lcd.drawLine(222, 210, 222, 240, TFT_BLACK);           // VERTICAL LINE
            M5.Lcd.drawLine(223, 210, 223, 240, TFT_BLACK);           // VERTICAL LINE
            M5.Lcd.drawLine(224, 210, 224, 240, TFT_BLACK);           // VERTICAL LINE
            M5.Lcd.drawLine(165, 210, 215, 210, TFT_BLACK);           // HORIZONTAL LINE
            M5.Lcd.drawLine(165, 211, 215, 211, TFT_BLACK);           // HORIZONTAL LINE
            M5.Lcd.drawLine(165, 212, 215, 212, TFT_BLACK);           // HORIZONTAL LINE
            M5.Lcd.drawLine(165, 213, 215, 213, TFT_BLACK);           // HORIZONTAL LINE
            M5.Lcd.drawLine(165, 214, 215, 214, TFT_BLACK);           // HORIZONTAL LINE
            M5.Lcd.drawLine(165, 215, 215, 215, TFT_BLACK);           // HORIZONTAL LINE
            M5.Lcd.drawLine(165, 216, 215, 216, TFT_BLACK);           // HORIZONTAL LINE
            M5.Lcd.drawLine(165, 217, 215, 217, TFT_BLACK);           // HORIZONTAL LINE
            M5.Lcd.drawLine(165, 218, 215, 218, TFT_BLACK);           // HORIZONTAL LINE
            M5.Lcd.drawLine(165, 219, 215, 219, TFT_BLACK);           // HORIZONTAL LINE
            int stw = M5.Lcd.textWidth(tmpStr);
            M5.Lcd.drawString(tmpStr, 170, 237, GFXFF);
            if ( (alarmDifSec > cfg.alarm_repeat * 60) && (snoozeDifSec == cfg.snooze_timeout * 60) ) {
              sndAlarm();
              lastAlarmTime = mktime(&timeinfo);
            }
          } else {
            if ((sensSgv <= cfg.snd_warning) && (sensSgv >= 0.1)) {
              // yellow warning state
              // M5.Lcd.fillRect(110, 220, 100, 20, TFT_YELLOW);
              Serial.println("LOW");
              M5.Lcd.fillRect(165, 220, 50, 30, TFT_YELLOW);
              M5.Lcd.setTextColor(TFT_BLACK, TFT_YELLOW);
              M5.Lcd.drawLine(215, 210, 215, 240, TFT_BLACK);           // VERTICAL LINE
              M5.Lcd.drawLine(216, 210, 216, 240, TFT_BLACK);           // VERTICAL LINE
              M5.Lcd.drawLine(217, 210, 217, 240, TFT_BLACK);           // VERTICAL LINE
              M5.Lcd.drawLine(218, 210, 218, 240, TFT_BLACK);           // VERTICAL LINE
              M5.Lcd.drawLine(219, 210, 219, 240, TFT_BLACK);           // VERTICAL LINE
              M5.Lcd.drawLine(220, 210, 220, 240, TFT_BLACK);           // VERTICAL LINE
              M5.Lcd.drawLine(221, 210, 221, 240, TFT_BLACK);           // VERTICAL LINE
              M5.Lcd.drawLine(222, 210, 222, 240, TFT_BLACK);           // VERTICAL LINE
              M5.Lcd.drawLine(223, 210, 223, 240, TFT_BLACK);           // VERTICAL LINE
              M5.Lcd.drawLine(224, 210, 224, 240, TFT_BLACK);           // VERTICAL LINE
              M5.Lcd.drawLine(165, 210, 215, 210, TFT_BLACK);           // HORIZONTAL LINE
              M5.Lcd.drawLine(165, 211, 215, 211, TFT_BLACK);           // HORIZONTAL LINE
              M5.Lcd.drawLine(165, 212, 215, 212, TFT_BLACK);           // HORIZONTAL LINE
              M5.Lcd.drawLine(165, 213, 215, 213, TFT_BLACK);           // HORIZONTAL LINE
              M5.Lcd.drawLine(165, 214, 215, 214, TFT_BLACK);           // HORIZONTAL LINE
              M5.Lcd.drawLine(165, 215, 215, 215, TFT_BLACK);           // HORIZONTAL LINE
              M5.Lcd.drawLine(165, 216, 215, 216, TFT_BLACK);           // HORIZONTAL LINE
              M5.Lcd.drawLine(165, 217, 215, 217, TFT_BLACK);           // HORIZONTAL LINE
              M5.Lcd.drawLine(165, 218, 215, 218, TFT_BLACK);           // HORIZONTAL LINE
              M5.Lcd.drawLine(165, 219, 215, 219, TFT_BLACK);           // HORIZONTAL LINE
              int stw = M5.Lcd.textWidth(tmpStr);
              M5.Lcd.drawString(tmpStr, 170, 237, GFXFF);
              if ( (alarmDifSec > cfg.alarm_repeat * 60) && (snoozeDifSec == cfg.snooze_timeout * 60) ) {
                sndWarning();
                lastAlarmTime = mktime(&timeinfo);
              }
            } else {
              if ( sensSgv >= cfg.snd_alarm_high ) {
                // red alarm state
                // M5.Lcd.fillRect(110, 220, 100, 20, TFT_RED);
                Serial.println("HIGH");
                M5.Lcd.fillRect(165, 220, 50, 30, TFT_RED);
                M5.Lcd.setTextColor(TFT_BLACK, TFT_RED);
                M5.Lcd.drawLine(215, 210, 215, 240, TFT_BLACK);           // VERTICAL LINE
                M5.Lcd.drawLine(216, 210, 216, 240, TFT_BLACK);           // VERTICAL LINE
                M5.Lcd.drawLine(217, 210, 217, 240, TFT_BLACK);           // VERTICAL LINE
                M5.Lcd.drawLine(218, 210, 218, 240, TFT_BLACK);           // VERTICAL LINE
                M5.Lcd.drawLine(219, 210, 219, 240, TFT_BLACK);           // VERTICAL LINE
                M5.Lcd.drawLine(220, 210, 220, 240, TFT_BLACK);           // VERTICAL LINE
                M5.Lcd.drawLine(221, 210, 221, 240, TFT_BLACK);           // VERTICAL LINE
                M5.Lcd.drawLine(222, 210, 222, 240, TFT_BLACK);           // VERTICAL LINE
                M5.Lcd.drawLine(223, 210, 223, 240, TFT_BLACK);           // VERTICAL LINE
                M5.Lcd.drawLine(224, 210, 224, 240, TFT_BLACK);           // VERTICAL LINE
                M5.Lcd.drawLine(165, 210, 215, 210, TFT_BLACK);           // HORIZONTAL LINE
                M5.Lcd.drawLine(165, 211, 215, 211, TFT_BLACK);           // HORIZONTAL LINE
                M5.Lcd.drawLine(165, 212, 215, 212, TFT_BLACK);           // HORIZONTAL LINE
                M5.Lcd.drawLine(165, 213, 215, 213, TFT_BLACK);           // HORIZONTAL LINE
                M5.Lcd.drawLine(165, 214, 215, 214, TFT_BLACK);           // HORIZONTAL LINE
                M5.Lcd.drawLine(165, 215, 215, 215, TFT_BLACK);           // HORIZONTAL LINE
                M5.Lcd.drawLine(165, 216, 215, 216, TFT_BLACK);           // HORIZONTAL LINE
                M5.Lcd.drawLine(165, 217, 215, 217, TFT_BLACK);           // HORIZONTAL LINE
                M5.Lcd.drawLine(165, 218, 215, 218, TFT_BLACK);           // HORIZONTAL LINE
                M5.Lcd.drawLine(165, 219, 215, 219, TFT_BLACK);           // HORIZONTAL LINE
                int stw = M5.Lcd.textWidth(tmpStr);
                M5.Lcd.drawString(tmpStr, 167, 237, GFXFF);
                if ( (alarmDifSec > cfg.alarm_repeat * 60) && (snoozeDifSec == cfg.snooze_timeout * 60) ) {
                  sndAlarm();
                  lastAlarmTime = mktime(&timeinfo);
                }
              } else {
                if ( sensSgv >= cfg.snd_warning_high ) {
                  // yellow warning state
                  // M5.Lcd.fillRect(110, 220, 100, 20, TFT_YELLOW);
                  Serial.println("HIGH");
                  M5.Lcd.fillRect(165, 220, 50, 30, TFT_YELLOW);
                  M5.Lcd.setTextColor(TFT_BLACK, TFT_YELLOW);
                  M5.Lcd.drawLine(215, 210, 215, 240, TFT_BLACK);           // VERTICAL LINE
                  M5.Lcd.drawLine(216, 210, 216, 240, TFT_BLACK);           // VERTICAL LINE
                  M5.Lcd.drawLine(217, 210, 217, 240, TFT_BLACK);           // VERTICAL LINE
                  M5.Lcd.drawLine(218, 210, 218, 240, TFT_BLACK);           // VERTICAL LINE
                  M5.Lcd.drawLine(219, 210, 219, 240, TFT_BLACK);           // VERTICAL LINE
                  M5.Lcd.drawLine(220, 210, 220, 240, TFT_BLACK);           // VERTICAL LINE
                  M5.Lcd.drawLine(221, 210, 221, 240, TFT_BLACK);           // VERTICAL LINE
                  M5.Lcd.drawLine(222, 210, 222, 240, TFT_BLACK);           // VERTICAL LINE
                  M5.Lcd.drawLine(223, 210, 223, 240, TFT_BLACK);           // VERTICAL LINE
                  M5.Lcd.drawLine(224, 210, 224, 240, TFT_BLACK);           // VERTICAL LINE
                  M5.Lcd.drawLine(165, 210, 215, 210, TFT_BLACK);           // HORIZONTAL LINE
                  M5.Lcd.drawLine(165, 211, 215, 211, TFT_BLACK);           // HORIZONTAL LINE
                  M5.Lcd.drawLine(165, 212, 215, 212, TFT_BLACK);           // HORIZONTAL LINE
                  M5.Lcd.drawLine(165, 213, 215, 213, TFT_BLACK);           // HORIZONTAL LINE
                  M5.Lcd.drawLine(165, 214, 215, 214, TFT_BLACK);           // HORIZONTAL LINE
                  M5.Lcd.drawLine(165, 215, 215, 215, TFT_BLACK);           // HORIZONTAL LINE
                  M5.Lcd.drawLine(165, 216, 215, 216, TFT_BLACK);           // HORIZONTAL LINE
                  M5.Lcd.drawLine(165, 217, 215, 217, TFT_BLACK);           // HORIZONTAL LINE
                  M5.Lcd.drawLine(165, 218, 215, 218, TFT_BLACK);           // HORIZONTAL LINE
                  M5.Lcd.drawLine(165, 219, 215, 219, TFT_BLACK);           // HORIZONTAL LINE
                  int stw = M5.Lcd.textWidth(tmpStr);
                  M5.Lcd.drawString(tmpStr, 167, 237, GFXFF);
                  if ( (alarmDifSec > cfg.alarm_repeat * 60) && (snoozeDifSec == cfg.snooze_timeout * 60) ) {
                    sndWarning();
                    lastAlarmTime = mktime(&timeinfo);
                  }
                } else {
                  if ( sensorDifMin >= cfg.snd_no_readings ) {
                    // yellow warning state
                    // M5.Lcd.fillRect(110, 220, 100, 20, TFT_YELLOW);
                    Serial.println("OUTR");
                    M5.Lcd.fillRect(165, 220, 50, 30, TFT_YELLOW);
                    M5.Lcd.setTextColor(TFT_BLACK, TFT_YELLOW);
                    M5.Lcd.drawLine(215, 210, 215, 240, TFT_BLACK);           // VERTICAL LINE
                    M5.Lcd.drawLine(216, 210, 216, 240, TFT_BLACK);           // VERTICAL LINE
                    M5.Lcd.drawLine(217, 210, 217, 240, TFT_BLACK);           // VERTICAL LINE
                    M5.Lcd.drawLine(218, 210, 218, 240, TFT_BLACK);           // VERTICAL LINE
                    M5.Lcd.drawLine(219, 210, 219, 240, TFT_BLACK);           // VERTICAL LINE
                    M5.Lcd.drawLine(220, 210, 220, 240, TFT_BLACK);           // VERTICAL LINE
                    M5.Lcd.drawLine(221, 210, 221, 240, TFT_BLACK);           // VERTICAL LINE
                    M5.Lcd.drawLine(222, 210, 222, 240, TFT_BLACK);           // VERTICAL LINE
                    M5.Lcd.drawLine(223, 210, 223, 240, TFT_BLACK);           // VERTICAL LINE
                    M5.Lcd.drawLine(224, 210, 224, 240, TFT_BLACK);           // VERTICAL LINE
                    M5.Lcd.drawLine(165, 210, 215, 210, TFT_BLACK);           // HORIZONTAL LINE
                    M5.Lcd.drawLine(165, 211, 215, 211, TFT_BLACK);           // HORIZONTAL LINE
                    M5.Lcd.drawLine(165, 212, 215, 212, TFT_BLACK);           // HORIZONTAL LINE
                    M5.Lcd.drawLine(165, 213, 215, 213, TFT_BLACK);           // HORIZONTAL LINE
                    M5.Lcd.drawLine(165, 214, 215, 214, TFT_BLACK);           // HORIZONTAL LINE
                    M5.Lcd.drawLine(165, 215, 215, 215, TFT_BLACK);           // HORIZONTAL LINE
                    M5.Lcd.drawLine(165, 216, 215, 216, TFT_BLACK);           // HORIZONTAL LINE
                    M5.Lcd.drawLine(165, 217, 215, 217, TFT_BLACK);           // HORIZONTAL LINE
                    M5.Lcd.drawLine(165, 218, 215, 218, TFT_BLACK);           // HORIZONTAL LINE
                    M5.Lcd.drawLine(165, 219, 215, 219, TFT_BLACK);           // HORIZONTAL LINE
                    int stw = M5.Lcd.textWidth(tmpStr);
                    M5.Lcd.drawString(tmpStr, 167, 237, GFXFF);
                    if ( (alarmDifSec > cfg.alarm_repeat * 60) && (snoozeDifSec == cfg.snooze_timeout * 60) ) {
                      sndWarning();
                      lastAlarmTime = mktime(&timeinfo);
                    }
                  } else {
                    // normal glycemia state
                    //                    M5.Lcd.fillRect(0, 220, 320, 20, TFT_BLACK);
                    //                    M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
                    // draw info line
                    char infoStr[64];
                    switch ( cfg.info_line ) {
                      case 0: // sensor information
                        strcpy(infoStr, sensDev);
                        if (strcmp(infoStr, "MIAOMIAO") == 0) {
                          if (obj.containsKey("xDrip_raw")) {
                            strcpy(infoStr, "xDrip MiaoMiao + Libre");
                          } else {
                            strcpy(infoStr, "Spike MiaoMiao + Libre");
                          }
                        }
                        if (strcmp(infoStr, "Tomato") == 0)
                          strcat(infoStr, " MiaoMiao + Libre");
                        M5.Lcd.drawString(infoStr, 0, 2220, GFXFF);
                        break;
                      case 1: // button function icons
                        // M5.Lcd.drawBitmap(50, 224, 16, 16, (uint8_t*)home_icon16x16);
                        // M5.Lcd.pushImage(50, 224, 16, 16, (uint8_t*)home_icon16x16);
                        drawIcon(58, 2220, (uint8_t*)sun_icon16x16, TFT_LIGHTGREY);
                        drawIcon(153, 2220, (uint8_t*)clock_icon16x16, TFT_LIGHTGREY);
                        // drawIcon(153, 220, (uint8_t*)timer_icon16x16, TFT_LIGHTGREY);
                        drawIcon(246, 2220, (uint8_t*)powerbutton_icon16x16, TFT_LIGHTGREY);
                        break;
                      case 2: // loop + basal information
                        strcpy(infoStr, "L: ");
                        strlcat(infoStr, loopInfoStr, 64);
                        M5.Lcd.drawString(infoStr, 0, 2220, GFXFF);
                        strcpy(infoStr, "B: ");
                        strlcat(infoStr, basalInfoStr, 64);
                        M5.Lcd.drawString(infoStr, 160, 2220, GFXFF);
                        break;
                    }
                  }
                }
              }
            }
          }

        }
      } else {
        String errstr = String(" ");
        Serial.println(errstr);
        M5.Lcd.setCursor(0, 23);
        M5.Lcd.setTextSize(1);
        M5.Lcd.setFreeFont(FSSB12);
        M5.Lcd.println(errstr);
        wasError = 1;
      }
    } else {
      String errstr = String(" ");
      Serial.println(errstr);
      M5.Lcd.setCursor(0, 23);
      M5.Lcd.setTextSize(1);
      M5.Lcd.setFreeFont(FSSB12);
      M5.Lcd.println(errstr);
      wasError = 1;
    }

    http.end();
  }
}

// the loop routine runs over and over again forever
void loop() {
  delay(20);
  buttons_test();

  // update glycemia every 15s
  if (millis() - msCount > 15000) {
    update_glycemia();
    msCount = millis();
  } else {
    if (cfg.show_current_time) {
      // update current time on display
      M5.Lcd.setFreeFont(FM9);
      M5.Lcd.setTextSize(1);
      M5.Lcd.setTextColor(TFT_WHITE, TFT_WHITE);
      if (!getLocalTime(&localTimeInfo)) {
        // unknown time
        strcpy(localTimeStr, "??:??");
        lastMin = 61;
      } else {
        if (getLocalTime(&localTimeInfo)) {
          sprintf(localTimeStr, " ", localTimeInfo.tm_hour, localTimeInfo.tm_min, localTimeInfo.tm_mday, localTimeInfo.tm_mon + 1);
        } else {
          strcpy(localTimeStr, "??:??");
          lastMin = 61;
        }
      }
      if (lastMin != localTimeInfo.tm_min) {
        lastSec = localTimeInfo.tm_sec;
        lastMin = localTimeInfo.tm_min;
        M5.Lcd.drawString(localTimeStr, 2220, 0, GFXFF);
      }
    }




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
