#include "arduino_secrets.h"

#include <NeoPixelBrightnessBus.h>

#include <Arduino.h>
#include <string.h>

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <ESP8266HTTPClient.h>

#include <WiFiClientSecureBearSSL.h>

#include "RemoteDebug.h"        //https://github.com/JoaoLopesF/RemoteDebug

RemoteDebug Debug;


#define BRIGHTNESS_INTERVAL 1500 // ms - interval between brightness updates
#define REQUEST_INTERVAL 300000 // How often we update. In practice LOOP_INTERVAL is added. In ms (5 min is 300000)
#define RETRY_INTERVAL 30000


#define BRIGHTNESS 30 // 20-30 recommended. If using a light sensor, this is the initial brightness on boot.

#define USE_LIGHT_SENSOR true // Set USE_LIGHT_SENSOR to true if you're using any light sensor.

/* This section only applies if you have an ambient light sensor connected */
#if USE_LIGHT_SENSOR
/* The sketch will automatically scale the light between MIN_BRIGHTNESS and
  MAX_BRIGHTNESS on the ambient light values between MIN_LIGHT and MAX_LIGHT
  Set MIN_BRIGHTNESS and MAX_BRIGHTNESS to the same value to achieve a simple on/off effect. */
#define MIN_BRIGHTNESS 4 // Recommend values above 4 as colors don't show well below that
#define MAX_BRIGHTNESS 25 // Recommend values between 20 and 30

// Light values are a raw reading for analog and lux for digital
#define MIN_LIGHT 2 // Recommended default is 16 for analog and 2 for lux
#define MAX_LIGHT 25 // Recommended default is 30 to 40 for analog and 20 for lux


// Adafruit VEML7700 Library - Version: Latest
#include <Adafruit_VEML7700.h>
Adafruit_VEML7700 veml = Adafruit_VEML7700();
#endif


#define ENTRY_DATA 4
#define ENTRY_SIZE 5
#define SKIP_HEADER_LINES 6
#define BUF_SIZE 2048
#define STATION_INDEX 1
#define CONDITION_INDEX 30


String url = "https://aviationweather.gov/cgi-bin/data/dataserver.php?dataSource=metars"
             "&requestType=retrieve&format=csv"
             "&hoursBeforeNow=3&mostRecentForEachStation=true"
             "&stationString=";

struct entry {
  char name[ENTRY_SIZE];
  char condition[ENTRY_SIZE];
};


#define NUM_AIRPORTS 72 // This is really the number of LEDs
// Define the array of leds
NeoPixelBrightnessBus<NeoGrbFeature, NeoEsp8266Uart1800KbpsMethod> strip(NUM_AIRPORTS);

#define colorSaturation 255
RgbColor red(colorSaturation, 0, 0);
RgbColor green(0, colorSaturation, 0);
RgbColor blue(0, 0, colorSaturation);
RgbColor magenta(colorSaturation, 0, colorSaturation);
RgbColor white(colorSaturation);
RgbColor black(0);

String airports[NUM_AIRPORTS] = {
  "KMRY",
  "KSWS",
  "KCUH",
  "KWVI",
  "KE16",
  "KRHV",
  "KSJC",
  "KNUQ",
  "KPAO",
  "KSQL",
  "KHAF",
  "KSFO",
  "KOAK",
  "KHWD",
  "KCCR",
  "KSUU",
  "KVCB",
  "KAPC",
  "KDVO",
  "KO69",
  "KSTS",
  "KUKI",
  "KFOT",
  "KACV",
  "KCEC",
  "KBOK",
  "KHS1",
  "K3S8",
  "KMFR",
  "KSIY",
  "KLMT",
  "KLKV",
  "KAAT",
  "KRDD",
  "KRBL",
  "KCIC",
  "KO05",
  "KSVE",
  "KLOL",
  "KNFL",
  "KRTS",
  "KRWO",
  "KCXP",
  "KMEV",
  "KTVL",
  "KTRK",
  "KBLU",
  "KGOO",
  "KBAB",
  "KMYV",
  "KSMF",
  "KSAC",
  "KMHR",
  "KMCC",
  "KLHM",
  "KAUN",
  "KPVF",
  "KJAQ",
  "KCPU",
  "KO22",
  "KSCK",
  "KC83",
  "KLVK",
  "KTCY",
  "KMOD",
  "KMCE",
  "KMAE",
  "KFAT",
  "KWLC",
  "KHJO",
  "KVIS",
  "KMMH"
};



unsigned long brightnessNext;
unsigned long requestNext;


void setup() {

  Serial.begin(9600);
  // Serial.setDebugOutput(true);

  pinMode(LED_BUILTIN, OUTPUT); // give us control of the onboard LED
  digitalWrite(LED_BUILTIN, LOW);

  Serial.println();
  Serial.println();
  Serial.println();

  for (uint8_t t = 4; t > 0; t--) {
    Serial.printf("[SETUP] WAIT %d...\n", t);
    Serial.flush();
    delay(1000);
  }

  WiFi.mode(WIFI_STA);
  WiFi.hostname("metar_map.local");
  WiFi.begin(SECRET_SSID, SECRET_PASSWORD);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  strip.Begin();
  strip.Show();

#if USE_LIGHT_SENSOR
  if (!veml.begin()) {
    Serial.println("Sensor not found");
    while (1);
  }
  Serial.println("Sensor found");

  veml.setGain(VEML7700_GAIN_1);
  veml.setIntegrationTime(VEML7700_IT_800MS);

  //delay long enough for one reading
  delay(900);
  adjustBrightness();
#else
  strip.SetBrightness(BRIGHTNESS);
#endif

  //indicate boot and demonstrate all LEDs work
  strip.ClearTo(RgbColor(255, 165, 0));
  strip.Show();

  for (int i = 0; i < NUM_AIRPORTS; i++) {
    if (i != 0) {
      url += ",";
    }

    url += airports[i];
  }

  Serial.println(url);

  Debug.begin("remotedebug"); // Initialize the WiFi server
  Debug.setResetCmdEnabled(true); // Enable the reset command
  Debug.showColors(true); // Colors
  Debug.setSerialEnabled(true);

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  
}


