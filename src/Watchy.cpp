#include "Watchy.h"

WatchyRTC Watchy::RTC;
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> Watchy::display(GxEPD2_154_D67(CS, DC, RESET, BUSY));

RTC_DATA_ATTR int hourglassMinutes = 8;
RTC_DATA_ATTR int targetMinute;
RTC_DATA_ATTR int guiState;
RTC_DATA_ATTR int menuIndex;
RTC_DATA_ATTR bool displayFullInit = true;
RTC_DATA_ATTR bool alarmSet;

Watchy::Watchy() {} //constructor

void Watchy::init(String datetime) {
    esp_sleep_wakeup_cause_t wakeup_reason;
    wakeup_reason = esp_sleep_get_wakeup_cause(); //get wake up reason
    Wire.begin(SDA, SCL); //init i2c
    RTC.init();

    alarmSet = false;

    // Init the display here for all cases, if unused, it will do nothing
    display.init(0, displayFullInit, 10, true); // 10ms by spec, and fast pulldown reset
    /* display.epd2.setBusyCallback(displayBusyCallback); */

    switch (wakeup_reason)
    {
        case ESP_SLEEP_WAKEUP_EXT0: //RTC Alarm
            switch (guiState) {
                case WATCHFACE_STATE:
                    showWatchFace(true);
                    break;
                case HOURGLASS_STATE:
                    showHourglass(true);
                    break;
            }
        case ESP_SLEEP_WAKEUP_EXT1: //button Press
            handleButtonPress();
            break;
        default: //reset
            RTC.config(datetime);
            showWatchFace(false); //full update on reset
            break;
    }
    deepSleep();
}

void Watchy::displayBusyCallback(const void*){
    gpio_wakeup_enable((gpio_num_t)BUSY, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_light_sleep_start();
}

void Watchy::deepSleep() {
    display.hibernate();
    displayFullInit = false; // Notify not to init it again
    if (!alarmSet) {
        RTC.clearAlarm(); //resets the alarm flag in the RTC
    }
    // Set pins 0-39 to input to avoid power leaking out
    for(int i=0; i<40; i++) {
        pinMode(i, INPUT);
    }
    esp_sleep_enable_ext0_wakeup(RTC_PIN, 0); //enable deep sleep wake on RTC interrupt
    esp_sleep_enable_ext1_wakeup(BTN_PIN_MASK, ESP_EXT1_WAKEUP_ANY_HIGH); //enable deep sleep wake on button press
    esp_deep_sleep_start();
}

void Watchy::handleButtonPress() {
    uint64_t wakeupBit = esp_sleep_get_ext1_wakeup_status();
    //Menu Button
    if (wakeupBit & MENU_BTN_MASK) {
        switch (guiState) {
            case WATCHFACE_STATE:
            case HOURGLASS_STATE:
                showMenu(menuIndex, false);
                break;
            case MAIN_MENU_STATE:
                switch (menuIndex) {
                    case 0:
                        setTime();
                        break;
                    case 1:
                        setHourglass();
                        break;
                }
                break;
        }
    }
    //Back Button
    else if (wakeupBit & BACK_BTN_MASK) {
        switch (guiState) {
            case MAIN_MENU_STATE: //exit to watch face if already in menu
                showWatchFace(false);
                break;
            case APP_STATE:
                showMenu(menuIndex, false); //exit to menu if already in app
                break;
        }
    }
    //Up Button
    else if (wakeupBit & UP_BTN_MASK) {
        switch (guiState) {
            case MAIN_MENU_STATE:
                menuIndex--;
                if(menuIndex < 0) {
                    menuIndex = MENU_LENGTH - 1;
                }
                showMenu(menuIndex, true);
                break;
        }
    }
    //Down Button
    else if (wakeupBit & DOWN_BTN_MASK) {
        switch (guiState) {
            case WATCHFACE_STATE:
            case HOURGLASS_STATE:
                // TODO: Add some pauses if the seconds hand is close to 60 or 0. I
                // don't know if minor variances in the RTC can cause our calculated
                // pauses to overshoot the target time causing the hourglass to reset to
                // 60 minutes.
                targetMinute = (RTC.getMinute() + hourglassMinutes) % 60;
                showHourglass(false);
                break;
            case MAIN_MENU_STATE:
                menuIndex++;
                if(menuIndex > MENU_LENGTH - 1) {
                    menuIndex = 0;
                }
                showMenu(menuIndex, true);
                break;
        }
    }

    switch (guiState) {
        case WATCHFACE_STATE:
        case HOURGLASS_STATE:
            return;
    }

    /***************** fast menu *****************/
    bool timeout = false;
    long lastTimeout = millis();
    pinMode(MENU_BTN_PIN, INPUT);
    pinMode(BACK_BTN_PIN, INPUT);
    pinMode(UP_BTN_PIN, INPUT);
    pinMode(DOWN_BTN_PIN, INPUT);
    while(!timeout) {
        if(millis() - lastTimeout > 5000) {
            timeout = true;
        }else{
            if(digitalRead(MENU_BTN_PIN) == 1) {
                lastTimeout = millis();
                if(guiState == MAIN_MENU_STATE) {//if already in menu, then select menu item
                    switch(menuIndex)
                    {
                        case 0:
                            setTime();
                            break;
                        case 1:
                            setHourglass();
                            break;
                        default:
                            break;
                    }
                }
            }else if(digitalRead(BACK_BTN_PIN) == 1) {
                lastTimeout = millis();
                if(guiState == MAIN_MENU_STATE) {//exit to watch face if already in menu
                    showWatchFace(false);
                    break; //leave loop
                }else if(guiState == APP_STATE) {
                    showMenu(menuIndex, false);//exit to menu if already in app
                }
            }else if(digitalRead(UP_BTN_PIN) == 1) {
                lastTimeout = millis();
                if(guiState == MAIN_MENU_STATE) {//increment menu index
                    menuIndex--;
                    if(menuIndex < 0) {
                        menuIndex = MENU_LENGTH - 1;
                    }
                    showMenu(menuIndex, true);
                }
            }else if(digitalRead(DOWN_BTN_PIN) == 1) {
                lastTimeout = millis();
                if(guiState == MAIN_MENU_STATE) {//decrement menu index
                    menuIndex++;
                    if(menuIndex > MENU_LENGTH - 1) {
                        menuIndex = 0;
                    }
                    showMenu(menuIndex, true);
                }
            }
        }
    }
}

void Watchy::showMenu(byte menuIndex, bool partialRefresh){
    display.setFullWindow();
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&FreeMonoBold9pt7b);

    int16_t  x1, y1;
    uint16_t w, h;
    int16_t yPos;

    const char *menuItems[] = {"Set Time", "Set Hourglass"};
    for(int i=0; i<MENU_LENGTH; i++) {
        yPos = 30+(MENU_HEIGHT*i);
        display.setCursor(0, yPos);
        if(i == menuIndex) {
            display.getTextBounds(menuItems[i], 0, yPos, &x1, &y1, &w, &h);
            display.fillRect(x1-1, y1-10, 200, h+15, GxEPD_BLACK);
            display.setTextColor(GxEPD_WHITE);
            display.println(menuItems[i]);
        }else{
            display.setTextColor(GxEPD_BLACK);
            display.println(menuItems[i]);
        }
    }

    display.display(partialRefresh);

    guiState = MAIN_MENU_STATE;
}

