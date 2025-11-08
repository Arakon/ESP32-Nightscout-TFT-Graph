/**
Nightscout glucose display for use with ESPS32-C3 and 1.3" or 1.54" SPI screen.
Based partially on my original version of this for ESP8266: https://github.com/Arakon/Nightscout-TFT
and gluci-clock by Frederic1000: https://github.com/Frederic1000/gluci-clock
Also compatible with other ESP32 devices, but may require pin reassignments below.

*/

// install libraries: WifiManager, arduinoJson, ESP_MultiResetDetector, LovyanGFX


// In the Arduino IDE, select Board Manager and downgrade to ESP32 2.0.14, ESP32 3.x based will NOT fit most smaller ESP32 boards anymore.

#include <Arduino.h>
#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>
#include <HTTPClient.h>
#include <time.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <WiFiManager.h>

//including the arrow images
#include "Flat.h"
#include "DoubleDown.h"
#include "DoubleUp.h"
#include "FortyFiveUp.h"
#include "FortyFiveDown.h"
#include "Up.h"
#include "Down.h"
#define ESP_MRD_USE_SPIFFS true

#define GFXFF 1
#include <LovyanGFX.hpp>
#include "_19_font28pt7b.h"  //Numbers font for the Glucose number

//setup for LovyanGFX, all settings are in tft_config.h - Adjust to match your display if needed
#include "tft_config.h"
LGFX_Sprite sprite(&tft);

int hour_c;
int min_c;

// Defaults for when the color of the glucose value changes, edit them in the web interface
int HighBG = 180;
int LowBG = 90;
int CritBG = 70;

// --- Graph configuration ---
static const int GRAPH_WIDTH = 220;
static const int GRAPH_HEIGHT = 60;
static const int GRAPH_X = 10;
static const int GRAPH_Y = 200;

static const int MAX_POINTS = 20;
int sgv_values[MAX_POINTS] = { 0 };
int sgv_count = 0;

// -------------------------------------
// -------   Other Config   ------
// -------------------------------------

const int PIN_LED = LED_BUILTIN;  // default onboard LED
int backlight = 64;               //default brightness

#define JSON_CONFIG_FILE "/config.json"

// Number of seconds after reset during which a
// subseqent reset will be considered a triple reset.
#define MRD_TIMES 3
#define MRD_TIMEOUT 10
#define MRD_ADDRESS 0
#include <ESP_MultiResetDetector.h>
MultiResetDetector* mrd;
//flag for saving data
bool shouldSaveConfig = false;


// url and API key defaults for Nightscout - Set in the web portal
// We use the /pebble API since it contains the delta value. Don't remove the /pebble at the end!
char NS_API_URL[150] = "http://yournightscoutwebsite/pebble";

// Create a token with read access in NS > Hamburger menu > Admin tools
// Enter the token in the web portal
char NS_API_SECRET[50] = "view-123456790";


// Parameters for time NTP server
char ntpServer1[50] = "pool.ntp.org";
char ntpServer2[50] = "de.pool.ntp.org";

// Time zone for local time and daylight saving, edit in web interface
// list here:
// https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
char local_time_zone[50] = "CET-1CEST,M3.5.0,M10.5.0/3";  // set for Europe/Berlin
char utc_time_zone[] = "GMT0";
int gmtOffset_sec = 3600;
long daylightOffset_sec = 3600;

// time zone offset in minutes, initialized to UTC
int tzOffset = 0;

