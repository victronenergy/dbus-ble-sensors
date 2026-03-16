#ifndef VICTRON_H
#define VICTRON_H


#include <stdint.h>
#include <bluetooth/bluetooth.h>

struct victron_device {
	const struct dev_info *dev_info;
	const char *def_name;
};

int victron_handle_mfg(const bdaddr_t *addr, const uint8_t *buf, int len);

#endif
