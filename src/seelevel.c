/*
 * SeeLevel (Garnet 709-BT) BLE Sensor Integration
 *
 * The Garnet 709-BT hardware supports Bluetooth Low Energy (BLE), and is
 * configured as a Broadcaster transmitting advertisement packets. It
 * continuously cycles through its connected sensors sending out sensor data.
 * No BLE connection is required to read the data.
 *
 * BLE Packet Format:
 * ------------------
 * Manufacturer ID: 305 (0x0131) - Cypress Semiconductor
 *
 * Payload (14 bytes):
 *   Bytes 0-2:   Coach ID (24-bit unique hardware ID, little-endian)
 *   Byte 3:      Sensor Number (0-13)
 *   Bytes 4-6:   Sensor Data (3 ASCII characters)
 *   Bytes 7-9:   Sensor Volume (3 ASCII characters, gallons)
 *   Bytes 10-12: Sensor Total (3 ASCII characters, gallons)
 *   Byte 13:     Sensor Alarm (ASCII digit '0'-'9')
 *
 * Sensor Numbers:
 *   0  = Fresh Water
 *   1  = Black Water
 *   2  = Gray Water
 *   3  = LPG
 *   4  = LPG 2
 *   5  = Galley
 *   6  = Galley 2
 *   7  = Temp
 *   8  = Temp 2
 *   9  = Temp 3
 *   10 = Temp 4
 *   11 = Chemical
 *   12 = Chemical 2
 *   13 = Battery (voltage × 10)
 *
 * Status Codes (in Sensor Data field):
 *   "OPN" = Sensor open/disconnected (device not created)
 *   "ERR" = Sensor error (device shown with error status)
 *   Numeric = Actual sensor reading
 *
 * Unit Conversions:
 *   - Tank Volume/Capacity: Gallons × 0.00378541 = m³
 *   - Temperature: (°F - 32) × 5/9 = °C
 *   - Battery Voltage: Value ÷ 10 = Volts
 *   - Tank Level: Direct percentage (0-100)
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#include <velib/utils/ve_item_utils.h>
#include <velib/vecan/products.h>

#include "ble-dbus.h"
#include "seelevel.h"
#include "tank.h"
#include "temperature.h"

/* SeeLevel sensor types from specification */
#define SENSOR_FRESH		0
#define SENSOR_BLACK		1
#define SENSOR_GRAY		2
#define SENSOR_LPG		3
#define SENSOR_LPG_2		4
#define SENSOR_GALLEY		5
#define SENSOR_GALLEY_2		6
#define SENSOR_TEMP		7
#define SENSOR_TEMP_2		8
#define SENSOR_TEMP_3		9
#define SENSOR_TEMP_4		10
#define SENSOR_CHEMICAL		11
#define SENSOR_CHEMICAL_2	12
#define SENSOR_BATTERY		13

struct seelevel_sensor_info {
	uint8_t		sensor_type;
	const char	*name_prefix;
	const struct dev_class *dev_class;
	uint16_t	product_id;
	int		fluid_type;	/* For tank sensors */
};

static const struct seelevel_sensor_info seelevel_sensors[] = {
	{
		.sensor_type	= SENSOR_FRESH,
		.name_prefix	= "Fresh Water",
		.dev_class	= &tank_class,
		.product_id	= VE_PROD_ID_TANK_SENSOR,
		.fluid_type	= FLUID_TYPE_FRESH_WATER,
	},
	{
		.sensor_type	= SENSOR_BLACK,
		.name_prefix	= "Black Water",
		.dev_class	= &tank_class,
		.product_id	= VE_PROD_ID_TANK_SENSOR,
		.fluid_type	= FLUID_TYPE_BLACK_WATER,
	},
	{
		.sensor_type	= SENSOR_GRAY,
		.name_prefix	= "Gray Water",
		.dev_class	= &tank_class,
		.product_id	= VE_PROD_ID_TANK_SENSOR,
		.fluid_type	= FLUID_TYPE_WASTE_WATER,
	},
	{
		.sensor_type	= SENSOR_LPG,
		.name_prefix	= "LPG",
		.dev_class	= &tank_class,
		.product_id	= VE_PROD_ID_TANK_SENSOR,
		.fluid_type	= FLUID_TYPE_LPG,
	},
	{
		.sensor_type	= SENSOR_LPG_2,
		.name_prefix	= "LPG 2",
		.dev_class	= &tank_class,
		.product_id	= VE_PROD_ID_TANK_SENSOR,
		.fluid_type	= FLUID_TYPE_LPG,
	},
	{
		.sensor_type	= SENSOR_GALLEY,
		.name_prefix	= "Galley Water",
		.dev_class	= &tank_class,
		.product_id	= VE_PROD_ID_TANK_SENSOR,
		.fluid_type	= FLUID_TYPE_WASTE_WATER,
	},
	{
		.sensor_type	= SENSOR_GALLEY_2,
		.name_prefix	= "Galley Water 2",
		.dev_class	= &tank_class,
		.product_id	= VE_PROD_ID_TANK_SENSOR,
		.fluid_type	= FLUID_TYPE_WASTE_WATER,
	},
	{
		.sensor_type	= SENSOR_TEMP,
		.name_prefix	= "Temp",
		.dev_class	= &temperature_class,
		.product_id	= VE_PROD_ID_TEMPERATURE_SENSOR,
	},
	{
		.sensor_type	= SENSOR_TEMP_2,
		.name_prefix	= "Temp 2",
		.dev_class	= &temperature_class,
		.product_id	= VE_PROD_ID_TEMPERATURE_SENSOR,
	},
	{
		.sensor_type	= SENSOR_TEMP_3,
		.name_prefix	= "Temp 3",
		.dev_class	= &temperature_class,
		.product_id	= VE_PROD_ID_TEMPERATURE_SENSOR,
	},
	{
		.sensor_type	= SENSOR_TEMP_4,
		.name_prefix	= "Temp 4",
		.dev_class	= &temperature_class,
		.product_id	= VE_PROD_ID_TEMPERATURE_SENSOR,
	},
	{
		.sensor_type	= SENSOR_CHEMICAL,
		.name_prefix	= "Chemical",
		.dev_class	= &tank_class,
		.product_id	= VE_PROD_ID_TANK_SENSOR,
		.fluid_type	= 0,	/* Generic/unspecified */
	},
	{
		.sensor_type	= SENSOR_CHEMICAL_2,
		.name_prefix	= "Chemical 2",
		.dev_class	= &tank_class,
		.product_id	= VE_PROD_ID_TANK_SENSOR,
		.fluid_type	= 0,	/* Generic/unspecified */
	},
	{
		.sensor_type	= SENSOR_BATTERY,
		.name_prefix	= "Battery",
		.dev_class	= NULL,	/* Uses standalone battery role */
		.product_id	= VE_PROD_ID_BATTERY_MONITOR,
	},
};

