#include "schedule_display.h"

#include "app_state.h"

bool parsePriorityPrefix(const String &entry, int &groupPriority, int &eventPriority, String &coreEntry)
{
  int firstSeparator = entry.indexOf('~');
  int secondSeparator;
  String firstPart;
  String secondPart;
  String thirdPart;

  groupPriority = 1;
  eventPriority = 1;
  coreEntry = entry;

  if (firstSeparator == -1)
  {
    return true;
  }

  firstPart = entry.substring(0, firstSeparator);
  secondSeparator = entry.indexOf('~', firstSeparator + 1);

  // Legacy single-prefix support: "N~rule|..."
  if (secondSeparator == -1)
  {
    firstPart.trim();
    if ((firstPart.length() == 1) && isDigit(firstPart.charAt(0)))
    {
      groupPriority = 1;
      eventPriority = firstPart.toInt();
      coreEntry = entry.substring(firstSeparator + 1);
      coreEntry.trim();
    }
    return true;
  }

  secondPart = entry.substring(firstSeparator + 1, secondSeparator);
  thirdPart = entry.substring(secondSeparator + 1);
  firstPart.trim();
  secondPart.trim();
  thirdPart.trim();

  if ((firstPart.length() == 1) && isDigit(firstPart.charAt(0)))
  {
    groupPriority = firstPart.toInt();
  }

  if ((secondPart.length() == 1) && isDigit(secondPart.charAt(0)))
  {
    eventPriority = secondPart.toInt();
  }
  else
  {
    // Two-prefix form is expected to carry explicit event priority.
    // If malformed, treat it as lowest priority by default.
    eventPriority = 9;
  }

  coreEntry = thirdPart;
  return true;
}

bool parseTimeSpec(const String &value, int &minutesSinceMidnight, bool &wildcard)
{
  int separatorIndex;
  String hourPart;
  String minutePart;

  wildcard = false;
  minutesSinceMidnight = 0;

  if (value == "*")
  {
    wildcard = true;
    return true;
  }

  separatorIndex = value.indexOf(':');
  if (separatorIndex == -1)
  {
    return false;
  }

  hourPart = value.substring(0, separatorIndex);
  minutePart = value.substring(separatorIndex + 1);
  hourPart.trim();
  minutePart.trim();

  for (int index = 0; index < hourPart.length(); ++index)
  {
    if (!isDigit(hourPart.charAt(index)))
    {
      return false;
    }
  }

  for (int index = 0; index < minutePart.length(); ++index)
  {
    if (!isDigit(minutePart.charAt(index)))
    {
      return false;
    }
  }

  int hour = hourPart.toInt();
  int minute = minutePart.toInt();
  if ((hour < 0) || (hour > 23) || (minute < 0) || (minute > 59))
  {
    return false;
  }

  minutesSinceMidnight = (hour * 60) + minute;
  return true;
}

String formatMinutesForDisplay(int minutesSinceMidnight)
{
  char buffer[16];
  int hour = minutesSinceMidnight / 60;
  int minute = minutesSinceMidnight % 60;

  if (tformat == "24")
  {
    snprintf(buffer, sizeof(buffer), "%02d:%02d", hour, minute);
    return String(buffer);
  }

  int hour12 = hour % 12;
  const char *suffix = (hour < 12) ? "am" : "pm";
  if (hour12 == 0)
  {
    hour12 = 12;
  }

  snprintf(buffer, sizeof(buffer), "%2d:%02d%s", hour12, minute, suffix);
  return String(buffer);
}

String formatScheduleDisplayRange(const String &entry)
{
  int groupPriority;
  int eventPriority;
  String coreEntry;
  int firstSeparator = entry.indexOf('|');
  int secondSeparator;
  int thirdSeparator;
  String startSpec;
  String endSpec;
  int startMinutes = 0;
  int endMinutes = 0;
  bool startWildcard = false;
  bool endWildcard = false;

  parsePriorityPrefix(entry, groupPriority, eventPriority, coreEntry);
  firstSeparator = coreEntry.indexOf('|');
  if (firstSeparator == -1)
  {
    return "";
  }

  secondSeparator = coreEntry.indexOf('|', firstSeparator + 1);
  if (secondSeparator == -1)
  {
    return "";
  }

  thirdSeparator = coreEntry.indexOf('|', secondSeparator + 1);
  if (thirdSeparator == -1)
  {
    return "";
  }

  startSpec = coreEntry.substring(firstSeparator + 1, secondSeparator);
  endSpec = coreEntry.substring(secondSeparator + 1, thirdSeparator);
  startSpec.trim();
  endSpec.trim();

  if (!parseTimeSpec(startSpec, startMinutes, startWildcard) ||
      !parseTimeSpec(endSpec, endMinutes, endWildcard))
  {
    return "";
  }

  if (startWildcard && endWildcard)
  {
    return "All Day        ";
  }

  if (startWildcard)
  {
    return String("Until ") + formatMinutesForDisplay(endMinutes) + String("  ");
  }

  if (endWildcard)
  {
    return String("From ") + formatMinutesForDisplay(startMinutes) + String("   ");
  }

  return formatMinutesForDisplay(startMinutes) + "-" + formatMinutesForDisplay(endMinutes);
}

