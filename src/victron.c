#include "victron.h"

#include "ble-dbus.h"
#include "victron-lsbms.h"
#include "victron-solarsense.h"

#include <openssl/aes.h>
#include <openssl/evp.h>

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

struct victron_device_data {
	veBool key_set;
	uint8_t key[16];
};

// Returns 0 on success, < 0 on failure
static int victron_decode(const uint8_t *buf, const uint8_t *key, const uint8_t *nonce,
			  uint8_t *out, int len)
{
	// Decode data using AES-CTR-128
	// Construct the 16-byte counter: 2-byte nonce + 14-byte counter
	uint8_t iv[16];
	uint8_t decrypted[16];

	if (len > 16)
		return -1;

	// Copy nonce (2 bytes, little-endian)
	iv[0] = nonce[0];
	iv[1] = nonce[1];

	// Fill the rest with zeros for the 14-byte counter
	memset(&iv[2], 0, 14);

	// Decrypt the 16 bytes at buf[8]
	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	if (!ctx) {
		return -2;
	}

	// Initialize AES-128-CTR decryption
	if (EVP_DecryptInit_ex(ctx, EVP_aes_128_ctr(), NULL, key, iv) != 1) {
		EVP_CIPHER_CTX_free(ctx);
		return -3;
	}

	int outlen;
	// Decrypt the 16 bytes
	if (EVP_DecryptUpdate(ctx, decrypted, &outlen, buf, len) != 1) {
		EVP_CIPHER_CTX_free(ctx);
		return -4;
	}

	EVP_CIPHER_CTX_free(ctx);
	memcpy(out, decrypted, len);

	return 0;
}

static uint8_t to_nibble(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return 0xFF;
}

static void parse_key_setting(struct victron_device_data *d, struct VeItem *setting)
{
	VeVariant var;
	veItemLocalValue(setting, &var);
	if (var.type.tp != VE_HEAP_STR) {
		d->key_set = veFalse;
		return;
	}
	size_t key_len = strlen((const char *)var.value.CPtr);
	if (key_len != 32) {
		d->key_set = veFalse;
		return;
	}

	const char *key_str = (const char *)var.value.CPtr;
	// Convert from hex string to binary
	for (int i = 0; i < 16; i++) {
		uint8_t hn = to_nibble(key_str[i * 2]);
		uint8_t ln = to_nibble(key_str[i * 2 + 1]);
		if (hn > 0x0F || ln > 0x0F) {
			d->key_set = veFalse;
			return;
		}
		d->key[i] = (hn << 4) | ln;
	}
	d->key_set = veTrue;
}

static void on_key_setting_changed(struct VeItem *droot, struct VeItem *setting, const void *data)
{
	struct victron_device_data *pdata = ble_dbus_get_pdata(droot);
	parse_key_setting(pdata, setting);
}

struct instant_readout_handler {
	uint16_t record_type;
	const struct victron_device *device;
};

static const struct instant_readout_handler instant_readout_handlers[] = {
	{ RECORD_TYPE_LYNX_SMART_BMS, &lsbms_victron_device },
	{ RECORD_TYPE_SOLARSENSE, &solarsense_victron_device },
};

static struct VeSettingProperties key_props = {
	.type	       = VE_STR, /* The setting will contain a VE_HEAP_STR */
	.def.value.Ptr = "",
};

static const struct dev_setting victron_control_settings[] = {
	{
		.name	  = "Key",
		.props	  = &key_props,
		.onchange = on_key_setting_changed,
	},
};

static int victron_device_init(struct VeItem *droot, const void *data)
{
	const struct instant_readout_handler *instant_readout_handler
		= (const struct instant_readout_handler *)data;
	const struct victron_device *victron_device = instant_readout_handler->device;
	struct victron_device_data *pdata	    = ble_dbus_get_pdata(droot);
	if (instant_readout_handler->record_type < 0xFF00) {
		ble_dbus_add_control_settings(droot, victron_control_settings,
					      array_size(victron_control_settings));
		parse_key_setting(pdata, ble_dbus_get_control_item(droot, "Key"));
	}
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
	info.pdata_size	  = sizeof(struct victron_device_data);
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
		// Encrypted record, decode it
		uint8_t decrypted[16];
		struct victron_device_data *pdata = ble_dbus_get_pdata(droot);

		if (!pdata->key_set || (pdata->key[0] != buf[7])) {
			struct VeItem *item;
			VeVariant val;
			fprintf(
				stderr,
				"%s: was enabled, but key not set (%d) or does not match device (%02X != %02X)\n",
				veItemId(droot), pdata->key_set, pdata->key[0], buf[7]);
			item = ble_dbus_get_control_item(droot, "Enabled");
			if (!item || !veItemSet(item, veVariantSn32(&val, 0))) {
				fprintf(stderr, "failed to disable device\n");
			}
			item = ble_dbus_get_control_item(droot, "Key");
			if (!item || !veItemSet(item, veVariantHeapStr(&val, ""))) {
				fprintf(stderr, "failed to clear key setting\n");
				veVariantFree(&val);
			}
			return 0;
		}
		if (victron_decode(buf + 8, pdata->key, buf + 5, decrypted, len - 8) < 0)
			return 0;

		ble_dbus_set_regs(droot, decrypted, len - 8);
	} else {
		ble_dbus_set_regs(droot, buf + 8, len - 8);
	}
	ble_dbus_update(droot);

	return 0;
}