void drawScreen(long sgv, int trend, int bg_delta, int elapsed_mn, const struct tm& tm) {
  sprite.fillScreen(TFT_BLACK);

  // ----------------------------
  // "Last Data" age display
  // ----------------------------
  int agecol = (elapsed_mn >= 15) ? TFT_RED : TFT_WHITE;  // change color if data is over 15 minutes old
  int agepos = (elapsed_mn >= 100)  ? 19                  // change position depending on number of digits
               : (elapsed_mn >= 10) ? 26
                                    : 33;

  sprite.setFont(&fonts::FreeSerifBold9pt7b);
  sprite.setTextColor(agecol, TFT_BLACK);
  sprite.setCursor(agepos, 4);
  sprite.printf("Last Data: %d min%s ago", elapsed_mn, (elapsed_mn == 1) ? "" : "s");  // add "s" to "mins" if >1

  // ----------------------------
  // Clock
  // ----------------------------
  sprite.setFont(&fonts::FreeSerifBold24pt7b);
  sprite.setTextColor(TFT_BLUE, TFT_BLACK);
  sprite.setCursor(65, 40);
  sprite.printf("%02d:%02d", tm.tm_hour, tm.tm_min);

  // ----------------------------
  // Glucose value
  // ----------------------------
  int bgcol = TFT_WHITE;
  if (sgv >= HighBG) bgcol = TFT_ORANGE;
  else if ((sgv <= LowBG) && (sgv > CritBG)) bgcol = TFT_YELLOW;
  else if (sgv <= CritBG) bgcol = TFT_RED;

  sprite.setFont(&_19_font28pt7b);
  sprite.setTextColor(bgcol, TFT_BLACK);
  sprite.setCursor((sgv < 100) ? 70 : 30, 100);  // adjust depending on whether the value has 2 or 3 digits
  sprite.println(sgv);

  // ----------------------------
  // Trend arrow
  // ----------------------------
  sprite.setSwapBytes(true);
  const uint16_t* arrowImg = nullptr;
  switch (trend) {
    case 1: arrowImg = DoubleUp; break;
    case 2: arrowImg = Up; break;
    case 3: arrowImg = FortyFiveUp; break;
    case 4: arrowImg = Flat; break;
    case 5: arrowImg = FortyFiveDown; break;
    case 6: arrowImg = Down; break;
    case 7: arrowImg = DoubleDown; break;
  }
  if (arrowImg) {
    sprite.pushImage(180, 100, 50, 50, arrowImg);
  }

  // ----------------------------
  // Delta
  // ----------------------------
  sprite.setFont(&fonts::FreeSerifBold18pt7b);
  sprite.setTextColor(TFT_BLUE, TFT_BLACK);
  sprite.setCursor((bg_delta >= 10) ? 37 : 49, 160);
  sprite.printf("Delta: %+d", bg_delta);
  drawSGVGraph(sprite);
  sprite.pushSprite(0, 0);
}

void drawSGVGraph(LGFX_Sprite& sprite) {

  // --- Define graph area ---
  int x0 = GRAPH_X;
  int y0 = GRAPH_Y;
  int w = GRAPH_WIDTH;
  int h = GRAPH_HEIGHT;

  // --- Clear graph area ---
  sprite.fillRect(x0, y0, w, h, TFT_BLACK);
  sprite.drawRect(x0, y0, w, h, TFT_DARKGREY);

  if (sgv_count == 0) return;

  const float MIN_SGV = 40.0f;
  const float MAX_SGV = 300.0f;
  const float LOG_BASE = 1.03f;
  auto logb = [&](float v) {
    return logf(v) / logf(LOG_BASE);
  };

  // compute consistent min/max
  float log_min = logb(MIN_SGV);
  float log_max = logb(MAX_SGV);

  // --- Draw Y-axis labels (SGV range) ---
  const int gridLevels[] = { 40, CritBG, HighBG, 300 };
  const int gridCount = sizeof(gridLevels) / sizeof(gridLevels[0]);
  sprite.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sprite.setTextDatum(TL_DATUM);
  sprite.setFont(&fonts::Font0);
  for (int i = 0; i < gridCount; i++) {
    float log_val = logb(gridLevels[i]);
    float ratio = (log_val - log_min) / (log_max - log_min);
    int y = y0 + h - (int)(ratio * h);
    sprite.drawLine(x0 + 1, y, x0 + w - 1, y, TFT_DARKGREY);
    sprite.drawString(String(gridLevels[i]), x0 + 3, y - 4);
  }

  const float x_margin = w * 0.03f;  // 3% padding on each side
  const float usable_width = w - (2 * x_margin);
  const float xstep = usable_width / (MAX_POINTS - 1);

  // --- X-axis tick marks (each 5-min interval) ---
  for (int i = 0; i < MAX_POINTS; i++) {
    int x = x0 + x_margin + (int)(i * xstep);
    sprite.drawLine(x, y0 + h - 4, x, y0 + h, TFT_DARKGREY);
  }

  // --- Plot SGV points as filled circles ---
  for (int i = 0; i < sgv_count; i++) {
    int x = x0 + x_margin + (int)(i * xstep);
    float clamped_sgv = std::max(MIN_SGV, std::min(MAX_SGV, (float)sgv_values[i]));  //keep it from going out of range
    float log_val = logb(clamped_sgv);
    int y = y0 + h - (int)(((log_val - log_min) / (log_max - log_min)) * h);

    int dotcol = TFT_GREEN;
    if (sgv_values[i] >= HighBG) dotcol = TFT_ORANGE;
    else if ((sgv_values[i] <= LowBG) && (sgv_values[i] > CritBG)) dotcol = TFT_YELLOW;
    else if (sgv_values[i] <= CritBG) dotcol = TFT_RED;
    sprite.fillCircle(x, y, 2, dotcol);
  }
}