String extractScheduleTitle(const String &entry)
{
  int groupPriority;
  int eventPriority;
  String coreEntry;
  int firstSeparator = entry.indexOf('|');
  int secondSeparator;
  int thirdSeparator;

  parsePriorityPrefix(entry, groupPriority, eventPriority, coreEntry);
  firstSeparator = coreEntry.indexOf('|');
  if (firstSeparator == -1)
  {
    return coreEntry;
  }

  secondSeparator = coreEntry.indexOf('|', firstSeparator + 1);
  if (secondSeparator == -1)
  {
    return coreEntry;
  }

  thirdSeparator = coreEntry.indexOf('|', secondSeparator + 1);
  if (thirdSeparator == -1)
  {
    return coreEntry;
  }

  String title = coreEntry.substring(thirdSeparator + 1);
  title.trim();
  return title;
}

String buildScheduleDisplayText(const String &entry)
{
  String displayRange = formatScheduleDisplayRange(entry);
  String scheduleTitle = extractScheduleTitle(entry);

  if (displayRange == "")
  {
    return scheduleTitle;
  }

  return "[" + displayRange + "] " + scheduleTitle;
}

bool parseScheduleTimeWindow(const String &entry, int &startMinutes, bool &startWildcard, int &endMinutes, bool &endWildcard)
{
  int groupPriority;
  int eventPriority;
  String coreEntry;
  int firstSeparator = entry.indexOf('|');
  int secondSeparator;
  int thirdSeparator;
  String startSpec;
  String endSpec;

  startMinutes = 0;
  endMinutes = 0;
  startWildcard = false;
  endWildcard = false;

  parsePriorityPrefix(entry, groupPriority, eventPriority, coreEntry);
  firstSeparator = coreEntry.indexOf('|');
  if (firstSeparator == -1)
  {
    return false;
  }

  secondSeparator = coreEntry.indexOf('|', firstSeparator + 1);
  if (secondSeparator == -1)
  {
    return false;
  }

  thirdSeparator = coreEntry.indexOf('|', secondSeparator + 1);
  if (thirdSeparator == -1)
  {
    return false;
  }

  startSpec = coreEntry.substring(firstSeparator + 1, secondSeparator);
  endSpec = coreEntry.substring(secondSeparator + 1, thirdSeparator);
  startSpec.trim();
  endSpec.trim();

  return parseTimeSpec(startSpec, startMinutes, startWildcard) &&
         parseTimeSpec(endSpec, endMinutes, endWildcard);
}

int getScheduleSortRank(const String &entry)
{
  int startMinutes = 0;
  int endMinutes = 0;
  bool startWildcard = false;
  bool endWildcard = false;

  if (!parseScheduleTimeWindow(entry, startMinutes, startWildcard, endMinutes, endWildcard))
  {
    return 3;
  }

  if (startWildcard && endWildcard)
  {
    return 0;
  }

  if (startWildcard)
  {
    return 1;
  }

  return 2;
}

