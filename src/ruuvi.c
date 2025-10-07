#include <stdio.h>
#include <stdint.h>

#include <velib/utils/ve_item_utils.h>
#include <velib/vecan/products.h>

#include "ble-dbus.h"
#include "ruuvi.h"
#include "temperature.h"

static int ruuvi_xlate_luminosity_v6(struct VeItem *root, VeVariant *val,
				     uint64_t rawval);
static int ruuvi_xlate_voc_nox(struct VeItem *root, VeVariant *val,
			       uint64_t rawval);

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

// Format 6 (Bluetooth 4 compatible)
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
		.xlate	= ruuvi_xlate_voc_nox,
		.name	= "VOC",
		.format	= &veUnitIndex,
	},
	{
		.type	= VE_UN8,
		.offset	= 12,
		.xlate	= ruuvi_xlate_voc_nox,
		.name	= "NOX",
		.format	= &veUnitIndex,
	},
	{
		.type	= VE_UN8,
		.offset	= 13,
		.xlate	= ruuvi_xlate_luminosity_v6,
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

static const struct dev_info ruuvi_airquality = {
	.dev_class	= &temperature_class,
	.product_id	= VE_PROD_ID_RUUVI_AIR,
	.dev_instance	= 20,
	.dev_prefix	= "ruuviaq_",
	.num_regs	= array_size(ruuvi_format6),
	.regs		= ruuvi_format6,
};

static int ruuvi_xlate_luminosity_v6(struct VeItem *root, VeVariant *val, uint64_t rv)
{
	// Logarithmic decoding for format 6
	const float MAX_VALUE = 65535.0;
	const float MAX_CODE = 254.0;
	const float DELTA = logf(MAX_VALUE + 1) / MAX_CODE;
	float lux;

	if (rv == 0xff)
		return -1;

	lux = expf(rv * DELTA) - 1;
	veVariantFloat(val, lux);
	return 0;
}

static int ruuvi_xlate_voc_nox(struct VeItem *root, VeVariant *val, uint64_t rv)
{
	// Extract 9-bit value: 8 bits from main field + 1 bit from flags
	struct VeItem *flags_item;
	VeVariant flags_val;
	uint64_t flag_bit;
	int is_nox = (rv >> 16) & 1;  // Use bit to distinguish VOC vs NOX

	flags_item = veItemByUid(root, "Flags");
	if (!flags_item || !veItemIsValid(flags_item))
		return -1;

	veItemLocalValue(flags_item, &flags_val);
	veVariantToN32(&flags_val);

	flag_bit = is_nox ?
		((flags_val.value.UN32 >> 7) & 1) :  // NOX bit 9
		((flags_val.value.UN32 >> 6) & 1);   // VOC bit 9

	uint64_t value = (rv & 0xFF) | (flag_bit << 8);

	if (value == 0x1FF)
		return -1;

	veVariantUn16(val, value);
	return 0;
}

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

int ruuvi_handle_mfg(const bdaddr_t *addr, const uint8_t *buf, int len)
{
	const uint8_t *mac;
	struct VeItem *root;
	const struct dev_info *info;
	char name[16];
	char dev[16];
	uint8_t format;

	if (len < 18)
		return -1;

	format = buf[0];

	// Handle different formats
	if (format == 5) {
		// Existing format 5 handling
		if (len != 24)
			return -1;

		mac = buf + 18;
		info = &ruuvi_tag;

		snprintf(dev, sizeof(dev), "%02x%02x%02x%02x%02x%02x",
			 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

		root = ble_dbus_create(dev, info, NULL);
		if (!root)
			return -1;

		snprintf(name, sizeof(name), "Ruuvi %02X%02X", mac[4], mac[5]);

	} else if (format == 6) {
		// Format 6 - air quality
		if (len < 20)
			return -1;

		mac = buf + 17;  // MAC at offset 17-19
		info = &ruuvi_airquality;

		snprintf(dev, sizeof(dev), "%02x%02x%02x%02x%02x%02x",
			 addr->b[5], addr->b[4], addr->b[3],
			 mac[0], mac[1], mac[2]);

		root = ble_dbus_create(dev, info, NULL);
		if (!root)
			return -1;

		snprintf(name, sizeof(name), "RuuviAQ %02X%02X", mac[1], mac[2]);

	} else {
		return -1;
	}

	ble_dbus_set_name(root, name);

	if (!ble_dbus_is_enabled(root))
		return 0;

	ble_dbus_set_regs(root, buf, len);
	ble_dbus_update(root);

	return 0;
}