void Watchy::showHourglass(bool partialRefresh) {
    guiState = HOURGLASS_STATE;
    int currentMinute = RTC.getMinute();
    if (currentMinute == targetMinute) {
        vibMotor(20, 2);
        showWatchFace(false);
        return;
    }

    int minutesRemaining = 0;
    if (currentMinute < targetMinute) {
        minutesRemaining = targetMinute - currentMinute;
    } else {
        minutesRemaining = 60 - currentMinute + targetMinute;
    }

    setNextAlarm(currentMinute, minutesRemaining);
}

void Watchy::setNextAlarm(int currentMinute, int minutesRemaining) {
    float percentageComplete = (float)(hourglassMinutes - minutesRemaining) / hourglassMinutes;
    int fills = (int)(percentageComplete / (1.0 / HOURGLASS_SEGMENTS));

    float nextFillPercentage = (fills + 1) * (1.0 / HOURGLASS_SEGMENTS);
    int nextMinute = 0;
    if (nextFillPercentage >= 0.99) {
        nextMinute = targetMinute;
    } else {
        int startMinute = targetMinute - hourglassMinutes;
        if (startMinute < 0) {
            startMinute += 60;
        }

        nextMinute = startMinute + (int)(nextFillPercentage * hourglassMinutes + 1);
        if (nextMinute > 59) {
            nextMinute -= 60;
        }
    }

    alarmSet = true;
    if (nextMinute < currentMinute) {
        RTC.clearAlarm(nextMinute + 60 - currentMinute);
        drawHourglass(minutesRemaining, nextMinute + 60 - currentMinute);
    } else {
        RTC.clearAlarm(nextMinute - currentMinute);
        drawHourglass(minutesRemaining, nextMinute - currentMinute);
    }
}

