#ifndef MOPEKA_H
#define MOPEKA_H

#include <stdint.h>
#include <bluetooth/bluetooth.h>

#include "ble-handler.h"

int mopeka_handle_mfg(const bdaddr_t *addr, const uint8_t *buf, int len, enum data_source source);

#endif
