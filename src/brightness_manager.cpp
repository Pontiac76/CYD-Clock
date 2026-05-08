#include "brightness_manager.h"

#include "app_state.h"

constexpr unsigned long photoResistorLogIntervalMs = 1000;

int photoResistorBrightRaw = 100;
int photoResistorDarkRaw = 1024;
int photoDimSteps = 10;
int photoDimDeadzone = 2;
int photoDimTargetStep = -1;
int photoDimTargetBrightness = -1;
int brightness = 128; // Brightness (0-255)
int mindim = 32;
int maxdim = 128;
int hourspan = 1;
String sunrise_time = "06:00";
String sunset_time = "18:00";
unsigned long next_auto_dim_ms = 0;
unsigned long auto_dim_resume_ms = 0;
unsigned long next_photoresistor_log_ms = 0;
int autodim_hold_ms = 2000;
int autodim_step_ms = 1000;
int autodim_percent = 10;
bool autodim_debug = false;
unsigned long next_autodim_debug_ms = 0;

void logPhotoResistorReading()
{
  unsigned long nowMs = millis();
  if (nowMs < next_photoresistor_log_ms)
  {
    return;
  }

  int lightLevel = analogRead(photoResistorPin);
  Serial.print("Photoresistor GPIO");
  Serial.print(photoResistorPin);
  Serial.print(" raw=");
  Serial.println(lightLevel);

  next_photoresistor_log_ms = nowMs + photoResistorLogIntervalMs;
}

int parseClockToMinutes(const String &value)
{
  int hour = 0;
  int minute = 0;
  String cleaned = value;
  cleaned.trim();
  if (sscanf(cleaned.c_str(), "%d:%d", &hour, &minute) != 2)
  {
    return -1;
  }
  if ((hour < 0) || (hour > 23) || (minute < 0) || (minute > 59))
  {
    return -1;
  }
  return (hour * 60) + minute;
}

int clampBrightness(int value)
{
  return min(255, max(1, value));
}

void applyBrightnessValue(int value)
{
  brightness = clampBrightness(value);
  analogWrite(backlightPin, brightness);
}

void setBrightnessFromController(int value, const char *source, bool holdAutoDim, int target, bool logChange)
{
  applyBrightnessValue(value);
  if (holdAutoDim)
  {
    auto_dim_resume_ms = millis() + autodim_hold_ms;
  }

  if (!logChange)
  {
    return;
  }

  Serial.print(source);
  Serial.print(" -> ");
  Serial.print(brightness);
  if (target >= 0)
  {
    Serial.print(" target=");
    Serial.print(target);
  }
  Serial.println();
}

int nextBrightnessStepToward(int target)
{
  int nextValue = brightness;
  int gap = abs(target - brightness);
  int step = max(1, (gap * autodim_percent) / 100);

  if (brightness < target)
  {
    nextValue = min(target, brightness + step);
  }
  else if (brightness > target)
  {
    nextValue = max(target, brightness - step);
  }

  return nextValue;
}

int computePhotoDimStepCount()
{
  return constrain(photoDimSteps, 2, 255);
}

void normalizePhotoDimSettings()
{
  photoDimSteps = computePhotoDimStepCount();
  photoDimDeadzone = constrain(photoDimDeadzone, 0, photoDimSteps - 1);
  photoDimTargetStep = -1;
  photoDimTargetBrightness = -1;
}

int computePhotoBrightnessForStep(int stepIndex)
{
  int safeMin = clampBrightness(min(mindim, maxdim));
  int safeMax = clampBrightness(max(mindim, maxdim));
  int stepCount = computePhotoDimStepCount();
  int brightnessRange = safeMax - safeMin;

  if (brightnessRange <= 0)
  {
    return safeMin;
  }

  stepIndex = constrain(stepIndex, 0, stepCount - 1);
  return safeMin + ((brightnessRange * stepIndex) / (stepCount - 1));
}

int computePhotoTargetStep(int rawLightLevel)
{
  int brightRaw = min(photoResistorBrightRaw, photoResistorDarkRaw);
  int darkRaw = max(photoResistorBrightRaw, photoResistorDarkRaw);
  int clampedRaw = constrain(rawLightLevel, brightRaw, darkRaw);
  int stepCount = computePhotoDimStepCount();

  if (brightRaw == darkRaw)
  {
    return stepCount - 1;
  }
  if (rawLightLevel <= brightRaw)
  {
    return stepCount - 1;
  }
  if (rawLightLevel >= darkRaw)
  {
    return 0;
  }

  int rawRange = darkRaw - brightRaw;
  int rawOffsetFromDark = darkRaw - clampedRaw;
  return (rawOffsetFromDark * (stepCount - 1)) / rawRange;
}

int computePhotoTargetBrightness(int rawLightLevel)
{
  return computePhotoBrightnessForStep(computePhotoTargetStep(rawLightLevel));
}

int computePhotoDimDeadzone()
{
  return constrain(photoDimDeadzone, 0, computePhotoDimStepCount() - 1);
}

