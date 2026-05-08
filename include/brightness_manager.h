#pragma once

#include "Arduino.h"
#include <time.h>

#ifndef CDS
#define CDS 34
#endif

constexpr int backlightPin = 21;
constexpr int photoResistorPin = CDS;

void logPhotoResistorReading();
int parseClockToMinutes(const String &value);
int clampBrightness(int value);
void applyBrightnessValue(int value);
void setBrightnessFromController(int value, const char *source, bool holdAutoDim, int target = -1, bool logChange = true);
int computePhotoDimStepCount();
void normalizePhotoDimSettings();
int computePhotoTargetBrightness(int rawLightLevel);
void processPhotoBrightness();
int computeAutoTargetBrightness(const struct tm &localtime);
void processAutoBrightness(const struct tm &localtime);
