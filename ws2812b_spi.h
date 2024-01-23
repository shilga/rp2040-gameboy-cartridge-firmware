#ifndef A9FD9FC9_43F8_4ADF_936A_6ADF1E1699A5
#define A9FD9FC9_43F8_4ADF_936A_6ADF1E1699A5

#include <hardware/spi.h>

void ws2812b_spi_init(spi_inst_t *spi);
void ws2812b_setRgb(uint8_t r, uint8_t g, uint8_t b);

#endif /* A9FD9FC9_43F8_4ADF_936A_6ADF1E1699A5 */
