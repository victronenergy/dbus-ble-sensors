#ifndef VICTRON_BSC_H
#define VICTRON_BSC_H

#include "victron.h"

extern const struct victron_device bsc_victron_device;

veBool bsc_is_supported(const uint8_t *buf, int len);

#endif
