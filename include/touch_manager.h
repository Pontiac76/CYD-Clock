#pragma once

#include "Arduino.h"
#include <XPT2046_Touchscreen.h>

#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33
#define ENABLE_TOUCH 1

void initialize_touch();
void suspend_touch_for_sd();
void resume_touch_after_sd();
void printTouchToSerial(TS_Point p);
void handleSystemIdSwitchTouch();
void processTouchInput();
