#include <stdio.h>
#include <stdint.h>

#include <velib/utils/ve_item_utils.h>
#include <velib/vecan/products.h>

#include "ble-dbus.h"
#include "ruuvi.h"
#include "temperature.h"

static const struct reg_info ruuvi_rawv2[] = {
	{
		.type	= VE_SN16,
		.offset	= 1,
		.scale	= 200,
		.inval	= 0x8000,
		.flags	= REG_FLAG_BIG_ENDIAN | REG_FLAG_INVALID,
		.name	= "Temperature",
		.format	= &veUnitCelsius1Dec,
	},
	{
		.type	= VE_UN16,
		.offset	= 3,
		.scale	= 400,
		.inval	= 0xffff,
		.flags	= REG_FLAG_BIG_ENDIAN | REG_FLAG_INVALID,
		.name	= "Humidity",
		.format	= &veUnitPercentage,
	},
	{
		.type	= VE_UN16,
		.offset	= 5,
		.scale	= 100,
		.bias	= 500,
		.inval	= 0xffff,
		.flags	= REG_FLAG_BIG_ENDIAN | REG_FLAG_INVALID,
		.name	= "Pressure",
		.format	= &veUnitHectoPascal,
	},
	{
		.type	= VE_SN16,
		.offset	= 7,
		.scale	= 1000,
		.inval	= 0x8000,
		.flags	= REG_FLAG_BIG_ENDIAN | REG_FLAG_INVALID,
		.name	= "AccelX",
		.format	= &veUnitG2Dec,
	},
	{
		.type	= VE_SN16,
		.offset	= 9,
		.scale	= 1000,
		.inval	= 0x8000,
		.flags	= REG_FLAG_BIG_ENDIAN | REG_FLAG_INVALID,
		.name	= "AccelY",
		.format	= &veUnitG2Dec,
	},
	{
		.type	= VE_SN16,
		.offset	= 11,
		.scale	= 1000,
		.inval	= 0x8000,
		.flags	= REG_FLAG_BIG_ENDIAN | REG_FLAG_INVALID,
		.name	= "AccelZ",
		.format	= &veUnitG2Dec,
	},
	{
		.type	= VE_UN16,
		.offset	= 13,
		.shift	= 5,
		.bits	= 11,
		.scale	= 1000,
		.bias	= 1.6,
		.inval	= 0x3ff,
		.flags	= REG_FLAG_BIG_ENDIAN | REG_FLAG_INVALID,
		.name	= "BatteryVoltage",
		.format	= &veUnitVolt2Dec,
	},
	{
		.type	= VE_UN8,
		.offset	= 14,
		.shift	= 0,
		.bits	= 5,
		.scale	= 0.5,
		.bias	= -40,
		.inval	= 0x1f,
		.flags	= REG_FLAG_BIG_ENDIAN | REG_FLAG_INVALID,
		.name	= "TxPower",
		.format	= &veUnitdBm,
	},
	{
		.type	= VE_UN16,
		.offset	= 16,
		.inval	= 0xffff,
		.flags	= REG_FLAG_BIG_ENDIAN | REG_FLAG_INVALID,
		.name	= "SeqNo",
		.format	= &veUnitNone,
	},
};

static float ruuvi_lowbat(struct VeItem *root, const struct alarm *alarm)
{
	struct VeItem *temp;
	VeVariant val;
	float low = 2.5;

	temp = veItemByUid(root, "Temperature");
	if (!temp)
		return low;

	veItemLocalValue(temp, &val);
	veVariantToFloat(&val);

	if (val.value.Float < -20)
		low = 2.0;
	else if (val.value.Float < 0)
		low = 2.3;

	return low;
}

static const struct alarm ruuvi_alarms[] = {
	{
		.name	= "LowBattery",
		.item	= "BatteryVoltage",
		.hyst	= 0.4,
		.get_level = ruuvi_lowbat,
	},
};

static const struct dev_info ruuvi_tag = {
	.dev_class	= &temperature_class,
	.product_id	= VE_PROD_ID_RUUVI_TAG,
	.dev_instance	= 20,
	.dev_prefix	= "ruuvi_",
	.num_regs	= array_size(ruuvi_rawv2),
	.regs		= ruuvi_rawv2,
	.num_alarms	= array_size(ruuvi_alarms),
	.alarms		= ruuvi_alarms,
};

static int ruuvi_xlate_9bit(struct VeItem *root, VeVariant *val, uint64_t rv,
			    int flag_bit)
{
	uint32_t flags = veItemValueInt(root, "Flags");
	uint32_t value;

	if (flags > 255)
		return -1;

	value = (rv << 1) | ((flags >> flag_bit) & 1);

	if (value == 0x1ff)
		return -1;

	veVariantUn32(val, value);

	return 0;
}

