#ifndef MOPEKA_H
#define MOPEKA_H

#include <stdint.h>
#include <bluetooth/bluetooth.h>

int mopeka_handle_mfg(const bdaddr_t *addr, const uint8_t *buf, int len);

#endif
