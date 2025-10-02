#include <velib/vecan/products.h>

#include "ble-dbus.h"
#include "gobius.h"
#include "tank.h"

/*
 * v1.1.1 Manufacturer Specific Data (after the 2-byte Company ID) is 14 bytes:
 *   0    : HardwareID (7 bits used)
 *   1    : Temperature (7 bits used; °C = value - 40), MSB reserved
 *   2-3  : Distance (mm, uint16 LE)
 *   4-6  : UID tail = advertiser address bytes [2:0]
 *   7-9  : Firmware version (major, middle, minor)
 *   10   : Status Flags (ignored here)
 *   11-13: Spare (ignored; expected 0)
 */

static const struct tank_info gobius_tank_info = {
	.flags = TANK_FLAG_TOPDOWN,
};

#define GOBIUS_ERROR	0xffff
#define GOBIUS_STARTUP	0xfffe

static int gobius_level(struct VeItem *root, VeVariant *val, uint64_t rawval)
{
	switch (rawval) {
	case GOBIUS_STARTUP:
	case GOBIUS_ERROR:
		return -1;
	}

	veVariantFloat(val, rawval / 10.0);

	return 0;
}

static const struct reg_info gobius_adv[] = {
	{
		/* Hardware identifier */
		.type	= VE_UN8,
		.offset = 0,
		.bits	= 7,
		.name	= "HardwareID",
		.format = &veUnitNone,
	},
	{
		/* Temperature, raw - 40 => °C */
		.type	= VE_UN8,
		.offset = 1,
		.bits	= 7,
		.scale	= 1,
		.bias	= -40,
		.name	= "Temperature",
		.format = &veUnitCelsius1Dec,
	},
	{
		/* Distance in mm */
		.type	= VE_UN16,
		.offset = 2,
		.scale	= 10,
		.name	= "RawValue",
		.format = &veUnitcm,
		.xlate	= gobius_level,
	},
};

static const struct dev_info gobius_sensor = {
	.dev_class	= &tank_class,
	.product_id	= VE_PROD_ID_GOBIUS_TANK_SENSOR,
	.dev_instance	= 20,
	.dev_prefix	= "gobius_",
	.num_regs	= array_size(gobius_adv),
	.regs		= gobius_adv,
};

int gobius_handle_mfg(const bdaddr_t *addr, const uint8_t *buf, int len)
{
	struct VeItem *root;
	const uint8_t *uid;
	char name[24];
	char dev[16];
	char fw[16];

	/* Expect 14-byte payload after Company ID */
	if (len != 14)
		return -1;

	/* UID tail at payload offsets 4..6 */
	uid = buf + 4;
	if (uid[0] != addr->b[2] ||
	    uid[1] != addr->b[1] ||
	    uid[2] != addr->b[0])
		return -1;

	snprintf(dev, sizeof(dev), "%02x%02x%02x%02x%02x%02x",
		 addr->b[5], addr->b[4], addr->b[3],
		 addr->b[2], addr->b[1], addr->b[0]);

	root = ble_dbus_create(dev, &gobius_sensor, &gobius_tank_info);
	if (!root)
		return -1;

	snprintf(name, sizeof(name), "Gobius C %02X:%02X:%02X",
		 uid[0], uid[1], uid[2]);
	ble_dbus_set_name(root, name);

	if (!ble_dbus_is_enabled(root))
		return 0;

	/* Firmware version at payload offsets 7..9 */
	snprintf(fw, sizeof(fw), "%u.%u.%u", buf[8], buf[8], buf[9]);
	ble_dbus_set_str(root, "/FirmwareVersion", fw);

	ble_dbus_set_regs(root, buf, len);
	ble_dbus_update(root);

	return 0;
}