void Watchy::vibMotor(uint8_t intervalMs, uint8_t length) {
    pinMode(VIB_MOTOR_PIN, OUTPUT);
    bool motorOn = false;
    for(int i=0; i<length; i++) {
        motorOn = !motorOn;
        digitalWrite(VIB_MOTOR_PIN, motorOn);
        delay(intervalMs);
    }
}

void Watchy::setTime() {
    guiState = APP_STATE;

    RTC.read(currentTime);

    int8_t minute = currentTime.Minute;
    int8_t hour = currentTime.Hour;
    int8_t day = currentTime.Day;
    int8_t month = currentTime.Month;
    int8_t year = currentTime.Year;

    int8_t setIndex = SET_HOUR;

    int8_t blink = 0;

    pinMode(DOWN_BTN_PIN, INPUT);
    pinMode(UP_BTN_PIN, INPUT);
    pinMode(MENU_BTN_PIN, INPUT);
    pinMode(BACK_BTN_PIN, INPUT);

    display.setFullWindow();

    long initTime = millis();
    while(1) {
        // Prevent bounce of the keypress that entered this state.
        if (millis() - initTime > 1000 && digitalRead(MENU_BTN_PIN) == 1) {
            setIndex++;
            if(setIndex > SET_DAY) {
                break;
            }
        }
        if(digitalRead(BACK_BTN_PIN) == 1) {
            if(setIndex != SET_HOUR) {
                setIndex--;
            }
        }

        blink = 1 - blink;

        if(digitalRead(DOWN_BTN_PIN) == 1) {
            blink = 1;
            switch(setIndex) {
                case SET_HOUR:
                    hour == 23 ? (hour = 0) : hour++;
                    break;
                case SET_MINUTE:
                    minute == 59 ? (minute = 0) : minute++;
                    break;
                case SET_YEAR:
                    year == 99 ? (year = 0) : year++;
                    break;
                case SET_MONTH:
                    month == 12 ? (month = 1) : month++;
                    break;
                case SET_DAY:
                    day == 31 ? (day = 1) : day++;
                    break;
                default:
                    break;
            }
        }

        if(digitalRead(UP_BTN_PIN) == 1) {
            blink = 1;
            switch(setIndex) {
                case SET_HOUR:
                    hour == 0 ? (hour = 23) : hour--;
                    break;
                case SET_MINUTE:
                    minute == 0 ? (minute = 59) : minute--;
                    break;
                case SET_YEAR:
                    year == 0 ? (year = 99) : year--;
                    break;
                case SET_MONTH:
                    month == 1 ? (month = 12) : month--;
                    break;
                case SET_DAY:
                    day == 1 ? (day = 31) : day--;
                    break;
                default:
                    break;
            }
        }

        display.fillScreen(GxEPD_BLACK);
        display.setTextColor(GxEPD_WHITE);
        display.setFont(&DSEG7_Classic_Bold_53);

        display.setCursor(5, 80);
        if(setIndex == SET_HOUR) {//blink hour digits
            display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
        }
        if(hour < 10) {
            display.print("0");
        }
        display.print(hour);

        display.setTextColor(GxEPD_WHITE);
        display.print(":");

        display.setCursor(108, 80);
        if(setIndex == SET_MINUTE) {//blink minute digits
            display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
        }
        if(minute < 10) {
            display.print("0");
        }
        display.print(minute);

        display.setTextColor(GxEPD_WHITE);

        display.setFont(&FreeMonoBold9pt7b);
        display.setCursor(45, 150);
        if(setIndex == SET_YEAR) {//blink minute digits
            display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
        }
        display.print(2000+year);

        display.setTextColor(GxEPD_WHITE);
        display.print("/");

        if(setIndex == SET_MONTH) {//blink minute digits
            display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
        }
        if(month < 10) {
            display.print("0");
        }
        display.print(month);

        display.setTextColor(GxEPD_WHITE);
        display.print("/");

        if(setIndex == SET_DAY) {//blink minute digits
            display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
        }
        if(day < 10) {
            display.print("0");
        }
        display.print(day);
        display.display(true); //partial refresh
    }

    tmElements_t tm;
    tm.Month = month;
    tm.Day = day;
    tm.Year = year;
    tm.Hour = hour;
    tm.Minute = minute;
    tm.Second = 0;

    RTC.set(tm);

    showWatchFace(false);
}