static const struct seelevel_sensor_info *seelevel_get_sensor_info(uint8_t sensor_type)
{
	int i;

	for (i = 0; i < array_size(seelevel_sensors); i++)
		if (seelevel_sensors[i].sensor_type == sensor_type)
			return &seelevel_sensors[i];

	return NULL;
}

/* Parse 3-byte ASCII value from packet */
static int seelevel_parse_ascii_value(const uint8_t *buf, char *str_out)
{
	char ascii[4];
	int val;
	int i;

	/* Copy 3 ASCII bytes and null-terminate */
	for (i = 0; i < 3; i++)
		ascii[i] = buf[i];
	ascii[3] = 0;

	/* Store string if output buffer provided */
	if (str_out)
		memcpy(str_out, ascii, 4);

	/* Try to parse as integer */
	if (sscanf(ascii, "%d", &val) == 1)
		return val;

	/* Could be status codes like "OPN", "ERR", etc. - return -1 for invalid */
	return -1;
}

/* Parse single ASCII digit */
static int seelevel_parse_ascii_digit(uint8_t byte)
{
	if (byte >= '0' && byte <= '9')
		return byte - '0';
	return -1;
}

static const struct dev_info seelevel_tank_sensor = {
	.dev_class	= &tank_class,
	.product_id	= VE_PROD_ID_TANK_SENSOR,
	.dev_instance	= 20,
	.dev_prefix	= "seelevel_",
};

static const struct dev_info seelevel_temp_sensor = {
	.dev_class	= &temperature_class,
	.product_id	= VE_PROD_ID_TEMPERATURE_SENSOR,
	.dev_instance	= 20,
	.dev_prefix	= "seelevel_",
};

static const struct dev_info seelevel_battery_sensor = {
	.product_id	= VE_PROD_ID_BATTERY_MONITOR,
	.dev_instance	= 20,
	.dev_prefix	= "seelevel_",
	.role		= "battery",
};

