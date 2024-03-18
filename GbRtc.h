#ifndef ADCD92E4_74E6_47EB_87C7_018CDAC9B005
#define ADCD92E4_74E6_47EB_87C7_018CDAC9B005

#include <stdint.h>

void GbRtc_WriteRegister(uint8_t val);
void GbRtc_ActivateRegister(uint8_t reg);
void GbRtc_PerformRtcTick();
void GbRtc_advanceToNewTimestamp(uint64_t newTimestamp);

#endif /* ADCD92E4_74E6_47EB_87C7_018CDAC9B005 */