#if USE_LIGHT_SENSOR
void adjustBrightness() {
  byte brightness;
  float reading = veml.readLuxNormalized();


  if (reading <= MIN_LIGHT) brightness = 0;
  else if (reading >= MAX_LIGHT) brightness = MAX_BRIGHTNESS;
  else {
    // Percentage in lux range * brightness range + min brightness
    float brightness_percent = (reading - MIN_LIGHT) / (MAX_LIGHT - MIN_LIGHT);
    brightness = brightness_percent * (MAX_BRIGHTNESS - MIN_BRIGHTNESS) + MIN_BRIGHTNESS;
  }

  debugD("Light reading: %f, %d brightness", reading, brightness);
  strip.SetBrightness(brightness);
}
#endif

void loop() {
  uint8_t oldb = strip.GetBrightness();

#if USE_LIGHT_SENSOR
  if (millis() >= brightnessNext) {
  adjustBrightness();
  brightnessNext = millis() + BRIGHTNESS_INTERVAL;
  }
#endif

  digitalWrite(LED_BUILTIN, LOW);
  bool success = false;


  // if we've waited long enough or its the first run
  if (strip.GetBrightness() != 0 && (( millis() >= requestNext) || oldb == 0)) {
    debugI("updating metars");
    // wait for WiFi connection
    if ((WiFi.waitForConnectResult() == WL_CONNECTED)) {
      success = getMetars();
      requestNext = millis() + REQUEST_INTERVAL;
    } else {
      debugW("[HTTPS] Unable to connect");
      requestNext = millis() + RETRY_INTERVAL;
    }

  }

  strip.Show();
  Debug.handle();
  digitalWrite(LED_BUILTIN, HIGH);
  ArduinoOTA.handle();

}

bool getMetars() {
  bool ret = false;

  //black indicates no data
  strip.ClearTo(black);

  std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);

  //client->setFingerprint(fingerprint);
  // Or, if you happy to ignore the SSL certificate, then use the following line instead:
  client->setInsecure();

  HTTPClient https;
  https.useHTTP10(true);
  https.setUserAgent("METAR Map");
  https.setReuse(false);

  debugD("[HTTPS] begin...");
  if (https.begin(*client, url)) {  // HTTPS

    debugD("[HTTPS] GET...");
    // start connection and send HTTP header
    int httpCode = https.GET();

    // httpCode will be negative on error
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      debugD("[HTTPS] GET... code: %d", httpCode);

      // file found at server
      if (httpCode == HTTP_CODE_OK) {
        handleMetars(https.getStream());
        debugI("done getting metars");
        ret = true;
      }
    } else {
      debugE("[HTTPS] GET... failed, error: %s", https.errorToString(httpCode).c_str());
      strip.ClearTo(white);
    }

    https.end();
  }

  return ret;
}

void handleMetars(WiFiClient& input) {
  unsigned int lines = 0;
  unsigned int length = 0;
  char buf[BUF_SIZE];
  signed char c;
  entry e;

  while ((c = input.read()) > 0) {
    yield(); //don't get a soft reset due to WDT

    if (c == '\n') {
      buf[length % BUF_SIZE] = '\0';
      length = 0;
      lines++;
      debugV("%s", buf);

      if (lines > SKIP_HEADER_LINES) updateLEDForEntry(processLine(buf));
      continue;
    }

    //skip the header lines in the CSV
    if (lines < SKIP_HEADER_LINES) {
      continue;
    }

    buf[length++ % BUF_SIZE] = c;

  }
}

entry processLine(char *input) {
  entry e;

  int token = 0;

  while(1) {
    char *p = strchr(input, ',');
    if (p != NULL) {
      *p = '\0';
      if (token == STATION_INDEX) {
        strlcpy(e.name, input, ENTRY_SIZE);
      } else if (token == CONDITION_INDEX) {
        strlcpy(e.condition, input, ENTRY_SIZE);
        break;
      }
      token++;
    } else {
      break;
    }
    input = p + 1;
  }


  return e;
}

void updateLEDForEntry(entry e) {

  for (int i = 0; i < NUM_AIRPORTS; i++) {
    if (strcmp(e.name, airports[i].c_str()) == 0) {
      debugD("found %s: %s", e.name, e.condition);
      if (strcmp(e.condition, "VFR") == 0) {
        strip.SetPixelColor(i, green);
      } else if (strcmp(e.condition, "MVFR") == 0) {
        strip.SetPixelColor(i, blue);
      } else if (strcmp(e.condition, "IFR") == 0) {
        strip.SetPixelColor(i, red);
      } else if (strcmp(e.condition, "LIFR") == 0 ) {
        strip.SetPixelColor(i, magenta);
      } else {
        // happens if we don't get a flight category
        debugD("Got condition %s for airport %s", e.condition, e.name);
      }
      return;
    }
  }

  debugD("Not found: %s", e.name);
}