bool shouldScheduleEntrySortBefore(const String &leftEntry, const String &rightEntry)
{
  int leftGroupPriority = 1;
  int leftEventPriority = 1;
  int rightGroupPriority = 1;
  int rightEventPriority = 1;
  String leftCoreEntry;
  String rightCoreEntry;
  int leftRank = getScheduleSortRank(leftEntry);
  int rightRank = getScheduleSortRank(rightEntry);
  int leftStartMinutes = 0;
  int rightStartMinutes = 0;
  int leftEndMinutes = 0;
  int rightEndMinutes = 0;
  bool leftStartWildcard = false;
  bool rightStartWildcard = false;
  bool leftEndWildcard = false;
  bool rightEndWildcard = false;
  String leftTitle;
  String rightTitle;

  parsePriorityPrefix(leftEntry, leftGroupPriority, leftEventPriority, leftCoreEntry);
  parsePriorityPrefix(rightEntry, rightGroupPriority, rightEventPriority, rightCoreEntry);

  if (leftGroupPriority != rightGroupPriority)
  {
    return leftGroupPriority < rightGroupPriority;
  }

  if (leftEventPriority != rightEventPriority)
  {
    return leftEventPriority < rightEventPriority;
  }

  if (leftRank != rightRank)
  {
    return leftRank < rightRank;
  }

  parseScheduleTimeWindow(leftEntry, leftStartMinutes, leftStartWildcard, leftEndMinutes, leftEndWildcard);
  parseScheduleTimeWindow(rightEntry, rightStartMinutes, rightStartWildcard, rightEndMinutes, rightEndWildcard);

  if (leftRank == 2 && (leftStartMinutes != rightStartMinutes))
  {
    return leftStartMinutes < rightStartMinutes;
  }

  leftTitle = extractScheduleTitle(leftEntry);
  rightTitle = extractScheduleTitle(rightEntry);
  leftTitle.trim();
  rightTitle.trim();
  leftTitle.toLowerCase();
  rightTitle.toLowerCase();

  return leftTitle < rightTitle;
}

int collectSortedVisibleScheduleIndices(const struct tm &localtime, int destination[], int capacity)
{
  int count = 0;

  for (int index = 0; index < MAX_SCHEDULE_ENTRIES; ++index)
  {
    if (ScheduleEntries[index] == "")
    {
      continue;
    }

    if (!shouldShowScheduleEntry(ScheduleEntries[index], localtime))
    {
      continue;
    }

    if (count < capacity)
    {
      destination[count] = index;
      ++count;
    }
  }

  for (int left = 0; left < count - 1; ++left)
  {
    for (int right = left + 1; right < count; ++right)
    {
      if (shouldScheduleEntrySortBefore(ScheduleEntries[destination[right]], ScheduleEntries[destination[left]]))
      {
        int temp = destination[left];
        destination[left] = destination[right];
        destination[right] = temp;
      }
    }
  }

  return count;
}

bool isCurrentTimeInRange(const struct tm &localtime, const String &startSpec, const String &endSpec)
{
  int currentMinutes = (localtime.tm_hour * 60) + localtime.tm_min;
  int startMinutes = 0;
  int endMinutes = 0;
  bool startWildcard = false;
  bool endWildcard = false;

  if (!parseTimeSpec(startSpec, startMinutes, startWildcard) ||
      !parseTimeSpec(endSpec, endMinutes, endWildcard))
  {
    return false;
  }

  if (startWildcard && endWildcard)
  {
    return true;
  }

  if (startWildcard)
  {
    return currentMinutes <= endMinutes;
  }

  if (endWildcard)
  {
    return currentMinutes >= startMinutes;
  }

  if (startMinutes <= endMinutes)
  {
    return (currentMinutes >= startMinutes) && (currentMinutes <= endMinutes);
  }

  return (currentMinutes >= startMinutes) || (currentMinutes <= endMinutes);
}

bool getScheduleEvaluationDate(const struct tm &localtime, const String &startSpec, const String &endSpec, struct tm &evaluationTime)
{
  int currentMinutes = (localtime.tm_hour * 60) + localtime.tm_min;
  int startMinutes = 0;
  int endMinutes = 0;
  bool startWildcard = false;
  bool endWildcard = false;

  evaluationTime = localtime;

  if (!parseTimeSpec(startSpec, startMinutes, startWildcard) ||
      !parseTimeSpec(endSpec, endMinutes, endWildcard))
  {
    return false;
  }

  if (startWildcard || endWildcard)
  {
    return true;
  }

  if ((startMinutes > endMinutes) && (currentMinutes <= endMinutes))
  {
    struct tm shiftedTime = localtime;
    time_t shiftedEpoch;

    shiftedTime.tm_hour = 12;
    shiftedTime.tm_min = 0;
    shiftedTime.tm_sec = 0;
    shiftedTime.tm_isdst = -1;

    shiftedEpoch = mktime(&shiftedTime);
    if (shiftedEpoch == -1)
    {
      return false;
    }

    shiftedEpoch -= 86400;
    localtime_r(&shiftedEpoch, &evaluationTime);
  }

  return true;
}

int parseIsoDateToDayNumber(const String &value)
{
  int year;
  int month;
  int day;

  if (sscanf(value.c_str(), "%d-%d-%d", &year, &month, &day) != 3)
  {
    return -1;
  }

  return year * 10000 + month * 100 + day;
}

