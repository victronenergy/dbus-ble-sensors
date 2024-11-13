#ifndef SOLARSENSE_H
#define SOLARSENSE_H

#include <stdint.h>
#include <bluetooth/bluetooth.h>

int solarsense_handle_mfg(const bdaddr_t *addr, const uint8_t *buf, int len);

#endif