int seelevel_handle_mfg(const bdaddr_t *addr, const uint8_t *buf, int len)
{
	struct VeItem *root;
	const struct seelevel_sensor_info *sensor_info;
	const struct dev_info *dev_info;
	uint32_t coach_id;
	uint8_t sensor_num;
	int sensor_data;
	int sensor_volume;
	int sensor_total;
	int sensor_alarm;
	char data_str[4];
	char dev[32];
	char name[64];
	VeVariant val;

	/* Minimum packet size: coach_id(3) + sensor_num(1) + data(3) + volume(3) + total(3) + alarm(1) = 14 */
	if (len < 14)
		return -1;

	/* Extract Coach ID (3 bytes, little-endian) */
	coach_id = buf[0] | (buf[1] << 8) | (buf[2] << 16);

	/* Extract Sensor Number */
	sensor_num = buf[3];

	/* Extract Sensor Data (3 ASCII bytes) */
	sensor_data = seelevel_parse_ascii_value(&buf[4], data_str);

	/* Extract Sensor Volume (3 ASCII bytes) */
	sensor_volume = seelevel_parse_ascii_value(&buf[7], NULL);

	/* Extract Sensor Total (3 ASCII bytes) */
	sensor_total = seelevel_parse_ascii_value(&buf[10], NULL);

	/* Extract Sensor Alarm (ASCII digit) */
	sensor_alarm = seelevel_parse_ascii_digit(buf[13]);

	/* Get sensor type information */
	sensor_info = seelevel_get_sensor_info(sensor_num);
	if (!sensor_info)
		return -1;

	/* Check for error/status codes in data field BEFORE creating device */
	if (sensor_data < 0) {
		/* Data contains status code like "OPN", "ERR", etc. */
		if (strcmp(data_str, "OPN") == 0) {
			/* Sensor open/disconnected - don't create/show this sensor */
			return 0;
		}
		
		/* For other error codes (ERR, etc.), we might want to show them
		 * Fall through to create device and show error status */
	}

	/* Create unique device ID using MAC address + sensor number
	 * This follows the same pattern as Mopeka sensors, using the BLE MAC
	 * as the primary identifier. Since one 709-BT broadcasts multiple sensors,
	 * we append the sensor number to create unique device IDs.
	 */
	snprintf(dev, sizeof(dev), "%02x%02x%02x%02x%02x%02x_%02x",
		 addr->b[5], addr->b[4], addr->b[3],
		 addr->b[2], addr->b[1], addr->b[0], sensor_num);

	/* Select appropriate device info based on sensor type */
	if (sensor_num == SENSOR_BATTERY)
		dev_info = &seelevel_battery_sensor;
	else if (sensor_num >= SENSOR_TEMP && sensor_num <= SENSOR_TEMP_4)
		dev_info = &seelevel_temp_sensor;
	else
		dev_info = &seelevel_tank_sensor;

	/* Create or get existing device */
	root = ble_dbus_create(dev, dev_info, sensor_info);
	if (!root)
		return -1;

	/* Set device name using MAC address last 3 bytes (same as coach_id) */
	snprintf(name, sizeof(name), "SeeLevel %s %02X:%02X:%02X",
		 sensor_info->name_prefix,
		 addr->b[2], addr->b[1], addr->b[0]);
	ble_dbus_set_name(root, name);

	if (!ble_dbus_is_enabled(root))
		return 0;

	/* Handle non-OPN error codes */
	if (sensor_data < 0) {
		/* Set device status based on status code */
		if (strcmp(data_str, "ERR") == 0) {
			/* Sensor error */
			ble_dbus_set_int(root, "Status", 4);  /* Error status */
			ble_dbus_set_str(root, "StatusMessage", "Sensor error");
		} else {
			ble_dbus_set_int(root, "Status", 4);  /* Error status */
			ble_dbus_set_str(root, "StatusMessage", data_str);
		}
		
		ble_dbus_update(root);
		return 0;
	}

	/* Update sensor values based on type */
	if (sensor_num == SENSOR_BATTERY) {
		/* Battery sensor - data is voltage * 10 */
		float voltage = sensor_data / 10.0f;
		ble_dbus_set_item(root, "BatteryVoltage",
				  veVariantFloat(&val, voltage),
				  &veUnitVolt2Dec);
	} else if (sensor_num >= SENSOR_TEMP && sensor_num <= SENSOR_TEMP_4) {
		/* Temperature sensor - assume Fahrenheit, convert to Celsius */
		float temp_f = sensor_data;
		float temp_c = (temp_f - 32.0f) * 5.0f / 9.0f;
		ble_dbus_set_item(root, "Temperature",
				  veVariantFloat(&val, temp_c),
				  &veUnitCelsius1Dec);
	} else {
		/* Tank sensor */
		/* Level as percentage (0-100)
		 * The SeeLevel 709-BT reports percentage directly, unlike Mopeka
		 * which reports raw distance and needs calibration.
		 */
		ble_dbus_set_int(root, "Level", sensor_data);

		/* Set fluid type for initial setup */
		ble_dbus_set_int(root, "FluidType", sensor_info->fluid_type);
		
		/* If the sensor provides volume data (non-zero), use it
		 * Note: Most 709-BT units send "000" for these fields, in which case
		 * the tank_class will calculate remaining as: (level/100) * capacity
		 * where capacity is user-configured in the UI.
		 */
		if (sensor_volume > 0) {
			/* Volume in gallons - convert to m³ (1 gal = 0.00378541 m³) */
			float volume_m3 = sensor_volume * 0.00378541f;
			ble_dbus_set_item(root, "Remaining",
					  veVariantFloat(&val, volume_m3),
					  &veUnitm3);
		}

		if (sensor_total > 0) {
			/* Total capacity in gallons - convert to m³ */
			float capacity_m3 = sensor_total * 0.00378541f;
			ble_dbus_set_item(root, "Capacity",
					  veVariantFloat(&val, capacity_m3),
					  &veUnitm3);
		}
	}

	/* Set alarm state if present */
	if (sensor_alarm >= 0) {
		ble_dbus_set_int(root, "Alarm", sensor_alarm);
	}

	/* Set status to OK */
	ble_dbus_set_int(root, "Status", STATUS_OK);

	ble_dbus_update(root);

	return 0;
}

