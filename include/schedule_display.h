#pragma once

#include "Arduino.h"
#include <time.h>

int parseScheduleIndex(const String &normalizedKey);
bool shouldShowScheduleEntry(const String &entry, const struct tm &localtime);
void clearScheduleEntries();
void printScheduleEntriesToSerial(const struct tm *localtime = nullptr);
void renderActiveScheduleEntries(const struct tm &localtime);
void reportScheduleEntriesForCurrentTime();
