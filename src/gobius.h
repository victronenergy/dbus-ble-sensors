#ifndef GOBIUS_H
#define GOBIUS_H

#include <stdint.h>
#include <bluetooth/bluetooth.h>

int gobius_handle_mfg(const bdaddr_t *addr, const uint8_t *buf, int len);

#endif
