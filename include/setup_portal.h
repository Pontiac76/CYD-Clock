#pragma once

#include "Arduino.h"

bool isSetupPortalRunning();
void startSetupPortal();
void refreshSetupPortalDisplay();
void processSetupSubmission();
void processSetupPortal();
bool loadFirstWifiProfileFromLittleFs();
