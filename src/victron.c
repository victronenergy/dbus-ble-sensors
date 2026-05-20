#include "victron.h"

#include "ble-dbus.h"
#include "victron-solarsense.h"

#define RECORD_TYPE_TEST_RECORD		    0x0000
#define RECORD_TYPE_SOLAR_CHARGER	    0x0001
#define RECORD_TYPE_BATTERY_MONITOR	    0x0002
#define RECORD_TYPE_INVERTER		    0x0003
#define RECORD_TYPE_DCDC_CONVERTER	    0x0004
#define RECORD_TYPE_SMARTLITHIUM	    0x0005
#define RECORD_TYPE_INVERTER_RS		    0x0006
#define RECORD_TYPE_GX_DEVICE		    0x0007
#define RECORD_TYPE_AC_CHARGER		    0x0008
#define RECORD_TYPE_SMART_BATTERY_PROTECT   0x0009
#define RECORD_TYPE_LYNX_SMART_BMS	    0x000A
#define RECORD_TYPE_MULTI_RS		    0x000B
#define RECORD_TYPE_VE_BUS		    0x000C
#define RECORD_TYPE_DC_ENERGY_METER	    0x000D
#define RECORD_TYPE_SMART_BMS		    0x000E
#define RECORD_TYPE_ORION_XS		    0x000F
#define RECORD_TYPE_UNENCRYPTED_TEST_RECORD 0xFF00
#define RECORD_TYPE_SOLARSENSE		    0xFF01

struct instant_readout_handler {
	uint16_t record_type;
	const struct victron_device *device;
};

static const struct instant_readout_handler instant_readout_handlers[] = {
	{ RECORD_TYPE_SOLARSENSE, &solarsense_victron_device },
};

static int victron_device_init(struct VeItem *droot, const void *data)
{
	const struct instant_readout_handler *instant_readout_handler
		= (const struct instant_readout_handler *)data;
	const struct victron_device *victron_device = instant_readout_handler->device;
	if (victron_device->dev_info->init)
		return victron_device->dev_info->init(droot, NULL);

	return 0;
}

int victron_handle_mfg(const bdaddr_t *addr, const uint8_t *buf, int len, enum data_source source)
{
	int i;
	uint16_t record_type;
	char name[24];
	char dev[16];
	struct VeItem *droot;
	const struct instant_readout_handler *instant_readout_handler = NULL;
	const struct victron_device *victron_device;
	struct dev_info info;
	uint16_t seqnr;

	if (len < 8)
		return -1;

	if (buf[0] != 0x10)
		return -1;

	record_type = buf[4];
	if (record_type == 0xFF)
		record_type = (record_type << 8) | buf[7];

	for (i = 0; i < array_size(instant_readout_handlers); i++) {
		if (record_type == instant_readout_handlers[i].record_type) {
			instant_readout_handler = &instant_readout_handlers[i];
			break;
		}
	}

	if (!instant_readout_handler)
		return -1;

	victron_device = instant_readout_handler->device;

	info		  = *victron_device->dev_info;
	info.product_id	  = bt_get_le16(&buf[2]);
	info.use_ble_name = veTrue;
	info.dev_instance = 20;
	info.init	  = victron_device_init;
	info.seqnr_bits	  = 16;
	info.seqnr_window = 60;
	// Because there are already GX devices in the field with an Enabled setting for a solarsense,
	// we keep the prefix for the solarsense. For all other device we use a more generic prefix.
	// This way, when a device switches instant readout format, it will keep its key and enabled
	// setting.
	info.dev_prefix = victron_device == &solarsense_victron_device ? "solarsense_" : "victron_";
	snprintf(dev, sizeof(dev), "%02x%02x%02x%02x%02x%02x", addr->b[5], addr->b[4], addr->b[3],
		 addr->b[2], addr->b[1], addr->b[0]);
	droot = ble_dbus_create(dev, &info, instant_readout_handler);
	if (!droot)
		return -1;

	snprintf(name, sizeof(name), "%s %02X%02X", victron_device->def_name, addr->b[1], addr->b[0]);
	ble_dbus_set_name(droot, name, NAME_ORIG_DEVICE);

	if (!ble_dbus_is_enabled(droot))
		return 0;

	seqnr = (buf[6] << 8) | buf[5];
	if (ble_dbus_check_dup_seq(droot, source, seqnr))
		return 0;

	if (record_type < 0xFF00) {
		// Not yet supported
	} else {
		ble_dbus_set_regs(droot, buf + 8, len - 8);
	}
	ble_dbus_update(droot);

	return 0;
}
