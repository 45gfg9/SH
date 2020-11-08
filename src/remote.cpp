#include "remote.h"

static const int LED = LED_BUILTIN;

static void gen(int pin, int delay1, int delay2)
{
    digitalWrite(pin, HIGH);
    delay(delay1);
    digitalWrite(pin, LOW);
    delay(delay2);
}

static void connectBlocking(const char *ssid, const char *pass)
{
    pinMode(LED, OUTPUT);
    digitalWrite(LED, LOW);
    Serial.printf_P(PSTR("Connecting to %s. "), ssid);
    WiFi.begin(ssid, pass);

    // returns -1 on timeout (default 60000)
    int8_t status = WiFi.waitForConnectResult();
    if (status == WL_CONNECTED)
    {
        Serial.println(F("Connected!"));
        digitalWrite(LED, HIGH);
    }
    else if (status == WL_CONNECT_FAILED || status == -1)
    {
        Serial.printf_P(PSTR("Failed to connect to %s"), ssid);
        for (int i = 0; i < 10; i++)
            gen(LED, 100, 100);
        ESP.restart();
    }
    else if (status == WL_NO_SSID_AVAIL)
    {
        Serial.printf_P(PSTR("%s not found"), ssid);
        for (int i = 500; i > 0; i -= 50)
            gen(LED, i, i);
        ESP.restart();
    }
}

void remote::begin()
{
    if (EX_SSID != nullptr)
    {
        WiFi.mode(WIFI_AP_STA);
        connectBlocking(EX_SSID, EX_PASS);
    }
    else
    {
        WiFi.mode(WIFI_AP);
    }
    WiFi.softAP(AP_SSID, AP_PASS);

    Serial.print(F("Gateway IP: "));
    Serial.println(WiFi.gatewayIP());
    Serial.print(F("Local IP: "));
    Serial.println(WiFi.localIP());
    Serial.print(F("AP IP: "));
    Serial.println(WiFi.softAPIP());
}

void remote::connect()
{
    WiFi.mode(WIFI_STA);
    connectBlocking(AP_SSID, AP_PASS);

    Serial.print(F("Gateway IP: "));
    Serial.println(WiFi.gatewayIP());
    Serial.print(F("Local IP: "));
    Serial.println(WiFi.localIP());
}

void remote::mDNSsetup(const String &name, int port)
{
    if (!MDNS.begin(name))
    {
        Serial.println(F("Error setting up mDNS responder!"));
        for (int i = 0; i < 500; i += 50)
            gen(LED, i, i);
        ESP.restart();
    }
    Serial.println(F("mDNS responder started"));

    MDNS.addService("http", "tcp", port);
}

void remote::listenTime(UDP &udp)
{
    if (udp.parsePacket())
    {
        Serial.println(F("Receiving UDP packet"));
        time_t t = 0;
        for (size_t i = 0; i < sizeof(time_t); i++)
            t |= udp.read() << 8 * i;
        if (t == 0)
        {
            Serial.println(F("Invalid Time 0"));
            return;
        }

        timeval tv = {t, 0};
        settimeofday(&tv, nullptr);
    }
}

IPAddress remote::getBroadcastIP(const IPAddress &ip, const IPAddress &mask)
{
    IPAddress ret;
    for (int i = 0; i < 4; i++)
        ret[i] = (ip[i] & mask[i]) | ~mask[i];

    return ret;
}

weather_data remote::getWeatherData()
{
    WiFiClient client;

    if (!client.connect(WiFi.gatewayIP(), TCP_PORT))
        return {F("Error"), F("Connection Failed :("), 0};

    Serial.print(F("Connecting to Gateway"));
    while (!client.connected())
    {
        Serial.print('.');
        delay(200);
    }
    Serial.println();
    delay(500);

    Serial.println(client.available());

    int locl = client.read();
    byte loc[locl + 1];
    client.read(loc, locl);
    loc[locl] = 0;
    int wthl = client.read();
    byte wth[wthl + 1];
    client.read(wth, wthl);
    wth[wthl] = 0;
    int tmp = client.read();

    client.stop();

    return {(char *)loc, (char *)wth, tmp};
}