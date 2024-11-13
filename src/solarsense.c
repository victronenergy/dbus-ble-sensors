#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include <velib/utils/ve_item_utils.h>
#include <velib/vecan/products.h>

#include "ble-dbus.h"
#include "solarsense.h"

static const struct dev_info solarsense_sensor = {
	.product_id	= VE_PROD_ID_SOLAR_SENSE_750,
	.dev_instance	= 20,
	.dev_prefix	= "solarsense_",
	.role		= "meteo",
};

static const struct reg_info solarsense_adv[] = {
	{
		.type	= VE_UN32,
		.offset	= 8,
		.name	= "ErrorCode",
		.format	= &veUnitNone,
	},
	{
		.type	= VE_UN8,
		.offset	= 12,
		.inval	= 0xff,
		.flags	= REG_FLAG_INVALID,
		.name	= "ChrErrorCode",
		.format	= &veUnitNone,
	},
	{
		.type	= VE_UN8,
		.offset	= 13,
		.scale	= 1,
		.bias	= -60,
		.inval	= 0xff,
		.flags	= REG_FLAG_INVALID,
		.name	= "CellTemperature",
		.format	= &veUnitCelsius1Dec,
	},
	{
		.type	= VE_UN8,
		.offset	= 14,
		.mask	= 0x00ff,
		.scale	= 100,
		.bias	= 1.7,
		.inval	= 0xff,
		.flags	= REG_FLAG_INVALID,
		.name	= "BatteryVoltage",
		.format = &veUnitVolt2Dec,
	},
	{
		.type	= VE_UN32,
		.offset = 15,
		.scale	= 1,
		.mask	= 0xfffff,
		.inval	= 0xfffff,
		.flags	= REG_FLAG_INVALID,
		.name	= "InstallationPower",
		.format = &veUnitWatt,
	},
	{
		.type	= VE_UN32,
		.offset = 17,
		.shift	= 4,
		.scale	= 100,
		.mask	= 0xfffff,
		.inval	= 0xfffff,
		.flags	= REG_FLAG_INVALID,
		.name	= "TodaysYield",
		.format = &veUnitKiloWattHour,
	},
	{
		.type	= VE_UN16,
		.offset	= 20,
		.mask	= 0x3fff,
		.scale	= 10,
		.inval	= 0x3fff,
		.flags	= REG_FLAG_INVALID,
		.name	= "Irradiance",
		.format = &veUnitIrradiance1Dec,
	},
};

static void solarsense_update_alarms(struct VeItem *devroot)
{
	struct VeItem *batv;
	struct VeItem *lowbat;

	VeVariant val;
	float low = 3.2;
	int lb;

	batv = veItemByUid(devroot, "BatteryVoltage");
	if (!batv)
		return;

	lowbat = veItemGetOrCreateUid(devroot, "Alarms/LowBattery");
	if (!lowbat)
		return;

	if (veItemIsValid(lowbat)) {
		veItemLocalValue(lowbat, &val);
		veVariantToN32(&val);

		if (val.value.UN32)
			low += 0.4;
	}

	veItemLocalValue(batv, &val);
	veVariantToFloat(&val);

	if (val.value.Float < low)
		lb = 1;
	else
		lb = 0;

	veVariantUn32(&val, lb);
	veItemOwnerSet(lowbat, &val);
}


int solarsense_handle_mfg(const bdaddr_t *addr, const uint8_t *buf, int len)
{
	struct VeItem *root;
	char name[24];
	char dev[16];

	if (len < 22)
		return -1;

	if ( buf[0] != 0x10 || buf[4] != 0xff || buf[7] != 0x01 )
		return -1;

	snprintf(dev, sizeof(dev), "%02x%02x%02x%02x%02x%02x",
		 addr->b[5], addr->b[4], addr->b[3],
		 addr->b[2], addr->b[1], addr->b[0]);

	root = ble_dbus_create(dev, &solarsense_sensor, NULL);
	if (!root)
		return -1;

	snprintf(name, sizeof(name), "SolarSense %02X%02X",
		 addr->b[1], addr->b[0]);
	ble_dbus_set_name(root, name);

	if (!ble_dbus_is_enabled(root))
		return 0;

	ble_dbus_set_regs(root, solarsense_adv, array_size(solarsense_adv),
			  buf, len);

	solarsense_update_alarms(root);
	ble_dbus_update(root);

	return 0;
}

