#ifndef SAFIERY_H
#define SAFIERY_H

#include <stdint.h>
#include <bluetooth/bluetooth.h>

#include "ble-handler.h"

int safiery_handle_mfg(const bdaddr_t *addr, const uint8_t *buf, int len, enum data_source source);

#endif
