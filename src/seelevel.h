#ifndef SEELEVEL_H
#define SEELEVEL_H

#include <bluetooth/bluetooth.h>

int seelevel_handle_mfg(const bdaddr_t *addr, const uint8_t *buf, int len);

#endif

