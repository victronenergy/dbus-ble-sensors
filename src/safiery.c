#include <stdio.h>
#include <stdint.h>

#include <velib/utils/ve_item_utils.h>
#include <velib/vecan/products.h>
#include <velib/utils/ve_logger.h>

#include "ble-dbus.h"
#include "safiery.h"
#include "tank.h"

#define HW_ID_TOPDOWN_BLE		10

static const struct reg_info safiery_adv[] = {
	{
		.type	= VE_UN8,
		.offset	= 0,
		.bits	= 7,
		.name	= "HardwareID",
		.format	= &veUnitNone,
	},
	{
		.type	= VE_UN8,
		.offset	= 1,
		.bits	= 7,
		.scale	= 32,
		.name	= "BatteryVoltage",
		.format = &veUnitVolt2Dec,
	},
	{
		.type	= VE_UN8,
		.offset	= 2,
		.bits	= 7,
		.scale	= 1,
		.bias	= -40,
		.name	= "Temperature",
		.format	= &veUnitCelsius1Dec,
	},
	{
		.type	= VE_UN8,
		.offset	= 2,
		.shift	= 7,
		.bits	= 1,
		.name	= "SyncButton",
		.format = &veUnitNone,
	},
	{
		.type	= VE_UN16,
		.offset	= 3,
		.bits	= 14,
		.scale	= 10,
		.name	= "RawValue",
		.format	= &veUnitcm,
	},
	{
		.type	= VE_SN8,
		.offset	= 8,
		.scale	= 1024,
		.name	= "AccelX",
		.format	= &veUnitG2Dec,
	},
	{
		.type	= VE_SN8,
		.offset	= 9,
		.scale	= 1024,
		.name	= "AccelY",
		.format	= &veUnitG2Dec,
	},
	{
		.type	= VE_SN8,
		.offset	= 10,
		.scale	= 1024,
		.name	= "AccelZ",
		.format	= &veUnitG2Dec,
	},
};

static const struct tank_info safiery_tank_info = {
	.flags		= TANK_FLAG_TOPDOWN,
};

static const struct dev_info safiery_sensor = {
	.dev_class	= &tank_class,
	.product_id	= VE_PROD_ID_SAFIERY_TANK_SENSOR,
	.dev_instance	= 20,
	.dev_prefix	= "safiery_",
	.num_regs	= array_size(safiery_adv),
	.regs		= safiery_adv,
};

int safiery_handle_mfg(const bdaddr_t *addr, const uint8_t *buf, int len)
{
	struct VeItem *root;
	const uint8_t *uid = buf + 5;
	char name[24];
	char dev[16];

	if (len != 10)
		return -1;

	if (uid[0] != addr->b[2] ||
	    uid[1] != addr->b[1] ||
	    uid[2] != addr->b[0])
		return -1;

	snprintf(dev, sizeof(dev), "%02x%02x%02x%02x%02x%02x",
		 addr->b[5], addr->b[4], addr->b[3],
		 addr->b[2], addr->b[1], addr->b[0]);

	root = ble_dbus_create(dev, &safiery_sensor, &safiery_tank_info);
	if (!root)
		return -1;

	snprintf(name, sizeof(name), "StarTank %02X:%02X:%02X",
		uid[0], uid[1], uid[2]);
	ble_dbus_set_name(root, name);

	if (!ble_dbus_is_enabled(root))
		return 0;

	ble_dbus_set_regs(root, buf, len);
	ble_dbus_update(root);

	return 0;
}