void processPhotoBrightness()
{
  unsigned long nowMs = millis();
  if (nowMs < auto_dim_resume_ms)
  {
    return;
  }
  if (nowMs < next_auto_dim_ms)
  {
    return;
  }

  int rawLightLevel = analogRead(photoResistorPin);
  int computedStep = computePhotoTargetStep(rawLightLevel);
  int computedTarget = computePhotoBrightnessForStep(computedStep);
  int deadzone = computePhotoDimDeadzone();
  int brightRaw = min(photoResistorBrightRaw, photoResistorDarkRaw);
  int darkRaw = max(photoResistorBrightRaw, photoResistorDarkRaw);
  bool outsidePhotoRange = (rawLightLevel <= brightRaw) || (rawLightLevel >= darkRaw);
  bool targetChanged = (computedStep != photoDimTargetStep);

  if (photoDimTargetStep == -1)
  {
    photoDimTargetStep = computedStep;
    photoDimTargetBrightness = computedTarget;
  }
  else if (targetChanged && (outsidePhotoRange || (abs(computedStep - photoDimTargetStep) >= deadzone)))
  {
    photoDimTargetStep = computedStep;
    photoDimTargetBrightness = computedTarget;
    if (autodim_debug)
    {
      Serial.print("PhotoDim target accepted=");
      Serial.print(photoDimTargetBrightness);
      Serial.print(" step=");
      Serial.print(photoDimTargetStep);
      Serial.print(" computed=");
      Serial.print(computedTarget);
      Serial.print(" deadzone=");
      Serial.print(deadzone);
      if (outsidePhotoRange)
      {
        Serial.print(" outside_range=1");
      }
      Serial.println();
    }
  }

  int nextValue = nextBrightnessStepToward(photoDimTargetBrightness);

  if (nextValue != brightness)
  {
    setBrightnessFromController(nextValue, "PhotoDim", false, photoDimTargetBrightness, autodim_debug);
    if (autodim_debug)
    {
      Serial.print("PhotoDim sensor raw=");
      Serial.print(rawLightLevel);
      Serial.print(" range=");
      Serial.print(brightRaw);
      Serial.print("-");
      Serial.print(darkRaw);
      Serial.print(" step=");
      Serial.print(computedStep);
      Serial.print(" computed=");
      Serial.print(computedTarget);
      Serial.print(" deadzone=");
      Serial.println(deadzone);
    }
  }

  next_auto_dim_ms = nowMs + max(10, autodim_step_ms);
}

int computeAutoTargetBrightness(const struct tm &localtime)
{
  int sunriseMinutes = parseClockToMinutes(sunrise_time);
  int sunsetMinutes = parseClockToMinutes(sunset_time);
  int currentMinutes = (localtime.tm_hour * 60) + localtime.tm_min;
  int safeMin = clampBrightness(min(mindim, maxdim));
  int safeMax = clampBrightness(max(mindim, maxdim));
  int transitionMinutes = max(2, hourspan * 60);
  int halfWindow = transitionMinutes / 2;

  if ((sunriseMinutes == -1) || (sunsetMinutes == -1) || (sunriseMinutes >= sunsetMinutes))
  {
    return safeMax;
  }

  int sunriseStart = sunriseMinutes - halfWindow;
  int sunriseEnd = sunriseMinutes + halfWindow;
  int sunsetStart = sunsetMinutes - halfWindow;
  int sunsetEnd = sunsetMinutes + halfWindow;

  if (currentMinutes < sunriseStart)
  {
    return safeMin;
  }
  if (currentMinutes < sunriseEnd)
  {
    int numerator = (currentMinutes - sunriseStart) * (safeMax - safeMin);
    int denominator = max(1, sunriseEnd - sunriseStart);
    return safeMin + (numerator / denominator);
  }
  if (currentMinutes < sunsetStart)
  {
    return safeMax;
  }
  if (currentMinutes < sunsetEnd)
  {
    int numerator = (currentMinutes - sunsetStart) * (safeMax - safeMin);
    int denominator = max(1, sunsetEnd - sunsetStart);
    return safeMax - (numerator / denominator);
  }

  return safeMin;
}

void processAutoBrightness(const struct tm &localtime)
{
  unsigned long nowMs = millis();
  if (nowMs < auto_dim_resume_ms)
  {
    if (autodim_debug && (nowMs >= next_autodim_debug_ms))
    {
      int target = computeAutoTargetBrightness(localtime);
      Serial.print("AutoDim HOLD current=");
      Serial.print(brightness);
      Serial.print(" target=");
      Serial.print(target);
      Serial.print(" resume_in_ms=");
      Serial.println(auto_dim_resume_ms - nowMs);
      next_autodim_debug_ms = nowMs + 5000;
    }
    return;
  }
  if (nowMs < next_auto_dim_ms)
  {
    return;
  }

  int target = computeAutoTargetBrightness(localtime);
  int nextValue = nextBrightnessStepToward(target);

  if (nextValue != brightness)
  {
    setBrightnessFromController(nextValue, "AutoDim STEP", false, target, autodim_debug);
  }
  else if (autodim_debug && (nowMs >= next_autodim_debug_ms))
  {
    Serial.print("AutoDim IDLE current=");
    Serial.print(brightness);
    Serial.print(" target=");
    Serial.println(target);
    next_autodim_debug_ms = nowMs + 5000;
  }

  next_auto_dim_ms = nowMs + max(10, autodim_step_ms);
}