void Watchy::setHourglass() {
    guiState = APP_STATE;
    bool blink = false;

    pinMode(DOWN_BTN_PIN, INPUT);
    pinMode(UP_BTN_PIN, INPUT);
    pinMode(MENU_BTN_PIN, INPUT);
    pinMode(BACK_BTN_PIN, INPUT);

    display.setFullWindow();

    int currentHourglassMinutes = hourglassMinutes;
    long initTime = millis();
    while(1) {
        // Prevent bounce of the keypress that entered this state.
        if (millis() - initTime > 1000 && digitalRead(MENU_BTN_PIN) == 1) {
            hourglassMinutes = currentHourglassMinutes;
            break;
        }

        if (digitalRead(BACK_BTN_PIN) == 1) {
            break;
        }
        blink = !blink;
        if (digitalRead(DOWN_BTN_PIN) == 1) {
            currentHourglassMinutes -= 1;
        }
        if (digitalRead(UP_BTN_PIN) == 1) {
            currentHourglassMinutes++;
        }
        if (currentHourglassMinutes > 59) {
            currentHourglassMinutes = 59;
        } else if (currentHourglassMinutes < 1) {
            currentHourglassMinutes = 1;
        }

        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
        display.setFont(&DSEG7_Classic_Bold_53);
        display.setCursor(35, 130);

        if (currentHourglassMinutes < 10) {
            display.print("0");
        }
        display.print(currentHourglassMinutes);

        display.setFont(&FreeMonoBold9pt7b);
        display.setTextColor(GxEPD_BLACK);
        display.print(" min");
        display.display(true); //partial refresh
    }
    showWatchFace(false);
}

void Watchy::showWatchFace(bool partialRefresh) {
    RTC.read(currentTime);
    display.setFullWindow();
    display.fillScreen(GxEPD_WHITE);
    drawWatchFace();
    display.display(partialRefresh); //partial refresh
    guiState = WATCHFACE_STATE;
}

void Watchy::drawWatchFace() {
    drawTime();
    drawBattery(154, 5);
}

void Watchy::drawHourglass(int minutesRemaining, int i) {
    display.init(0, false); //_initial_refresh to false to prevent full update on init
    display.setFullWindow();
    display.fillScreen(GxEPD_WHITE);

    display.setTextColor(GxEPD_BLACK);
    display.setFont(&DSEG7_Classic_Bold_53);
    display.setCursor(4, 4);
    display.println(i);

    float percentageComplete = (float)(hourglassMinutes - minutesRemaining) / hourglassMinutes;
    int fillHeight = (int)((float)DISPLAY_HEIGHT / HOURGLASS_SEGMENTS);
    int fills = (int)(percentageComplete / (1.0 / HOURGLASS_SEGMENTS));

    display.fillScreen(GxEPD_WHITE);
    for (int i = 0; i < fills; i++) {
        display.fillRect(0, DISPLAY_HEIGHT - fillHeight * i, DISPLAY_WIDTH, fillHeight, GxEPD_BLACK);
    }
    display.display(false);
}

void Watchy::drawTime() {
    display.setTextColor(GxEPD_BLACK);

    display.setFont(&DSEG7_Classic_Bold_53);
    display.setCursor(5, 130);
    if(currentTime.Hour < 10) {
        display.print("0");
    }
    display.print(currentTime.Hour);
    display.print(":");
    if(currentTime.Minute < 10) {
        display.print("0");
    }
    display.println(currentTime.Minute);
}

void Watchy::drawBattery(uint8_t x, uint8_t y) {
    const uint8_t BATTERY_SEGMENT_WIDTH = 7;
    const uint8_t BATTERY_SEGMENT_HEIGHT = 11;
    const uint8_t BATTERY_SEGMENT_SPACING = 9;

    display.drawBitmap(x, y, battery, 37, 21, GxEPD_BLACK);
    display.fillRect(x + 5, y + 5, 27, BATTERY_SEGMENT_HEIGHT, GxEPD_WHITE); //clear battery HOURGLASS_SEGMENTS
    int8_t batteryLevel = 0;
    float VBAT = getBatteryVoltage();
    if(VBAT > 4.0) {
        batteryLevel = 3;
    } else if(VBAT > 3.80) {
        batteryLevel = 2;
    } else if(VBAT > 3.55) {
        batteryLevel = 1;
    } else {
        batteryLevel = 0;
    }

    for(int8_t batterySegments = 0; batterySegments < batteryLevel; batterySegments++) {
        display.fillRect(x + 5 + (batterySegments * BATTERY_SEGMENT_SPACING), y + 5, BATTERY_SEGMENT_WIDTH, BATTERY_SEGMENT_HEIGHT, GxEPD_BLACK);
    }
}


float Watchy::getBatteryVoltage() {
    if(RTC.rtcType == DS3231) {
        return analogReadMilliVolts(V10_ADC_PIN) / 1000.0f * 2.0f; // Battery voltage goes through a 1/2 divider.
    }else{
        return analogReadMilliVolts(V15_ADC_PIN) / 1000.0f * 2.0f;
    }
}