void serialPrintParams() {
  Serial.println("\tNS_API_URL : " + String(NS_API_URL));
  Serial.println("\tNS_API_SECRET: " + String(NS_API_SECRET));
  Serial.println("\tntpServer1 : " + String(ntpServer1));
  Serial.println("\tntpServer2 : " + String(ntpServer2));
  Serial.println("\tlocal_time_zone : " + String(local_time_zone));
  Serial.println("\tgmtOffset_sec : " + String(gmtOffset_sec));
  Serial.println("\tdaylightOffset_sec : " + String(daylightOffset_sec));
  Serial.println("\tbacklight : " + String(backlight));
  Serial.println("\tHigh BG : " + String(HighBG));
  Serial.println("\tLow BG : " + String(LowBG));
  Serial.println("\tCritical BG : " + String(CritBG));
}

void saveConfigFile() {
  Serial.println(F("Saving config"));
  StaticJsonDocument<256> json;

  json["NS_API_URL"] = NS_API_URL;
  json["NS_API_SECRET"] = NS_API_SECRET;
  json["ntpServer1"] = ntpServer1;
  json["ntpServer2"] = ntpServer2;
  json["local_time_zone"] = local_time_zone;
  json["gmtOffset_sec"] = gmtOffset_sec;
  json["daylightOffset_sec"] = daylightOffset_sec;
  json["backlight"] = backlight;
  json["HighBG"] = HighBG;
  json["LowBG"] = LowBG;
  json["CritBG"] = CritBG;

  File configFile = SPIFFS.open(JSON_CONFIG_FILE, "w");
  if (!configFile) {
    Serial.println("failed to open config file for writing");
  }

  serializeJsonPretty(json, Serial);
  if (serializeJson(json, configFile) == 0) {
    Serial.println(F("Failed to write to file"));
  }
  configFile.close();

  delay(3000);
  ESP.restart();
  delay(5000);
}

bool loadConfigFile() {
  Serial.println("mounting FS...");

  if (!SPIFFS.begin(false)) {  // mount existing FS
    Serial.println("SPIFFS mount failed, formatting...");
    delay(10);
    if (!SPIFFS.begin(true)) {  // format only if mount fails
      Serial.println("SPIFFS format failed!");
      return false;
    }
  }

  Serial.println("mounted file system");

  if (SPIFFS.exists(JSON_CONFIG_FILE)) {
    Serial.println("reading config file");
    File configFile = SPIFFS.open(JSON_CONFIG_FILE, "r");
    if (configFile) {
      StaticJsonDocument<256> json;
      DeserializationError error = deserializeJson(json, configFile);
      configFile.close();  // close immediately to release file handle

      serializeJsonPretty(json, Serial);

      if (!error) {
        backlight = json["backlight"];
        HighBG = json["HighBG"];
        LowBG = json["LowBG"];
        CritBG = json["CritBG"];
        strcpy(NS_API_URL, json["NS_API_URL"]);
        strcpy(NS_API_SECRET, json["NS_API_SECRET"]);
        strcpy(ntpServer1, json["ntpServer1"]);
        strcpy(ntpServer2, json["ntpServer2"]);
        strcpy(local_time_zone, json["local_time_zone"]);
        gmtOffset_sec = json["gmtOffset_sec"];
        daylightOffset_sec = json["daylightOffset_sec"];
        return true;
      } else {
        Serial.println("failed to load json config");
      }
    }
  }

  return false;
}


