#ifndef GARNET_H
#define GARNET_H


#include <stdint.h>
#include <bluetooth/bluetooth.h>

int garnet_handle_mfg(const bdaddr_t *addr, const uint8_t *buf, int len);

#endif // GARNET_H
