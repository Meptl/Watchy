#include "Watchy.h"

WatchyRTC Watchy::RTC;
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> Watchy::display(GxEPD2_154_D67(CS, DC, RESET, BUSY));

RTC_DATA_ATTR int hourglassMinutes = 8;
RTC_DATA_ATTR int targetMinute;
RTC_DATA_ATTR int guiState;
RTC_DATA_ATTR int menuIndex;
RTC_DATA_ATTR BMA423 sensor;
RTC_DATA_ATTR bool displayFullInit = true;

Watchy::Watchy() {} //constructor

void Watchy::init(String datetime) {
    esp_sleep_wakeup_cause_t wakeup_reason;
    wakeup_reason = esp_sleep_get_wakeup_cause(); //get wake up reason
    Wire.begin(SDA, SCL); //init i2c
    RTC.init();

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
            _bmaConfig();
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
    RTC.clearAlarm(); //resets the alarm flag in the RTC
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
                        setTime();
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
                            setTime();
                            break;
                        default:
                            break;
                    }
                }
            }else if(digitalRead(BACK_BTN_PIN) == 1) {
                lastTimeout = millis();
                if(guiState == MAIN_MENU_STATE) {//exit to watch face if already in menu
                    RTC.clearAlarm(); //resets the alarm flag in the RTC
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
                    showFastMenu(menuIndex);
                }
            }else if(digitalRead(DOWN_BTN_PIN) == 1) {
                lastTimeout = millis();
                if(guiState == MAIN_MENU_STATE) {//decrement menu index
                    menuIndex++;
                    if(menuIndex > MENU_LENGTH - 1) {
                        menuIndex = 0;
                    }
                    showFastMenu(menuIndex);
                }
            }
        }
    }
}

void Watchy::showMenu(byte menuIndex, bool partialRefresh){
    display.setFullWindow();
    display.fillScreen(GxEPD_BLACK);
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
            display.fillRect(x1-1, y1-10, 200, h+15, GxEPD_WHITE);
            display.setTextColor(GxEPD_BLACK);
            display.println(menuItems[i]);
        }else{
            display.setTextColor(GxEPD_WHITE);
            display.println(menuItems[i]);
        }
    }

    display.display(partialRefresh);

    guiState = MAIN_MENU_STATE;
}

