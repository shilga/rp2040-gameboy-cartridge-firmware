#include "GbRtc.h"
#include "GlobalDefines.h"
#include <stdint.h>

#include <hardware/timer.h>

static volatile uint8_t _registerMasks[] = {0x3f, 0x3f, 0x1f, 0xff, 0xc1};
static uint8_t _currentRegister = 0;

static uint64_t _lastMilli = 0;
static uint32_t _millies = 0;

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
  uint8_t registerToMask = 0;

  if (!g_rtcReal.reg.status.halt) {
    if ((now - _lastMilli) > 1000U) {
      _lastMilli += 1000;
      _millies++;
    }

    if (_millies >= 1000) {
      g_rtcReal.reg.seconds++;
      _millies = 0;
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
  }
}
