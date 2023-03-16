//
// Set DS3231 from current time w/UTC offset and daylight saving time
// uses https://ipapi.co/<your-ip>/utc_offset/
//
// Copyright (c) 2023 BitBank Software, Inc.
// written by Larry Bank
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//    http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//===========================================================================

#include <WiFi.h>
#include "rtc_eeprom.h"
#include <bb_spi_lcd.h>
#include <NTPClient.h>           //https://github.com/taranais/NTPClient
#include <WiFiUdp.h>
#include <Wire.h>
#include <HTTPClient.h>
#include "esp_wifi.h"
static WiFiUDP ntpUDP;
static NTPClient timeClient(ntpUDP, "pool.ntp.org");
struct tm myTime;

// Designed for the Atoms3 GROVE connector
// change for your target machine
#define SDA_PIN 2
#define SCL_PIN 1

BB_SPI_LCD lcd;
const char* ssid     = "your_ssid";
const char* password = "your_password";

//
// This function uses the ipapi.co website to convert
// a public IP address into a time zone offset (HHMM)
// It returns the offset in seconds from GMT
//
int GetTimeOffset(char *szIP)
{
  HTTPClient http;
  int httpCode = -1;
  char szTemp[256];

  //format -> https://ipapi.co/<your public ip>/utc_offset/
  sprintf(szTemp, "https://ipapi.co/%s/utc_offset/", szIP);
  http.begin(szTemp);
  httpCode = http.GET();  //send GET request
  if (httpCode != 200) {
     http.end();
     return -1;
  } else {
     const char *s;
     int i;
     String payload = http.getString();
     http.end();
     s = payload.c_str();
    // Get the raw HTTP response text (+HHMM)
    // and convert the time zone offset (HHMM) into seconds
    lcd.print("TZ offset ");
    lcd.println(s);
    i = ((s[1]-'0') * 10) + (s[2]-'0'); // hour
    i *= 60;
    i += ((s[3]-'0') * 10) + (s[4]-'0'); // minute
    if (s[0] == '-')
      i = -i; // negative offset
    return (i*60); // return seconds
  } // if successfully connected
  return -1;
} /* GetTimeOffset() */
//
// Get our external IP from ipify.org
// Copy it into the given string variable
// in the form (a.b.c.d)
// Returns true for success
//
bool GetExternalIP(char *szIP)
{
  WiFiClient client;

  if (!client.connect("api.ipify.org", 80)) {
    lcd.println("api.ipify.org failed!");
    return false;
  }
  else {
    int timeout = millis() + 5000;
    client.print("GET /?format=json HTTP/1.1\r\nHost: api.ipify.org\r\n\r\n");
    while (client.available() == 0) {
      if (timeout - millis() < 0) {
        lcd.println("Client Timeout!");
        client.stop();
        return false;
      }
    }
    // Get the raw HTTP+JSON response text
    // and parse out just the IP address
    int i, j, size, offset = 0;
    char szTemp[256];
    while ((size = client.available()) > 0) {
      if (size+offset > 256) size = 256-offset;
      size = client.read((uint8_t *)&szTemp[offset], size);
      offset += size;
    } // while data left to read

    // parse the IP address we want
    for (i=0; i<offset; i++) {
      if (memcmp(&szTemp[i],"{\"ip\":\"", 7) == 0) {
        for (j=i+7; j<offset && szTemp[j] != '\"'; j++) {
          szIP[j-(i+7)] = szTemp[j];
        } // for j
        szIP[j-(i+7)] = 0; // zero terminate it
        return true;
      } // if found start of IP
    } // for i
  } // if successfully connected
  return false;
} /* GetExternalIP() */

void setup() {
  int iTimeout;
  char szIP[32], szTemp[128];

  // Initialize LCD display of AtomS3
  lcd.begin(DISPLAY_M5STACK_ATOMS3);
  lcd.fillScreen(0);
  lcd.setTextColor(0xffff, 0x1f);
  lcd.setFont(FONT_12x16);
  lcd.println("RTC SETTER");
  lcd.setFont(FONT_8x8);
  // See if a DS3231 is attache
  Wire.begin(SDA_PIN, SCL_PIN, 100000);
  Wire.setTimeout(20000L);
  Wire.beginTransmission(0x68);
  if (Wire.endTransmission()) {
    // NACK means there's no RTC
    lcd.setTextColor(0xf800, 0);
    lcd.println("No RTC found");
    lcd.println("Stopping");
    while (1) {};
  } else {
    rtcInit(RTC_DS3231, SDA_PIN, SCL_PIN, false);
    lcd.setTextColor(0xf81f, 0);
    lcd.println("old time:");
    rtcGetTime(&myTime);
    lcd.printf("%02d:%02d:%02d\n", myTime.tm_hour, myTime.tm_min, myTime.tm_sec);
  }
  Wire.end();
  lcd.setTextColor(0x7e0,0);
  lcd.println("Conn to WiFi");
  WiFi.begin(ssid, password);
  iTimeout = 0;
  while (WiFi.status() != WL_CONNECTED && iTimeout < 20) {
    delay(500);
    iTimeout++;
    lcd.print(".");
   }
if (WiFi.status() == WL_CONNECTED) {
   lcd.println("\nCONNECTED!");
} else {
   lcd.setTextColor(0xf800,0);
   lcd.print("\nFailed!");
   lcd.println("Press reset");
   lcd.print("to try again");
   while (1) {};
}
  if (GetExternalIP(szIP)) {
    int iTimeOffset; // offset in seconds
    lcd.setTextColor(0xffe0);
    lcd.println("My IP:");
    lcd.println(szIP);
    // Get our time zone offset (including daylight saving time)
    iTimeOffset = GetTimeOffset(szIP);
    if (iTimeOffset != -1) {
    // Initialize a NTPClient to get time
      timeClient.begin();
      timeClient.setTimeOffset(iTimeOffset);
      timeClient.update();
      unsigned long epochTime = timeClient.getEpochTime();
  //Get a time structure 
      struct tm *ptm = gmtime ((time_t *)&epochTime);
      rtcSetTime(ptm); // set it into our RTC chip
      timeClient.end(); // don't need it any more
    } else {
      lcd.setTextColor(0xf800, 0);
      lcd.println("TZ info failed");
    }
  }
  WiFi.disconnect(true); // disconnect and turn off the WiFi radio

} /* setup() */

// show the current time from the RTC continuously

void loop() {
  char szTemp[32];

    lcd.setTextColor(0xffff,0);
    lcd.println("\nLocal Time:");
    lcd.setFont(FONT_12x16);
    while (1) {
      rtcGetTime(&myTime);
      lcd.setCursor(0, 96);
      sprintf(szTemp, "%02d:%02d:%02d\n", myTime.tm_hour, myTime.tm_min, myTime.tm_sec);
      lcd.print(szTemp);
      delay(1000);
    }
}
