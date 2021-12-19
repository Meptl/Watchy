#ifndef WATCHY_H
#define WATCHY_H

#include <Arduino.h>
#include <Arduino_JSON.h>
#include <GxEPD2_BW.h>
#include <Wire.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include "DSEG7_Classic_Bold_53.h"
#include "WatchyRTC.h"
#include "config.h"
#include "icons.h"

class Watchy {
    public:
        const int HOURGLASS_SEGMENTS = 8;
        static WatchyRTC RTC;
        static GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display;
        tmElements_t currentTime;
    public:
        Watchy();
        void init(String datetime = "");
        void deepSleep();
        static void displayBusyCallback(const void*);
        float getBatteryVoltage();
        void vibMotor(uint8_t intervalMs = 100, uint8_t length = 20);

        void handleButtonPress();
        void showMenu(byte menuIndex, bool partialRefresh);
        void showFastMenu(byte menuIndex);

        void showWatchFace(bool partialRefresh);
        void showHourglass(bool partialRefresh);
        void drawTime();
        void drawBattery(uint8_t x, uint8_t y);
        void drawHourglass(int minutesRemainning, int i);
        void setTime();
        virtual void drawWatchFace(); //override this method for different watch faces

    private:
        void setNextAlarm(int currentMinute, int minutesRemaining);
};

extern RTC_DATA_ATTR int guiState;
extern RTC_DATA_ATTR int menuIndex;
extern RTC_DATA_ATTR BMA423 sensor;

#endif
