#include <Arduino.h>
#include <SimpleDHT.h>
#include <LiquidCrystal_I2C.h>
#include <FlagTicker.h>
#include "remote.hxx"

#define DHT_PIN D0
#define FAN_PIN D6
#define HEAT_PIN D7
#define HUMID_PIN D8

#define LCD_ADDR 0x27
#define LCD_COLS 20
#define LCD_ROWS 4

#define CONTROL_INTERVAL 10
#define TIME_INTERVAL 60
#define WEATHER_INTERVAL 300

// D1 = SCL, D2 = SDA

SimpleDHT11 dht(DHT_PIN);
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

/* Example display:
  [    07-11  14:20    ]
  [I -12C 30%H / O -10C]
  [Thundershower with  ]
  [ Hail   (Yinchuan)  ]
*/

FlagTicker ft_control;
FlagTicker ft_time;
FlagTicker ft_weather;

struct
{
    tm time;
    String location;
    int in_temp;
    int in_humid;
    int dht_last_err;
    int online_temp;
    String weather;
} display_data;

void updateControl();
void updateTime();
void updateWeather();

void setup()
{
    Serial.begin(115200);
    Serial.println();

    pinMode(FAN_PIN, OUTPUT);
    pinMode(HEAT_PIN, OUTPUT);
    pinMode(HUMID_PIN, OUTPUT);

    lcd.init();
    lcd.backlight();
    lcd.print(F("Initializing"));

    remote::connect();
    lcd.clear();

    ft_control.begin(CONTROL_INTERVAL);
    ft_time.begin(TIME_INTERVAL);
    ft_weather.begin(WEATHER_INTERVAL);
}

void loop()
{
    bool update_lcd = ft_control | ft_time | ft_weather;
    if (ft_control)
    {
        updateControl();
        ft_control.done();
    }
    if (ft_time)
    {
        updateTime();
        ft_time.done();
    }
    if (ft_weather)
    {
        updateWeather();
        ft_weather.done();
    }

    if (!update_lcd)
        // no need to update lcd
        return;

    lcd.clear();
    lcd.setCursor(4, 0);
    lcd.printf_P(PSTR("%02d-%02d"),
                 display_data.time.tm_mon,
                 display_data.time.tm_mday);
    lcd.setCursor(11, 0);
    lcd.printf_P(PSTR("%02d:%02d"),
                 display_data.time.tm_hour,
                 display_data.time.tm_min);

    lcd.setCursor(0, 1);
    lcd.print(F("I "));
    if (display_data.dht_last_err == SimpleDHTErrSuccess)
        lcd.printf_P(PSTR("%3dC %2d%%H"), display_data.in_temp, display_data.in_humid);
    else
        lcd.printf_P(PSTR("Err! 0x%02X"), display_data.dht_last_err);
    lcd.printf_P(PSTR(" / O %3dC"), display_data.online_temp);

    lcd.setCursor(0, 2);
    const String &ref = display_data.weather;
    if (ref.length() <= LCD_COLS)
    {
        // Weather fits in one line
        lcd.print(ref);
    }
    else
    {
        // "Thundershower with Hail" case
        int sub_idx = -1;
        for (int idx;;)
        {
            idx = ref.indexOf(' ', sub_idx + 1);
            if (idx <= 20 && idx != -1)
                sub_idx = idx;
            else
                break;
        }

        lcd.print(ref.substring(0, sub_idx));
        lcd.setCursor(1, 3);
        lcd.print(ref.substring(sub_idx + 1));
    }

    lcd.setCursor(8, 3);
    lcd.printf_P(PSTR("(%s)"), display_data.location.c_str());

    update_lcd = false;
}

void updateControl()
{
    static const int SHL = 30,
                     STL = 18,
                     STH = 23,
                     WHL = 30,
                     WTL = 23,
                     WTH = 28;

    float temp, humid;
    int err = dht.read2(&temp, &humid, nullptr);
    display_data.dht_last_err = err > 0xFF ? err >> 8 : err;
    display_data.in_temp = (int)temp;
    display_data.in_humid = (int)humid;

    // summer is from May to Oct, other months are winter
    bool is_summer = ((display_data.time.tm_mon + 1) % 12 / 6);

    bool dht_success = err == SimpleDHTErrSuccess;
    if (!dht_success)
        Serial.printf_P(PSTR("DHT read error %x\r\n"), err);

    // read success is first condition
    digitalWrite(FAN_PIN, dht_success & (temp > (is_summer ? STH : WTH)));
    digitalWrite(HEAT_PIN, dht_success & (temp < (is_summer ? STL : WTL)));
    digitalWrite(HUMID_PIN, dht_success & (humid < (is_summer ? SHL : WHL)));
}

void updateTime()
{
    time_t t;
    time(&t);
    // localtime() returns static variable address
    tm *lt = localtime(&t);
    display_data.time = *lt;
}

void updateWeather()
{
    weather_data data = remote::getWeatherData();
    display_data.online_temp = data.temperature;
    display_data.location = data.location;
    display_data.weather = data.weather;
}