void Watchy::showFastMenu(byte menuIndex) {
    display.setFullWindow();
    display.fillScreen(GxEPD_BLACK);
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
            display.fillRect(x1-1, y1-10, 200, h+15, GxEPD_WHITE);
            display.setTextColor(GxEPD_BLACK);
            display.println(menuItems[i]);
        }else{
            display.setTextColor(GxEPD_WHITE);
            display.println(menuItems[i]);
        }
    }

    display.display(true);

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

    if (nextMinute < currentMinute) {
        RTC.setAlarm(nextMinute + 60 - currentMinute);
        drawHourglass(minutesRemaining, nextMinute + 60 - currentMinute);
    } else {
        RTC.setAlarm(nextMinute - currentMinute);
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

    while(1) {

        if(digitalRead(MENU_BTN_PIN) == 1) {
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

    showMenu(menuIndex, false);

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
    display.setCursor(5, 120);
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

uint16_t Watchy::_readRegister(uint8_t address, uint8_t reg, uint8_t *data, uint16_t len)
{
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.endTransmission();
    Wire.requestFrom((uint8_t)address, (uint8_t)len);
    uint8_t i = 0;
    while (Wire.available()) {
        data[i++] = Wire.read();
    }
    return 0;
}

uint16_t Watchy::_writeRegister(uint8_t address, uint8_t reg, uint8_t *data, uint16_t len)
{
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.write(data, len);
    return (0 !=  Wire.endTransmission());
}

void Watchy::_bmaConfig() {
    if (sensor.begin(_readRegister, _writeRegister, delay) == false) {
        //fail to init BMA
        return;
    }

    // Accel parameter structure
    Acfg cfg;
    /*!
      Output data rate in Hz, Optional parameters:
      - BMA4_OUTPUT_DATA_RATE_0_78HZ
      - BMA4_OUTPUT_DATA_RATE_1_56HZ
      - BMA4_OUTPUT_DATA_RATE_3_12HZ
      - BMA4_OUTPUT_DATA_RATE_6_25HZ
      - BMA4_OUTPUT_DATA_RATE_12_5HZ
      - BMA4_OUTPUT_DATA_RATE_25HZ
      - BMA4_OUTPUT_DATA_RATE_50HZ
      - BMA4_OUTPUT_DATA_RATE_100HZ
      - BMA4_OUTPUT_DATA_RATE_200HZ
      - BMA4_OUTPUT_DATA_RATE_400HZ
      - BMA4_OUTPUT_DATA_RATE_800HZ
      - BMA4_OUTPUT_DATA_RATE_1600HZ
      */
    cfg.odr = BMA4_OUTPUT_DATA_RATE_100HZ;
    /*!
      G-range, Optional parameters:
      - BMA4_ACCEL_RANGE_2G
      - BMA4_ACCEL_RANGE_4G
      - BMA4_ACCEL_RANGE_8G
      - BMA4_ACCEL_RANGE_16G
      */
    cfg.range = BMA4_ACCEL_RANGE_2G;
    /*!
      Bandwidth parameter, determines filter configuration, Optional parameters:
      - BMA4_ACCEL_OSR4_AVG1
      - BMA4_ACCEL_OSR2_AVG2
      - BMA4_ACCEL_NORMAL_AVG4
      - BMA4_ACCEL_CIC_AVG8
      - BMA4_ACCEL_RES_AVG16
      - BMA4_ACCEL_RES_AVG32
      - BMA4_ACCEL_RES_AVG64
      - BMA4_ACCEL_RES_AVG128
      */
    cfg.bandwidth = BMA4_ACCEL_NORMAL_AVG4;

    /*! Filter performance mode , Optional parameters:
      - BMA4_CIC_AVG_MODE
      - BMA4_CONTINUOUS_MODE
      */
    cfg.perf_mode = BMA4_CONTINUOUS_MODE;

    // Configure the BMA423 accelerometer
    sensor.setAccelConfig(cfg);

    // Enable BMA423 accelerometer
    // Warning : Need to use feature, you must first enable the accelerometer
    // Warning : Need to use feature, you must first enable the accelerometer
    sensor.enableAccel();

    struct bma4_int_pin_config config ;
    config.edge_ctrl = BMA4_LEVEL_TRIGGER;
    config.lvl = BMA4_ACTIVE_HIGH;
    config.od = BMA4_PUSH_PULL;
    config.output_en = BMA4_OUTPUT_ENABLE;
    config.input_en = BMA4_INPUT_DISABLE;
    // The correct trigger interrupt needs to be configured as needed
    sensor.setINTPinConfig(config, BMA4_INTR1_MAP);

    struct bma423_axes_remap remap_data;
    remap_data.x_axis = 1;
    remap_data.x_axis_sign = 0xFF;
    remap_data.y_axis = 0;
    remap_data.y_axis_sign = 0xFF;
    remap_data.z_axis = 2;
    remap_data.z_axis_sign = 0xFF;
    // Need to raise the wrist function, need to set the correct axis
    sensor.setRemapAxes(&remap_data);

    // Enable BMA423 isStepCounter feature
    sensor.enableFeature(BMA423_STEP_CNTR, true);
    // Enable BMA423 isTilt feature
    sensor.enableFeature(BMA423_TILT, true);
    // Enable BMA423 isDoubleClick feature
    sensor.enableFeature(BMA423_WAKEUP, true);

    // Reset steps
    sensor.resetStepCounter();

    // Turn on feature interrupt
    sensor.enableStepCountInterrupt();
    sensor.enableTiltInterrupt();
    // It corresponds to isDoubleClick interrupt
    sensor.enableWakeupInterrupt();
}
