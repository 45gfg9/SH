#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <FlagTicker.h>
#include "remote.hxx"

#define BLINKER_WIFI
#include <Blinker.h>

#define TIME_INTERVAL 60
#define REQ_INTERVAL 3600

const IPAddress GATEWAY_IP(192, 168, 45, 1);
const IPAddress SUBNET_MASK(255, 255, 255, 0);

ESP8266WebServer server; // default port 80
WiFiServer tcp(TCP_PORT);
WiFiUDP udp;
FlagTicker ft_time;

BlinkerNumber bln_temperature("num-tmp");
BlinkerNumber bln_humidity("num-hum");
BlinkerNumber bln_dust("num-dst");

weather_data fetchWeatherData();
time_t fetchTime();

void setup() {
  Serial.begin(BAUD_RATE);
  Serial.println();

  if (!LittleFS.begin()) {
    Serial.println(F("FS fail!"));
  }

  WiFi.softAPConfig(GATEWAY_IP, GATEWAY_IP, SUBNET_MASK);
  remote::begin();
  Blinker.begin("ed581893a94c", nullptr, nullptr);

  tcp.setNoDelay(true);
  tcp.begin();
  udp.begin(UDP_PORT);

  ft_time.begin(TIME_INTERVAL);

  server.begin(80);

  server.serveStatic("/", LittleFS, "/index.html");
}

void loop() {
  static time_t last_udp_req = -1000 * REQ_INTERVAL;
  if (ft_time) {
    Serial.print(F("Updating time.. "));
    time_t t;

    if (millis() - last_udp_req >= 1000 * REQ_INTERVAL) {
      t = fetchTime() + 8 * 3600; // UTC+8
      // Drop milliseconds accuracy
      timeval tv = {t, 0};
      settimeofday(&tv, nullptr);
      last_udp_req = millis();
    } else {
      time(&t);
    }

    udp_packet pkt = {t};

    Serial.println(F("Sending UDP packet"));
    udp.beginPacket(remote::getBroadcastIP(WiFi.softAPIP(), SUBNET_MASK),
                    UDP_PORT);
    udp.write(reinterpret_cast<const char *>(&pkt), sizeof(pkt));
    udp.endPacket();

    ft_time = false;
  }

  while (tcp.hasClient()) {
    Serial.print(F("TCP handling.. "));
    size_t sent = 0;
    WiFiClient client = tcp.available();

    int cmd = client.read();
    if (cmd == 0x0B) {
      weather_data data = fetchWeatherData();

      sent += client.write(data.location.length());
      sent += client.print(data.location);
      sent += client.write(data.weather.length());
      sent += client.print(data.weather);
      sent += client.write(data.temperature);

      delay(500);

      Serial.printf_P(PSTR("%u bytes sent\r\n"), sent);
    } else if (cmd == 0x0C) {
      bln_temperature.print(client.read());
      bln_humidity.print(client.read());
      bln_dust.print(client.read());
      Serial.println(F("Env info updated"));
    }

    client.stop();
  }

  server.handleClient();
  Blinker.run();
}

time_t fetchTime() {
  static const size_t NTP_PACKET_SIZE = 48;
  static const uint32_t SECONDS_FROM_1900_TO_1970 = 2208988800UL;
  static const uint32_t TIMEOUT = 200;

  IPAddress ntpIP;
  WiFi.hostByName("ntp.ntsc.ac.cn", ntpIP);

  byte buf[NTP_PACKET_SIZE];
  memset(buf, 0, NTP_PACKET_SIZE);
  buf[0] = 0xE3;

  udp.beginPacket(ntpIP, 123);
  udp.write(buf, NTP_PACKET_SIZE);
  udp.endPacket();

  unsigned long start = millis();
  while (!udp.parsePacket()) {
    if (millis() - start >= TIMEOUT) {
      Serial.println(F("UDP Timeout"));
      return 0;
    }
  }
  udp.read(buf, NTP_PACKET_SIZE);

  time_t epoch = (buf[40] << 24 | buf[41] << 16 | buf[42] << 8 | buf[43])
                 - SECONDS_FROM_1900_TO_1970;

  return epoch;
}

weather_data fetchWeatherData() {
  static const size_t JSON_BUFSIZE = 512;
  static const size_t PSK_STRLEN = 18;
  static char psk[PSK_STRLEN];

  if (WiFi.getMode() == WIFI_AP)
    return {SHERR, SHERR_NC, 0};

  if (!*psk) {
    EEPROM.begin(PSK_STRLEN);
    memcpy(psk, EEPROM.getConstDataPtr(), PSK_STRLEN);
    EEPROM.end();
  }

  WiFiClient wc;
  HTTPClient hc;

  // TODO hmac req
  if (!hc.begin(wc,
                String(F("http://api.seniverse.com/v3/weather/"
                         "now.json?language=en&location=ip&key="))
                    + psk))
    return {SHERR, SHERR_HTTP_CON, 0};

  int code = hc.GET();
  if (code == 0) {
    hc.end();
    return {SHERR, SHERR_HTTP_GET, 0};
  }
  if (code != HTTP_CODE_OK) {
    hc.end();
    return {SHERR, String(F("HTTP Code ")) + code, 0};
  }

  String json = hc.getString();
  hc.end();

  DynamicJsonDocument doc(JSON_BUFSIZE);
  deserializeJson(doc, json);

  JsonObject result = doc["results"][0];
  const char *location = result["location"]["name"];

  JsonObject result_now = result["now"];
  const char *weather = result_now["text"];

  byte temp = atoi(result_now["temperature"]);

  return {location, weather, temp};
}
