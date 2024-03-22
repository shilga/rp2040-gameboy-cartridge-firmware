#include "GbRtc.h"
#include "GlobalDefines.h"
#include <stdint.h>

#include <hardware/timer.h>

#define SECS_PER_MIN 60UL
#define SECS_PER_HOUR 3600UL
#define SECS_PER_DAY (SECS_PER_HOUR * 24UL)
#define SECS_PER_YEAR (SECS_PER_DAY * 365UL)
#define SECS_YR_2000 946684800UL /* the time at the start of y2k */

// leap year calculator expects year argument as years offset from 1970
#define LEAP_YEAR(Y)                                                           \
  (((1970 + (Y)) > 0) && !((1970 + (Y)) % 4) &&                                \
   (((1970 + (Y)) % 100) || !((1970 + (Y)) % 400)))

static volatile uint8_t _registerMasks[] = {0x3f, 0x3f, 0x1f, 0xff, 0xc1};
static uint8_t _currentRegister = 0;

static uint64_t _lastMilli = 0;
static uint32_t _millies = 0;

static uint64_t _lastTimestampTick = 0;

static inline void GbRtc_processTick();

void __no_inline_not_in_flash_func(GbRtc_WriteRegister)(uint8_t val) {
  const uint8_t oldHalt = g_rtcReal.reg.status.halt;

  g_rtcReal.asArray[_currentRegister] = val & _registerMasks[_currentRegister];

  if (_currentRegister == 0) {
    _lastMilli = time_us_64();
    _millies = 0;
  }

  if (oldHalt && !g_rtcReal.reg.status.halt) {
    _lastMilli = time_us_64();
  }
}

void __no_inline_not_in_flash_func(GbRtc_ActivateRegister)(uint8_t reg) {
  if (reg >= sizeof(_registerMasks)) {
    return;
  }

  _rtcLatchPtr = &g_rtcLatched.asArray[reg];
  _currentRegister = reg;
}

void __no_inline_not_in_flash_func(GbRtc_PerformRtcTick)() {
  uint64_t now = time_us_64();

  if (!g_rtcReal.reg.status.halt) {
    if ((now - _lastMilli) > 1000U) {
      _lastMilli += 1000;
      _millies++;
    }

    if (_millies >= 1000) {
      _millies = 0;
      GbRtc_processTick();
    }
  }

  if ((now - _lastTimestampTick) > 1000000U) {
    g_rtcTimestamp++;
    _lastTimestampTick = now;
  }
}

static inline void GbRtc_processTick() {
  uint8_t registerToMask = 0;

  g_rtcReal.reg.seconds++;
  g_rtcReal.asArray[registerToMask] &= _registerMasks[registerToMask];
  registerToMask++;

  if (g_rtcReal.reg.seconds == 60) {
    g_rtcReal.reg.seconds = 0;
    g_rtcReal.reg.minutes++;

    g_rtcReal.asArray[registerToMask] &= _registerMasks[registerToMask];
    registerToMask++;

    if (g_rtcReal.reg.minutes == 60) {
      g_rtcReal.reg.minutes = 0;
      g_rtcReal.reg.hours++;

      g_rtcReal.asArray[registerToMask] &= _registerMasks[registerToMask];
      registerToMask++;
    }

    if (g_rtcReal.reg.hours == 24) {
      g_rtcReal.reg.hours = 0;
      g_rtcReal.reg.days++;

      g_rtcReal.asArray[registerToMask] &= _registerMasks[registerToMask];
      registerToMask++;

      if (g_rtcReal.reg.days == 0) {
        if (g_rtcReal.reg.status.days_high) {
          g_rtcReal.reg.status.days_carry = 1;
        }
        g_rtcReal.reg.status.days_high++;
      }
    }
  }
}

void GbRtc_advanceToNewTimestamp(uint64_t newTimestamp) {
  uint64_t diff;
  if (newTimestamp > g_rtcTimestamp) {
    diff = newTimestamp - g_rtcTimestamp;

    while (diff > 0) {
      GbRtc_processTick();
      diff--;
    }
  }

  g_rtcTimestamp = newTimestamp;

  _millies = 0;
}

static const uint8_t monthDays[] = {31, 28, 31, 30, 31, 30,
                                    31, 31, 30, 31, 30, 31};

// taken from https://github.com/PaulStoffregen/Time/blob/master/Time.cpp
uint64_t makeTime(const struct TimePoint *tp) {
  // note year argument is offset from 1970 (see macros in time.h to convert to
  // other formats)
  int i;
  uint64_t seconds;

  // seconds from 1970 till 1 jan 00:00:00 of the given year
  seconds = tp->Year * (SECS_PER_DAY * 365);
  for (i = 0; i < tp->Year; i++) {
    if (LEAP_YEAR(i)) {
      seconds += SECS_PER_DAY; // add extra days for leap years
    }
  }

  // add days for this year, months start from 1
  for (i = 0; i < tp->Month; i++) {
    if ((i == 1) && LEAP_YEAR(tp->Year)) {
      seconds += SECS_PER_DAY * 29;
    } else {
      seconds += SECS_PER_DAY * monthDays[i];
    }
  }
  seconds += (tp->Day) * SECS_PER_DAY;
  seconds += tp->Hour * SECS_PER_HOUR;
  seconds += tp->Minute * SECS_PER_MIN;
  seconds += tp->Second;
  return seconds;
}
void breakTime(uint64_t timeInput, struct TimePoint *tp) {
  // break the given timestamp into components
  // this is a more compact version of the C library localtime function
  // note that year is offset from 1970 !!!

  uint8_t year;
  uint8_t month, monthLength;
  uint32_t time;
  unsigned long days;

  time = (uint32_t)timeInput;
  tp->Second = time % 60;
  time /= 60; // now it is minutes
  tp->Minute = time % 60;
  time /= 60; // now it is hours
  tp->Hour = time % 24;
  time /= 24; // now it is days

  year = 0;
  days = 0;
  while ((unsigned)(days += (LEAP_YEAR(year) ? 366 : 365)) <= time) {
    year++;
  }
  tp->Year = year; // year is offset from 1970

  days -= LEAP_YEAR(year) ? 366 : 365;
  time -= days; // now it is days in this year, starting at 0

  days = 0;
  month = 0;
  monthLength = 0;
  for (month = 0; month < 12; month++) {
    if (month == 1) { // february
      if (LEAP_YEAR(year)) {
        monthLength = 29;
      } else {
        monthLength = 28;
      }
    } else {
      monthLength = monthDays[month];
    }

    if (time >= monthLength) {
      time -= monthLength;
    } else {
      break;
    }
  }
  tp->Month = month;
  tp->Day = time;
}