//callback notifying us of the need to save config
void saveConfigCallback() {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

WiFiManager wm;

// default password for Access Point, made from macID
char* getDefaultPassword() {
  // example:
  // const char * password = getDefaultPassword();

  // source for chipId: Espressif library example ChipId
  uint32_t chipId = 0;
  for (int i = 0; i < 17; i = i + 8) {
    chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  chipId = chipId % 100000000;
  static char pw[9];  // with +1 char for end of chain
  sprintf(pw, "%08d", chipId);
  return pw;
}
const char* apPassword = getDefaultPassword();

//callback notifying us of the need to save parameters
void saveParamsCallback() {
  Serial.println("Should save params");
  shouldSaveConfig = true;
  wm.stopConfigPortal();  // will abort config portal after page is sent
}

// This gets called when the config mode is launched
void configModeCallback(WiFiManager* myWiFiManager) {
  Serial.println("Entered Conf Mode");
  Serial.print("Config SSID: ");
  Serial.println(myWiFiManager->getConfigPortalSSID());
  Serial.print("Config password: ");
  Serial.println(apPassword);
  Serial.print("Config IP Address: ");
  Serial.println(WiFi.softAPIP());

  // Show IP and password for initial setup
  tft.clear(0x000000u);
  tft.setCursor(10, 80);
  tft.setTextSize(2);
  tft.println("Please connect to: ");
  tft.println(WiFi.softAPIP());
  tft.setCursor(10, 130);
  tft.println("Password: ");
  tft.println(apPassword);
  tft.setTextSize(1);
}


/**
 * Set the timezone
 */
void setTimezone(char* timezone) {
  Serial.printf("  Setting Timezone to %s\n", timezone);
  configTzTime(timezone, ntpServer1, ntpServer2);
}

/**
 * Get time from internal clock
 * returns time for the timezone defined by setTimezone(char* timezone)
 */
struct tm getActualTzTime() {
  struct tm timeinfo = { 0 };
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time!");
    return (timeinfo);  // return {0} if error
  }
  Serial.print("System time: ");
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  return (timeinfo);
}

/**
 * Returns the offset in seconds between local time and UTC
 */
int getTzOffset(char* timezone) {

  // set timezone to UTC
  setTimezone(utc_time_zone);

  // and get tm struct
  struct tm tm_utc_now = getActualTzTime();

  // convert to time_t
  time_t t_utc_now = mktime(&tm_utc_now);

  // set timezone to local
  setTimezone(local_time_zone);

  // convert time_t to tm struct
  struct tm tm_local_now = *localtime(&t_utc_now);

  // set timezone back to UTC
  setTimezone(utc_time_zone);

  // convert tm to time_t
  time_t t_local_now = mktime(&tm_local_now);

  // calculate difference between the two time_t, in seconds
  int tzOffset = round(difftime(t_local_now, t_utc_now));
  Serial.printf("\nTzOffset : %d\n", tzOffset);

  return (tzOffset);
}

void setup() {

  Serial.begin(115200);
  Serial.println();
  Serial.println("ESP start");
  bool forceConfig = false;

  delay(200);  // reduce the chance of false detection at cold boot
  mrd = new MultiResetDetector(MRD_TIMEOUT, MRD_ADDRESS);
  if (mrd->detectMultiReset()) {
    Serial.println(F("Forcing config mode as there was a Triple reset detected"));
    forceConfig = true;
  }

  pinMode(PIN_LED, OUTPUT);

  // Initialize display
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  //Print something during boot so you know it's doing something
  tft.setBrightness(backlight);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(10, 60);
  tft.setTextSize(2);
  tft.println("Starting up...");
  tft.println(" Wait 60 seconds.");
  tft.setTextSize(1);
  Serial.setTimeout(2000);
  Serial.println();
  Serial.println("Starting up...");
  sprite.setColorDepth(16);
  if (!sprite.createSprite(240, 280)) {
    Serial.println("ERROR: createSprite() failed - insufficient memory or fragmentation.");
  }
  if (!loadConfigFile()) {
    Serial.println(F("Forcing config mode as there is no saved config"));
    forceConfig = true;
  }

  WiFi.mode(WIFI_STA);  // explicitly set mode, esp defaults to STA+AP
  delay(10);

  //sets timeout until configuration portal gets turned off
  wm.setTimeout(10 * 60);
  wm.setDarkMode(true);

  //set callbacks
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.setSaveParamsCallback(saveParamsCallback);
  //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wm.setAPCallback(configModeCallback);
  wm.setTitle("Nightscout-TFT");

  // Set cutom menu via menu[] or vector
  const char* wmMenu[] = { "param", "wifi", "close", "sep", "info", "restart", "exit" };
  wm.setMenu(wmMenu, 7);  // custom menu array must provide length

  // url and API key for Nightscout

  WiFiManagerParameter custom_NS_API_URL("NS_API_URL", "NightScout API URL", NS_API_URL, 150);

  WiFiManagerParameter custom_NS_API_SECRET("NS_API_SECRET", "NightScout API secret", NS_API_SECRET, 50);

  // Parameters for time NTP server
  WiFiManagerParameter custom_ntpServer1("ntpServer1", "NTP server 1", ntpServer1, 50);
  WiFiManagerParameter custom_ntpServer2("ntpServer2", "NTP server 2", ntpServer2, 50);

  // Time zone for local time and daylight saving
  // list here:
  // https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
  // char[50] local_time_zone = "CET-1CEST,M3.5.0,M10.5.0/3"; // set for Europe/Paris
  WiFiManagerParameter custom_local_time_zone("local_time_zone", "local_time_zone", local_time_zone, 50);

  //int  gmtOffset_sec = 3600;
  char str_gmtOffset_sec[5];
  sprintf(str_gmtOffset_sec, "%d", gmtOffset_sec);
  WiFiManagerParameter custom_gmtOffset_sec("gmtOffset_sec", "gmtOffset sec", str_gmtOffset_sec, 5);

  // int   daylightOffset_sec = 3600;
  char str_daylightOffset_sec[5];
  sprintf(str_daylightOffset_sec, "%d", daylightOffset_sec);
  WiFiManagerParameter custom_daylightOffset_sec("daylightOffset_sec", "daylightOffset sec", str_daylightOffset_sec, 5);

  char str_backlight[5];
  sprintf(str_backlight, "%u", backlight);
  WiFiManagerParameter custom_backlight("backlight", "Backlight 0-255", str_backlight, 3);

  char str_highbg[5];
  sprintf(str_highbg, "%u", HighBG);
  WiFiManagerParameter custom_highbg("HighBG", "High BG Value", str_highbg, 3);
  char str_lowbg[5];
  sprintf(str_lowbg, "%u", LowBG);
  WiFiManagerParameter custom_lowbg("LowBG", "Low BG Value", str_lowbg, 3);
  char str_critbg[5];
  sprintf(str_critbg, "%u", CritBG);
  WiFiManagerParameter custom_critbg("CritBG", "Critical BG Value", str_critbg, 3);

  // add app parameters to web interface
  wm.addParameter(&custom_NS_API_URL);
  wm.addParameter(&custom_NS_API_SECRET);
  wm.addParameter(&custom_ntpServer1);
  wm.addParameter(&custom_ntpServer2);
  wm.addParameter(&custom_local_time_zone);
  wm.addParameter(&custom_gmtOffset_sec);
  wm.addParameter(&custom_daylightOffset_sec);
  wm.addParameter(&custom_backlight);
  wm.addParameter(&custom_highbg);
  wm.addParameter(&custom_lowbg);
  wm.addParameter(&custom_critbg);

  digitalWrite(PIN_LED, LOW);
  if (forceConfig) {
    Serial.println("forceconfig = True");

    if (!wm.startConfigPortal("Nightscout-TFT", apPassword)) {
      Serial.print("shouldSaveConfig: ");
      Serial.println(shouldSaveConfig);
      if (!shouldSaveConfig) {
        Serial.println("failed to connect CP and hit timeout");
        delay(3000);
        //reset and try again
        ESP.restart();
        delay(5000);
      }
    }
  } else {
    Serial.println("Running wm.autoconnect");

    if (!wm.autoConnect("Nightscout-TFT", apPassword)) {
      Serial.println("failed to connect AC and hit timeout");
      delay(3000);
      // if we still have not connected restart and try all over again
      ESP.restart();
      delay(5000);
    }
  }

  // If we get here, we are connected to the WiFi or should save params
  digitalWrite(PIN_LED, HIGH);

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  // Lets deal with the user config values

  strcpy(NS_API_URL, custom_NS_API_URL.getValue());
  strcpy(NS_API_SECRET, custom_NS_API_SECRET.getValue());
  strcpy(ntpServer1, custom_ntpServer1.getValue());
  strcpy(ntpServer2, custom_ntpServer2.getValue());
  strcpy(local_time_zone, custom_local_time_zone.getValue());
  gmtOffset_sec = atoi(custom_gmtOffset_sec.getValue());
  daylightOffset_sec = atoi(custom_daylightOffset_sec.getValue());
  backlight = atoi(custom_backlight.getValue());
  HighBG = atoi(custom_highbg.getValue());
  LowBG = atoi(custom_lowbg.getValue());
  CritBG = atoi(custom_critbg.getValue());


  Serial.println("\nThe values returned are: ");
  serialPrintParams();

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    saveConfigFile();
  }
  tft.setBrightness(backlight);

  // offset in seconds between local time and UTC
  tzOffset = getTzOffset(local_time_zone);

  // set timezone to local
  setTimezone(local_time_zone);
}

