#pragma once

#include "Arduino.h"

uint16_t createColor(uint8_t r, uint8_t g, uint8_t b);

void drawBuildAndSystemInfo();
void drawQrCode(const char *payload, const char *caption);
void drawSetupJoinQrCode();
void drawSetupPortalQrCode();
void drawSetupStatus(const char *line1, const String &line2 = "");
void rebootAfterSetupStatus(const char *line1, const String &line2 = "");
