#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <RTClib.h>
#include <SPI.h>
#include <TM1637Display.h>
#include <Wire.h>

// debugging related, uncomment to print debugging statements to serial port
#define TRACE(x) // Serial.println(x)

// timer related
#define TIMER_RELOAD_VALUE (40000000) // 2 Hz interrupt @ 80Mhz clock

// wifi related
const char *ssid = "WirelessClock";
const char *password = "th5r5!5n0$p00n";

ESP8266WebServer server(80);

// display related
const int CLK = D6;
const int DIO = D5;

TM1637Display display(CLK, DIO);

// RTC related
volatile bool colonOn = false;
RTC_DS3231 rtc;
DateTime now;

void sendDateTime() {
  const int RESPONSE_BUFFER_LENGTH = 200;

  char responseBuffer[RESPONSE_BUFFER_LENGTH];

  sprintf(responseBuffer,
          "{\"Date\":\"%04d/%02d/%02d\",\"Time\":\"%02d:%02d:%02d\"}",
          now.year(), now.month(), now.day(), //
          now.hour(), now.minute(), now.second());

  server.send(200, "text/json", responseBuffer);
}

void handleGET(void) { sendDateTime(); }

void handlePOST(void) {

  String dateArg = server.arg("date");
  String timeArg = server.arg("time");

  bool dateOK = false, timeOK = false;
  int year(0), mon(0), day(0);
  int hh(0), mm(0), ss(0);

  if (dateArg.length() >= 10) {
    year = (dateArg[0] - '0') * 1000 + (dateArg[1] - '0') * 100 +
           (dateArg[2] - '0') * 10 + (dateArg[3] - '0');

    mon = (dateArg[5] - '0') * 10 + (dateArg[6] - '0');

    day = (dateArg[8] - '0') * 10 + (dateArg[9] - '0');

    dateOK = (((year >= 2000) && (year <= 3000)) &&
              ((mon >= 1) && (mon <= 12)) && ((day >= 1) && (day <= 31)));
  }

  if (timeArg.length() >= 8) {

    hh = (timeArg[0] - '0') * 10 + (timeArg[1] - '0');
    mm = (timeArg[3] - '0') * 10 + (timeArg[4] - '0');
    ss = (timeArg[6] - '0') * 10 + (timeArg[7] - '0');

    timeOK = ((hh >= 0 && hh <= 23) && (mm >= 0 && mm <= 59) &&
              (ss >= 0 && ss <= 59));
  }

  if (dateOK && timeOK) {
    rtc.adjust(DateTime(year, mon, day, hh, mm, ss));
    now = rtc.now();

    sendDateTime();
  } else {
    if (!dateOK) {
      if (timeOK)
        server.send(400, "text/json",
                    "{\"date\":\"invalid input/format; reqd:yyyy/mm/dd\"}");
      else
        server.send(400, "text/json",
                    "{\"date\":\"invalid input/format; reqd:yyyy/mm/dd\","
                    "\"time\":\"invalid input/format; reqd:hh:mm:ss\"}");
    } else if (!timeOK) {
      server.send(400, "text/json",
                  "{\"time\":\"invalid input/format; reqd:hh:mm:ss\"}");
    }
  }
}

void timer0_ISR(void) {

  uint8_t segments[4] = {0, 0, 0, 0};

  colonOn = !colonOn;

  if (colonOn) {
    now = rtc.now();

    segments[0] = display.encodeDigit(now.hour() / 10);
    segments[1] = display.encodeDigit(now.hour() % 10);
    segments[2] = display.encodeDigit(now.minute() / 10);
    segments[3] = display.encodeDigit(now.minute() % 10);
  }

  if (colonOn)
    segments[1] |= 0x80;
  else
    segments[1] &= 0x7f;

  display.setSegments(segments);

  timer0_write(ESP.getCycleCount() + TIMER_RELOAD_VALUE);
}

void setup() {
  Serial.begin(115200); // For debugging output

  TRACE("Initializing RTC ...");

  if (!rtc.begin()) {
    TRACE("Failed. Couldn\"t find RTC.");
    while (1) {
      delay(1000);
    }
  }

  if (rtc.lostPower()) {
    TRACE("RTC lost power, resetting the time!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  now = rtc.now();

  TRACE("Done");

  TRACE("Initializing display");

  display.setBrightness(0x0a); // set the diplay to maximum brightness

  TRACE("Done");

  TRACE("Initializing Wifi ...");

  WiFi.softAP(ssid, password);

  server.on("/clock", HTTP_GET, handleGET);
  server.on("/clock", HTTP_POST, handlePOST);
  server.begin();

  TRACE("Done");

  TRACE("Initializing Timer ...");

  noInterrupts();
  timer0_isr_init();
  timer0_attachInterrupt(timer0_ISR);
  timer0_write(ESP.getCycleCount() + TIMER_RELOAD_VALUE);
  interrupts();

  TRACE("Done");
}

void loop() { server.handleClient(); }
