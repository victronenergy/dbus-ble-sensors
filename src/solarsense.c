#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include <velib/utils/ve_item_utils.h>
#include <velib/vecan/products.h>

#include "ble-dbus.h"
#include "ble-scan.h"
#include "solarsense.h"

static int solarsense_xlate_txpower(struct VeItem *root, VeVariant *val,
				    uint64_t rawval)
{
	int txp = rawval ? 6 : 0;

	veVariantUn8(val, txp);

	return 0;
}

static int solarsense_xlate_tss(struct VeItem *root, VeVariant *val,
				uint64_t rawval)
{
	int tss;

	if (rawval <= 29)
		tss = rawval * 2;
	else if (rawval <= 95)
		tss = 60 + 10 * (rawval - 30);
	else if (rawval <= 126)
		tss = 720 + 30 * (rawval - 96);
	else
		tss = rawval;

	veVariantUn16(val, tss);

	return 0;
}

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
		.type	= VE_UN32,
		.offset = 13,
		.scale	= 1,
		.bits	= 20,
		.inval	= 0xfffff,
		.flags	= REG_FLAG_INVALID,
		.name	= "InstallationPower",
		.format = &veUnitWatt,
	},
	{
		.type	= VE_UN32,
		.offset = 15,
		.shift	= 4,
		.scale	= 100,
		.bits	= 20,
		.inval	= 0xfffff,
		.flags	= REG_FLAG_INVALID,
		.name	= "TodaysYield",
		.format = &veUnitKiloWattHour,
	},
	{
		.type	= VE_UN16,
		.offset	= 18,
		.bits	= 14,
		.scale	= 10,
		.inval	= 0x3fff,
		.flags	= REG_FLAG_INVALID,
		.name	= "Irradiance",
		.format = &veUnitIrradiance1Dec,
	},
	{
		.type	= VE_UN16,
		.offset	= 19,
		.shift	= 6,
		.bits	= 11,
		.scale	= 10,
		.bias	= -60,
		.inval	= 0x7ff,
		.flags	= REG_FLAG_INVALID,
		.name	= "CellTemperature",
		.format	= &veUnitCelsius1Dec,
	},
	{
		.type	= VE_UN8,
		.offset	= 20,
		.shift	= 1,
		.bits	= 1,
		.name	= "UnspecifiedRemnant",
		.format	= &veUnitNone,
	},
	{
		.type	= VE_UN16,
		.offset	= 21,
		.shift	= 2,
		.bits	= 8,
		.scale	= 100,
		.bias	= 1.7,
		.inval	= 0xff,
		.flags	= REG_FLAG_INVALID,
		.name	= "BatteryVoltage",
		.format = &veUnitVolt2Dec,
	},
	{
		.type	= VE_UN8,
		.offset	= 22,
		.shift	= 2,
		.bits	= 1,
		.xlate	= solarsense_xlate_txpower,
		.name	= "TxPowerLevel",
		.format	= &veUnitdBm,
	},
	{
		.type	= VE_UN16,
		.offset	= 22,
		.shift	= 3,
		.bits	= 7,
		.inval	= 0x7f,
		.flags	= REG_FLAG_INVALID,
		.xlate	= solarsense_xlate_tss,
		.name	= "TimeSinceLastSun",
		.format	= &veUnitMinutes,
	},
};

static const struct alarm solarsense_alarms[] = {
	{
		.name	= "LowBattery",
		.item	= "BatteryVoltage",
		.level	= 3.2,
		.hyst	= 0.4,
	},
};

static int solarsense_get_seqno(const uint8_t *data, int len, uint16_t *seqno)
{
	/* Nonce is at byte 5 in the manufacturer data (after mfg ID)
	 * Structure: [0]=0x10, [1-2]=ProductID, [3]=Reserved,
	 *            [4]=0xff, [5]=Nonce (8-bit counter)
	 */
	if (len >= 8 && data[0] == 0x10 && data[4] == 0xff) {
		*seqno = data[5];
		return 1;
	}
	return 0;
}

static const struct dev_info solarsense_sensor = {
	.product_id	= VE_PROD_ID_SOLAR_SENSE_750,
	.dev_instance	= 20,
	.dev_prefix	= "solarsense_",
	.role		= "meteo",
	.num_regs	= array_size(solarsense_adv),
	.regs		= solarsense_adv,
	.num_alarms	= array_size(solarsense_alarms),
	.alarms		= solarsense_alarms,
	.get_seqno	= solarsense_get_seqno,
};

int solarsense_handle_mfg(const bdaddr_t *addr, const uint8_t *buf, int len)
{
	struct VeItem *root;
	int source;
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

	source = ble_get_current_source();
	if (ble_dbus_check_dup(root, buf, len, source))
		return 0;

	ble_dbus_set_regs(root, buf, len);
	ble_dbus_update(root, source);

	return 0;
}
