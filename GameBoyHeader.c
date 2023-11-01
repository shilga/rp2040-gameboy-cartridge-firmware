#include "GameBoyHeader.h"

uint8_t GameBoyHeader_readRamBankCount(const uint8_t *gameptr) {
  static const uint8_t LOOKUP[] = {0, 0, 1, 4, 16, 8};
  const uint8_t value = gameptr[0x0149];

  if (value <= sizeof(LOOKUP)) {
    return LOOKUP[value];
  }

  return 0xFF;
}