void loop() {
  mrd->loop();
  static int last_minute = -1;  // remembers the last minute we fetched data
  time_t now = time(nullptr);
  struct tm tm;
  localtime_r(&now, &tm);

  if (WiFi.status() != WL_CONNECTED) {
    delay(10);
    static unsigned long lastReconnectAttempt = 0;
    if (millis() - lastReconnectAttempt > 3000) {
      Serial.println("Reconnecting WiFi...");
      WiFi.reconnect();
      lastReconnectAttempt = millis();
    }
  }

  // Retrieve glucose data from NightScout Server
  if (tm.tm_min != last_minute) {
    last_minute = tm.tm_min;
    HTTPClient http;
    Serial.println("\n[HTTP] begin...");
    http.begin(NS_API_URL);  // fetch latest value
    http.addHeader("API-SECRET", NS_API_SECRET);
    Serial.print("[HTTP] GET...\n");

    // start connection and send HTTP header
    http.setConnectTimeout(10000);  // 10s
    http.setTimeout(10000);         // 10s
    int httpCode = http.GET();
    // httpCode will be negative on error
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      // NightScout data received from server
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();

        // parse NightScout data Json response
        StaticJsonDocument<512> filter;
        filter["bgs"][0]["sgv"] = true;
        filter["bgs"][0]["trend"] = true;
        filter["bgs"][0]["bgdelta"] = true;
        filter["bgs"][0]["datetime"] = true;
        filter["status"][0]["now"] = true;

        StaticJsonDocument<512> httpResponseBody;
        DeserializationError error = deserializeJson(httpResponseBody, payload, DeserializationOption::Filter(filter));

        // Test if parsing NightScout data succeeded
        if (error) {
          Serial.println();
          Serial.print(F("deserializeJson() failed: "));
          Serial.println(error.f_str());
        } else {

          // Get glucose values, trend, delta and timestamps
          long sgv = long(httpResponseBody["bgs"][0]["sgv"]);
          int trend = httpResponseBody["bgs"][0]["trend"];
          int bg_delta = httpResponseBody["bgs"][0]["bgdelta"];
          long long status0_now = httpResponseBody["status"][0]["now"];
          long long bgs0_datetime = httpResponseBody["bgs"][0]["datetime"];
          int elapsed_mn = (status0_now - bgs0_datetime) / 1000 / 60;

          if (sgv > 0) {  //only update if glucose value is valid
            if ((elapsed_mn == 0) || (elapsed_mn == 5)) {

              if (sgv_count < MAX_POINTS) {
                sgv_values[sgv_count++] = sgv;
              } else {
                // shift left, drop oldest
                for (int i = 0; i < MAX_POINTS - 1; i++) {
                  sgv_values[i] = sgv_values[i + 1];
                }
                sgv_values[MAX_POINTS - 1] = sgv;
              }
              Serial.printf("Appended SGV to history at minute %d (sgv=%ld)\n", tm.tm_min, sgv);
            }
            drawScreen(sgv, trend, bg_delta, elapsed_mn, tm);
            Serial.printf("%02d:%02d", tm.tm_hour, tm.tm_min);
            Serial.println();
            Serial.printf("Received Data, last update %d min%s ago", elapsed_mn, (elapsed_mn == 1) ? "" : "s");
          }
        }
      }

      else {
        Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
        delay(2000);  // prevent rapid retries
      }

      http.end();
    }
  }
}
