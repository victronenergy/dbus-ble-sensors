#ifndef VICTRON_LSBMS_H
#define VICTRON_LSBMS_H

#include "victron.h"

extern const struct victron_device lsbms_victron_device;
extern veBool lsbms_is_supported(const uint8_t *buf, int len);

#endif
