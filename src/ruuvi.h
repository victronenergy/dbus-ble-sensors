#ifndef RUUVI_H
#define RUUVI_H

#include <stdint.h>

#define MFG_ID_RUUVI 0x0499

int ruuvi_handle_mfg(const uint8_t *buf, int len);

#endif
