#ifndef ADCD92E4_74E6_47EB_87C7_018CDAC9B005
#define ADCD92E4_74E6_47EB_87C7_018CDAC9B005

#include <stdint.h>

struct __attribute__((packed)) TimePoint {
  uint8_t Second;
  uint8_t Minute;
  uint8_t Hour;
  uint8_t Day;
  uint8_t Month;
  uint8_t Year; // offset from 1970;
};

void GbRtc_WriteRegister(uint8_t val);
void GbRtc_ActivateRegister(uint8_t reg);
void GbRtc_PerformRtcTick();
void GbRtc_advanceToNewTimestamp(uint64_t newTimestamp);

uint64_t makeTime(const struct TimePoint *tp);
void breakTime(uint64_t timeInput, struct TimePoint *tp);

#endif /* ADCD92E4_74E6_47EB_87C7_018CDAC9B005 */
