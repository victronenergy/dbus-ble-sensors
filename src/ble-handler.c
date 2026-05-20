#include "ble-handler.h"
#include "ble-dbus.h"
#include "garnet.h"
#include "gobius.h"
#include "mopeka.h"
#include "ruuvi.h"
#include "safiery.h"
#include "victron.h"

struct mfg_data_handler {
	uint16_t id;
	int (*handler)(const bdaddr_t *addr, const uint8_t *buf, int len, enum data_source source);
};

static const struct mfg_data_handler mfg_data_handlers[] = {
	{ MFG_ID_GOBIUS,	gobius_handle_mfg },
	{ MFG_ID_RUUVI,		ruuvi_handle_mfg },
	{ MFG_ID_NORDIC,	mopeka_handle_mfg },
	{ MFG_ID_SAFIERY,	safiery_handle_mfg },
	{ MFG_ID_VICTRON,	victron_handle_mfg },
	{ MFG_ID_GARNET,	garnet_handle_mfg },
};

void ble_handle_name(const bdaddr_t *bdaddr, const uint8_t *buf, int len)
{
	struct VeItem *droot;
	char name[256];
	char dev[16];

	snprintf(dev, sizeof(dev), "%02x%02x%02x%02x%02x%02x", bdaddr->b[5], bdaddr->b[4], bdaddr->b[3],
		 bdaddr->b[2], bdaddr->b[1], bdaddr->b[0]);

	droot = ble_dbus_get_dev(dev);
	if (!droot)
		return;

	if (len >= sizeof(name))
		len = sizeof(name) - 1;

	memcpy(name, buf, len);
	name[len] = 0;
	ble_dbus_set_name(droot, name);
}

int ble_handle_mfg(const bdaddr_t *bdaddr, uint16_t mfg, const uint8_t *buf, int len,
		   enum data_source source)
{
	int i;

	for (i = 0; i < array_size(mfg_data_handlers); i++) {
		if (mfg == mfg_data_handlers[i].id) {
			if (!mfg_data_handlers[i].handler(bdaddr, buf, len, source))
				break;
		}
	}

	return 0;
}
