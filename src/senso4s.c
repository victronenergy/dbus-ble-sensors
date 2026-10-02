#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <velib/vecan/products.h>

#include "ble-dbus.h"
#include "senso4s.h"
#include "tank.h"

static int senso4s_level(struct VeItem *root, VeVariant *val, uint64_t rawval)
{
	if (rawval > 100)
		return -1;

	veVariantFloat(val, rawval);
	return 0;
}

static const struct reg_info senso4s_adv[] = {
	{
		.type	= VE_UN8,
		.offset = 1,
		.xlate	= senso4s_level,
		.name	= "RawValue",
		.format = &veUnitPercentage,
	},
	{
		.type	= VE_UN8,
		.offset = 4,
		.name	= "BatteryVoltage",
		.format = &veUnitPercentage,
	},
};

static const struct tank_info senso4s_tank_info = {
	.default_fluid_type = FLUID_TYPE_LPG,
	.raw_unit	    = "%",
	.raw_min	    = 0,
	.raw_max	    = 100,
	.raw_empty	    = 0,
	.raw_full	    = 100,
};

static const struct dev_info senso4s_sensor = {
	.dev_class    = &tank_class,
	.product_id   = VE_PROD_ID_TANK_SENSOR,
	.dev_instance = 20,
	.dev_prefix   = "senso4s_",
	.num_regs     = array_size(senso4s_adv),
	.regs	      = senso4s_adv,
};

static int senso4s_matches_address(const bdaddr_t *addr, const uint8_t *buf)
{
	int i;
	int forward = 1;
	int reverse = 1;

	for (i = 0; i < 6; i++) {
		if (buf[6 + i] != addr->b[i])
			forward = 0;
		if (buf[6 + i] != addr->b[5 - i])
			reverse = 0;
	}

	return forward || reverse;
}

int senso4s_handle_mfg(const bdaddr_t *addr, const uint8_t *buf, int len,
		       enum data_source source)
{
	struct VeItem *root;
	char dev[16];
	char name[24];

	if (len != 12)
		return -1;

	if ((buf[0] & 0x0f) < 1 || (buf[0] & 0x0f) > 5 || buf[1] > 100 || buf[4] > 100 || !senso4s_matches_address(addr, buf))
		return -1;

	snprintf(dev, sizeof(dev), "%02x%02x%02x%02x%02x%02x", addr->b[5], addr->b[4], addr->b[3],
		 addr->b[2], addr->b[1], addr->b[0]);

	root = ble_dbus_create(dev, &senso4s_sensor, &senso4s_tank_info);
	if (!root)
		return -1;

	if (ble_dbus_check_dup(root, source))
		return 0;

	snprintf(name, sizeof(name), "Senso4s %02X:%02X:%02X", addr->b[2], addr->b[1], addr->b[0]);
	ble_dbus_set_name(root, name, NAME_ORIG_DEVICE);

	if (!ble_dbus_is_enabled(root))
		return 0;

	ble_dbus_set_regs(root, buf, len);
	ble_dbus_update(root);

	return 0;
}