bool matchesMonthDay(const struct tm &localtime, const String &value)
{
  int month;
  int day;

  if (sscanf(value.c_str(), "%d-%d", &month, &day) != 2)
  {
    return false;
  }

  return ((localtime.tm_mon + 1) == month) && (localtime.tm_mday == day);
}

bool matchesDayOfWeek(const struct tm &localtime, String value)
{
  static const char *DAY_NAMES[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };

  value.trim();
  value.toUpperCase();
  if (value.length() >= 3)
  {
    value = value.substring(0, 3);
  }
  return value == DAY_NAMES[localtime.tm_wday];
}

long calculateDayDeltaFromAnchor(const struct tm &localtime, int year, int month, int day)
{
  struct tm anchorTime = {};
  struct tm currentTime = localtime;
  time_t anchorEpoch;
  time_t currentEpoch;

  anchorTime.tm_year = year - 1900;
  anchorTime.tm_mon = month - 1;
  anchorTime.tm_mday = day;
  anchorTime.tm_hour = 12;
  anchorTime.tm_min = 0;
  anchorTime.tm_sec = 0;
  anchorTime.tm_isdst = -1;

  currentTime.tm_hour = 12;
  currentTime.tm_min = 0;
  currentTime.tm_sec = 0;
  currentTime.tm_isdst = -1;

  anchorEpoch = mktime(&anchorTime);
  currentEpoch = mktime(&currentTime);
  if ((anchorEpoch == -1) || (currentEpoch == -1))
  {
    return -1;
  }

  return long((currentEpoch - anchorEpoch) / 86400);
}

bool matchesDailyRule(const struct tm &localtime, String ruleValue)
{
  int intervalDays;
  long dayDelta;

  ruleValue.trim();
  if (ruleValue == "")
  {
    return true;
  }
  intervalDays = ruleValue.toInt();
  if (intervalDays <= 0)
  {
    return false;
  }

  dayDelta = calculateDayDeltaFromAnchor(localtime, 1970, 1, 1);
  if (dayDelta < 0)
  {
    return false;
  }

  return ((dayDelta % intervalDays) == 0);
}

bool matchesModRule(const struct tm &localtime, String ruleValue)
{
  int firstSeparator = ruleValue.indexOf(':');
  int intervalDays;
  int anchorDayNumber;
  long dayDelta;

  if (firstSeparator == -1)
  {
    intervalDays = ruleValue.toInt();
    if (intervalDays <= 0)
    {
      return false;
    }

    dayDelta = calculateDayDeltaFromAnchor(localtime, 1970, 1, 1);
    if (dayDelta < 0)
    {
      return false;
    }

    return ((dayDelta % intervalDays) == 0);
  }

  intervalDays = ruleValue.substring(0, firstSeparator).toInt();
  if (intervalDays <= 0)
  {
    return false;
  }

  anchorDayNumber = parseIsoDateToDayNumber(ruleValue.substring(firstSeparator + 1));
  if (anchorDayNumber == -1)
  {
    return false;
  }

  dayDelta = calculateDayDeltaFromAnchor(
    localtime,
    anchorDayNumber / 10000,
    (anchorDayNumber / 100) % 100,
    anchorDayNumber % 100
  );
  if (dayDelta < 0)
  {
    return false;
  }

  return ((dayDelta % intervalDays) == 0);
}

bool doesScheduleEntryMatchDate(const struct tm &localtime, String ruleSpec)
{
  int separatorIndex = ruleSpec.indexOf(':');
  String ruleName = ruleSpec;
  String ruleValue = "";
  int currentDayNumber;

  if (separatorIndex != -1)
  {
    ruleName = ruleSpec.substring(0, separatorIndex);
    ruleValue = ruleSpec.substring(separatorIndex + 1);
  }

  ruleName.trim();
  ruleName.toLowerCase();
  ruleValue.trim();

  if (ruleName == "daily")
  {
    return matchesDailyRule(localtime, ruleValue);
  }

  if (ruleName == "dow")
  {
    return matchesDayOfWeek(localtime, ruleValue);
  }

  if (ruleName == "date")
  {
    currentDayNumber = (localtime.tm_year + 1900) * 10000 + (localtime.tm_mon + 1) * 100 + localtime.tm_mday;
    if (parseIsoDateToDayNumber(ruleValue) != -1)
    {
      return currentDayNumber == parseIsoDateToDayNumber(ruleValue);
    }

    return matchesMonthDay(localtime, ruleValue);
  }

  if (ruleName == "mod")
  {
    return matchesModRule(localtime, ruleValue);
  }

  return false;
}

