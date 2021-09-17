#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <velib/utils/ve_item_utils.h>
#include <velib/vecan/products.h>

#include "ble-dbus.h"
#include "ruuvi.h"

static struct VeSettingProperties temp_type = {
	.type		= VE_SN32,
	.def.value.SN32	= 2,
	.min.value.SN32	= 0,
	.max.value.SN32	= 2,
};

static const struct dev_setting ruuvi_settings[] = {
	{
		.name	= "TemperatureType",
		.props	= &temp_type,
	},
};

static const struct dev_info ruuvi_tag = {
	.product_id	= VE_PROD_ID_RUUVI_TAG,
	.dev_instance	= 20,
	.dev_class	= "analog",
	.dev_prefix	= "ruuvi_",
	.role		= "temperature",
	.num_settings	= array_size(ruuvi_settings),
	.settings	= ruuvi_settings,
};

static const VeVariantUnitFmt veUnitKiloPascal1Dec = { 1, "kPa" };
static const VeVariantUnitFmt veUnitG2Dec = { 2, "g" };
static const VeVariantUnitFmt veUnitdBm = { 0, "dBm" };

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
		.scale	= 1000,
		.bias	= 50,
		.inval	= 0xffff,
		.flags	= REG_FLAG_BIG_ENDIAN | REG_FLAG_INVALID,
		.name	= "Pressure",
		.format	= &veUnitKiloPascal1Dec,
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
		.mask	= 0x7ff,
		.scale	= 1000,
		.bias	= 1.6,
		.inval	= 0x3ff,
		.flags	= REG_FLAG_BIG_ENDIAN | REG_FLAG_INVALID,
		.name	= "BatteryVoltage",
		.format	= &veUnitVolt2Dec,
	},
	{
		.type	= VE_UN16,
		.offset	= 13,
		.shift	= 0,
		.mask	= 0x1f,
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

int ruuvi_handle_mfg(const uint8_t *buf, int len)
{
	const uint8_t *mac = buf + 18;
	char dev[16];

	if (len != 24)
		return -1;

	if (buf[0] != 5)
		return -1;

	snprintf(dev, sizeof(dev), "%02x%02x%02x%02x%02x%02x",
		 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	ble_dbus_set_regs(dev, &ruuvi_tag,
			  ruuvi_rawv2, array_size(ruuvi_rawv2),
			  buf, len);

	return 0;
}
