#ifndef SENSO4S_H
#define SENSO4S_H

#include "ble-handler.h"

int senso4s_handle_mfg(const bdaddr_t *addr, const uint8_t *buf, int len,
		       enum data_source source);

#endif