int parseScheduleIndex(const String &normalizedKey)
{
  const String prefix = "schedule";
  String indexText;

  if (!normalizedKey.startsWith(prefix))
  {
    return -1;
  }

  indexText = normalizedKey.substring(prefix.length());
  if (indexText == "")
  {
    return -1;
  }

  for (int index = 0; index < indexText.length(); ++index)
  {
    if (!isDigit(indexText.charAt(index)))
    {
      return -1;
    }
  }

  int parsedIndex = indexText.toInt();
  if ((parsedIndex < 0) || (parsedIndex >= MAX_SCHEDULE_ENTRIES))
  {
    return -1;
  }

  return parsedIndex;
}

bool shouldShowScheduleEntry(const String &entry, const struct tm &localtime)
{
  int groupPriority;
  int eventPriority;
  String coreEntry;
  int firstSeparator = entry.indexOf('|');
  int secondSeparator;
  int thirdSeparator;
  String ruleSpec;
  String startSpec;
  String endSpec;
  struct tm evaluationTime;

  parsePriorityPrefix(entry, groupPriority, eventPriority, coreEntry);
  firstSeparator = coreEntry.indexOf('|');
  if (firstSeparator == -1)
  {
    return false;
  }

  secondSeparator = coreEntry.indexOf('|', firstSeparator + 1);
  if (secondSeparator == -1)
  {
    return false;
  }

  thirdSeparator = coreEntry.indexOf('|', secondSeparator + 1);
  if (thirdSeparator == -1)
  {
    return false;
  }

  ruleSpec = coreEntry.substring(0, firstSeparator);
  startSpec = coreEntry.substring(firstSeparator + 1, secondSeparator);
  endSpec = coreEntry.substring(secondSeparator + 1, thirdSeparator);

  ruleSpec.trim();
  startSpec.trim();
  endSpec.trim();

  if (!getScheduleEvaluationDate(localtime, startSpec, endSpec, evaluationTime))
  {
    return false;
  }

  return doesScheduleEntryMatchDate(evaluationTime, ruleSpec) &&
         isCurrentTimeInRange(localtime, startSpec, endSpec);
}

void clearScheduleEntries()
{
  for (int index = 0; index < MAX_SCHEDULE_ENTRIES; ++index)
  {
    ScheduleEntries[index] = "";
  }
}

void printScheduleEntriesToSerial(const struct tm *localtime)
{
  int printedCount = 0;

  Serial.println("Loaded alarms:");
  for (int index = 0; index < MAX_SCHEDULE_ENTRIES; ++index)
  {
    if (ScheduleEntries[index] == "")
    {
      continue;
    }

    Serial.print("  schedule");
    Serial.print(index);
    Serial.print("=");
    Serial.print(buildScheduleDisplayText(ScheduleEntries[index]));
    if (localtime != nullptr)
    {
      Serial.print(" -> ");
      Serial.println(shouldShowScheduleEntry(ScheduleEntries[index], *localtime) ? "SHOW" : "hide");
    }
    else
    {
      Serial.println();
    }
    ++printedCount;
  }

  if (printedCount == 0)
  {
    Serial.println("  <none>");
  }
}

void renderActiveScheduleEntries(const struct tm &localtime)
{
  const int boxX = 6;
  const int boxY = 144;
  const int boxW = 320 - boxX;
  const int lineHeight = 32;
  const int boxH = 240 - boxY;
  int visibleIndices[MAX_SCHEDULE_ENTRIES];
  int visibleCount = collectSortedVisibleScheduleIndices(localtime, visibleIndices, MAX_SCHEDULE_ENTRIES);
  int drawnCount = 0;

  tft.fillRect(boxX, boxY, boxW, boxH, TFT_BLACK);
  tft.setTextColor(scheduleTextColor, TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextSize(2);

  for (int visibleIndex = 0; visibleIndex < visibleCount; ++visibleIndex)
  {
    String displayText;
    int index = visibleIndices[visibleIndex];

    displayText = extractScheduleTitle(ScheduleEntries[index]);
    tft.drawString(displayText, boxX, boxY + (drawnCount * lineHeight), 2);
    ++drawnCount;

    if (drawnCount >= MAX_VISIBLE_SCHEDULE_ENTRIES)
    {
      break;
    }
  }

  tft.setTextSize(1);
}

void reportScheduleEntriesForCurrentTime()
{
  struct tm localtime;

  if (!getLocalTime(&localtime, 1000))
  {
    Serial.println("Loaded alarms: unable to evaluate current time");
    return;
  }

  printScheduleEntriesToSerial(&localtime);
}