static int ruuvi_xlate_voc(struct VeItem *root, VeVariant *val, uint64_t rv)
{
	return ruuvi_xlate_9bit(root, val, rv, 6);
}

static int ruuvi_xlate_nox(struct VeItem *root, VeVariant *val, uint64_t rv)
{
	return ruuvi_xlate_9bit(root, val, rv, 7);
}

static int ruuvi_xlate_lum(struct VeItem *root, VeVariant *val, uint64_t rv)
{
	float scale = 16 / M_LOG2E / 254;
	float lux = expf((uint8_t)rv * scale) - 1;

	veVariantFloat(val, lux);

	return 0;
}

/* Format 6 (Bluetooth 4 compatible) */
static const struct reg_info ruuvi_format6[] = {
	{
		.type	= VE_SN16,
		.offset	= 1,
		.scale	= 200,
		.inval	= 0x8000,
		.flags	= REG_FLAG_BIG_ENDIAN | REG_FLAG_INVALID,
		.name	= "Temperature",
		.format	= &veUnitCelsius1Dec,
	},
	{
		.type	= VE_UN16,
		.offset	= 3,
		.scale	= 400,
		.inval	= 0xffff,
		.flags	= REG_FLAG_BIG_ENDIAN | REG_FLAG_INVALID,
		.name	= "Humidity",
		.format	= &veUnitPercentage,
	},
	{
		.type	= VE_UN16,
		.offset	= 5,
		.scale	= 100,
		.bias	= 500,
		.inval	= 0xffff,
		.flags	= REG_FLAG_BIG_ENDIAN | REG_FLAG_INVALID,
		.name	= "Pressure",
		.format	= &veUnitHectoPascal,
	},
	{
		.type	= VE_UN16,
		.offset	= 7,
		.scale	= 10,
		.inval	= 0xffff,
		.flags	= REG_FLAG_BIG_ENDIAN | REG_FLAG_INVALID,
		.name	= "PM25",
		.format	= &veUnitUgM3,
	},
	{
		.type	= VE_UN16,
		.offset	= 9,
		.inval	= 0xffff,
		.flags	= REG_FLAG_BIG_ENDIAN | REG_FLAG_INVALID,
		.name	= "CO2",
		.format	= &veUnitPPM,
	},
	{
		.type	= VE_UN8,
		.offset	= 16,
		.name	= "Flags",
		.format	= &veUnitNone,
	},
	{
		.type	= VE_UN8,
		.offset	= 11,
		.xlate	= ruuvi_xlate_voc,
		.name	= "VOC",
		.format	= &veUnitIndex,
	},
	{
		.type	= VE_UN8,
		.offset	= 12,
		.xlate	= ruuvi_xlate_nox,
		.name	= "NOX",
		.format	= &veUnitIndex,
	},
	{
		.type	= VE_UN8,
		.offset	= 13,
		.inval	= 0xff,
		.flags	= REG_FLAG_INVALID,
		.xlate	= ruuvi_xlate_lum,
		.name	= "Luminosity",
		.format	= &veUnitLux,
	},
	{
		.type	= VE_UN8,
		.offset	= 15,
		.name	= "SeqNo",
		.format	= &veUnitNone,
	},
};

static const struct dev_info ruuvi_air = {
	.dev_class	= &temperature_class,
	.product_id	= VE_PROD_ID_RUUVI_AIR,
	.dev_instance	= 20,
	.dev_prefix	= "ruuvi_",
	.num_regs	= array_size(ruuvi_format6),
	.regs		= ruuvi_format6,
};

int ruuvi_handle_mfg(const bdaddr_t *addr, const uint8_t *buf, int len)
{
	const uint8_t *mac = addr->b;
	const struct dev_info *info;
	struct VeItem *root;
	char name[16];
	char dev[16];
	char *label;

	if (len < 1)
		return -1;

	switch (buf[0]) {
	case 5:			/* Format 5, aka RAWv2 */
		if (len != 24)
			return -1;

		info = &ruuvi_tag;
		label = "Ruuvi";
		break;

	case 6:			/* Format 6 */
		if (len != 20)
			return -1;

		info = &ruuvi_air;
		label = "Ruuvi Air";
		break;

	default:
		return -1;
	}

	snprintf(dev, sizeof(dev), "%02x%02x%02x%02x%02x%02x",
		 mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);

	root = ble_dbus_create(dev, info, NULL);
	if (!root)
		return -1;

	snprintf(name, sizeof(name), "%s %02X%02X", label, mac[1], mac[0]);
	ble_dbus_set_name(root, name);

	if (!ble_dbus_is_enabled(root))
		return 0;

	ble_dbus_set_regs(root, buf, len);
	ble_dbus_update(root);

	return 0;
}
