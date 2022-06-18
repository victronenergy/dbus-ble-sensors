#ifndef RUUVI_H
#define RUUVI_H

#include <stdint.h>
#include <bluetooth/bluetooth.h>

#define MFG_ID_RUUVI 0x0499

int ruuvi_handle_mfg(const bdaddr_t *addr, const uint8_t *buf, int len);

#endif
