#pragma once
#include <stdint.h>

#include "driver/i2c_master.h"
#ifdef __cplusplus
extern "C" {
#endif
/* The BOX's speaker, through its ES8311 on the I2C bus given: stereo
   16-bit frames. */
int audio_open (i2c_master_bus_handle_t bus, int rate, int volume);
void audio_write (const int16_t *frames, int count);
#ifdef __cplusplus
}
#endif
