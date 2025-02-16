#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <velib/platform/plt.h>
#include <velib/types/types.h>
#include <velib/types/variant.h>
#include <velib/types/ve_item.h>
#include <velib/utils/ve_item_utils.h>
#include <velib/types/ve_dbus_item.h>
#include <velib/vecan/products.h>
#include <velib/ve_regs.h>

#include "ble-dbus.h"
#include "ble-scan.h"
#include "task.h"

const VeVariantUnitFmt veUnitHectoPascal = { 0, "hPa" };
const VeVariantUnitFmt veUnitG2Dec = { 2, "g" };
const VeVariantUnitFmt veUnitdBm = { 0, "dBm" };
const VeVariantUnitFmt veUnitcm = { 1, "cm" };
const VeVariantUnitFmt veUnitm3 = { 3, "m3" };
const VeVariantUnitFmt veUnitDegree = { 1, "°" };

static struct VeSettingProperties empty_string = {
	.type = VE_HEAP_STR,
	.def.value.Ptr = "",
};

static struct VeSettingProperties bool_val = {
	.type = VE_SN32,
	.def.value.SN32 = 0,
	.min.value.SN32 = 0,
	.max.value.SN32 = 1,
};

static inline void *unconst(const void *p)
{
	union { const void *p; void *q; } u = { p };
	return u.q;
}

static int type_size(VeDataBasicType t)
{
	return (t + 1) / 2;
}

static int type_isint(VeDataBasicType t)
{
	if (t < VE_UN8)
		return 0;
	if (t <= VE_SN32)
		return 1;
	return 0;
}

static int type_issigned(VeDataBasicType t)
{
	return !(t & 1);
}

static int64_t sext(int64_t v, int b)
{
	b = 64 - b;
	return v << b >> b;
}

static int load_int(VeVariant *val, const struct reg_info *reg,
		    const uint8_t *buf, int len, struct VeItem *root)
{
	VeDataBasicType type = reg->type;
	float scale = reg->scale;
	float bias = reg->bias;
	uint64_t v;
	int size;
	float f;
	int i;

	size = type_size(type);
	if (!size)
		return -1;

	if (len < size)
		return -1;

	if (reg->flags & REG_FLAG_BIG_ENDIAN) {
		for (v = 0, i = 0; i < size; i++)
			v = v << 8 | *buf++;
	} else {
		for (v = 0, i = 0; i < size; i++)
			v |= *buf++ << (8 * i);
	}

	if (reg->mask)
		v = (v >> reg->shift) & reg->mask;

	if ((reg->flags & REG_FLAG_INVALID) && v == reg->inval)
		return -1;

	if (reg->xlate) {
		return reg->xlate(root, val, v);
	} else if (scale) {
		if (type_issigned(type))
			f = sext(v, 8 * size);
		else
			f = v;
		veVariantFloat(val, f / scale + bias);
	} else if (type_issigned(type)) {
		veVariantSn32(val, sext(v, 8 * size));
	} else {
		veVariantUn32(val, v);
	}

	return 0;
}

static int load_reg(const struct reg_info *reg, VeVariant *val,
		    const uint8_t *buf, int len, struct VeItem *root)
{
	buf += reg->offset;
	len -= reg->offset;

	if (!type_isint(reg->type))
		return -1;

	return load_int(val, reg, buf, len, root);
}

int ble_dbus_set_item(struct VeItem *root, const char *path, VeVariant *val,
		      const void *format)
{
	struct VeItem *item = veItemGetOrCreateUid(root, path);

	if (!item) {
		printf("failed to create item %s\n", path);
		return -1;
	}

	veItemSetFmt(item, veVariantFmt, format);
	veItemOwnerSet(item, val);

	return 0;
}

int ble_dbus_set_str(struct VeItem *root, const char *path, const char *str)
{
	VeVariant val;
	return ble_dbus_set_item(root, path, veVariantHeapStr(&val, str),
				 &veUnitNone);
}

int ble_dbus_set_int(struct VeItem *root, const char *path, int num)
{
	VeVariant val;
	return ble_dbus_set_item(root, path, veVariantUn32(&val, num),
				 &veUnitNone);
}

static int set_reg(struct VeItem *root, const struct reg_info *reg,
		    const uint8_t *buf, int len)
{
	VeVariant val;
	int err;

	err = load_reg(reg, &val, buf, len, root);
	if (err)
		return err;

	return ble_dbus_set_item(root, reg->name, &val, reg->format);
}

static struct VeItem *devices;
static uint32_t tick;

static void on_contscan_changed(struct VeItem *cont)
{
	VeVariant val;

	veItemLocalValue(cont, &val);
	if (veVariantIsValid(&val))
		ble_scan_continuous(val.value.SN32);
}

int ble_dbus_init(void)
{
	struct VeItem *settings = get_settings();
	struct VeItem *ctl = get_control();
	struct VeItem *cont;

	devices = veItemAlloc(NULL, "");
	if (!devices)
		return -1;

	cont = veItemCreateSettingsProxy(settings, "Settings/BleSensors",
		ctl, "ContinuousScan", veVariantFmt, &veUnitNone, &bool_val);
	veItemSetChanged(cont, on_contscan_changed);

	return 0;
}

int ble_dbus_add_interface(const char *name, const char *addr)
{
	struct VeItem *ctl = get_control();
	char buf[256];

	snprintf(buf, sizeof(buf), "Interfaces/%s/Address", name);
	ble_dbus_set_str(ctl, buf, addr);

	return 0;
}

struct VeItem *ble_dbus_get_dev(const char *dev)
{
	return veItemByUid(devices, dev);
}

int ble_dbus_is_enabled(struct VeItem *droot)
{
	const struct dev_info *info = veItemCtx(droot)->ptr;
	const char *dev = veItemId(droot);
	struct VeItem *ctl = get_control();
	char name[64];

	snprintf(name, sizeof(name), "Devices/%s%s/Enabled",
		 info->dev_prefix, dev);

	return veItemValueInt(ctl, name) == 1;
}

static void on_enabled_changed(struct VeItem *ena)
{
	struct VeItem *droot = veItemCtx(ena)->ptr;
	struct VeDbus *dbus;
	VeVariant val;

	veItemLocalValue(ena, &val);
	if (veVariantIsValid(&val) && val.value.SN32)
		return;

	dbus = veItemDbus(droot);
	if (!dbus)
		return;

	veDbusDisconnect(dbus);
}

struct VeItem *ble_dbus_create(const char *dev, const struct dev_info *info,
			       void *data)
{
	struct VeItem *droot;
	struct VeItem *settings = get_settings();
	struct VeItem *ctl = get_control();
	struct VeItem *ena;
	VeVariant val;
	char dev_id[32];
	char path[64];
	char name[64];

	droot = ble_dbus_get_dev(dev);
	if (droot)
		goto out;

	droot = veItemGetOrCreateUid(devices, dev);
	veItemCtx(droot)->ptr = unconst(info);
	veItemLocalSet(droot, veVariantUn32(&val, tick));

	snprintf(dev_id, sizeof(dev_id), "%s%s", info->dev_prefix, dev);
	snprintf(path, sizeof(path), "Settings/Devices/%s", dev_id);

	snprintf(name, sizeof(name), "Devices/%s/Enabled", dev_id);
	ena = veItemCreateSettingsProxyId(settings, path, ctl,
		"Enabled", veVariantFmt, &veUnitNone, &bool_val, name);
	veItemCtx(ena)->ptr = droot;
	veItemSetChanged(ena, on_enabled_changed);

	veItemCreateSettingsProxy(settings, path, droot, "CustomName",
				  veVariantFmt, &veUnitNone, &empty_string);

	ble_dbus_add_settings(droot, info->settings, info->num_settings);

	if (info->init)
		info->init(droot, data);

	veItemSendPendingChanges(ctl);

out:
	veItemLocalSet(droot, veVariantUn32(&val, tick));

	return droot;
}

int ble_dbus_add_settings(struct VeItem *droot,
			  const struct dev_setting *dev_settings,
			  int num_settings)
{
	const char *dev = veItemId(droot);
	const struct dev_info *info = veItemCtx(droot)->ptr;
	struct VeItem *settings = get_settings();
	char path[64];
	int i;

	snprintf(path, sizeof(path), "Settings/Devices/%s%s",
		 info->dev_prefix, dev);

	for (i = 0; i < num_settings; i++)
		veItemCreateSettingsProxy(settings, path, droot,
					  dev_settings[i].name,
					  veVariantFmt, &veUnitNone,
					  dev_settings[i].props);

	return 0;
}

static int ble_dbus_connect(struct VeItem *droot)
{
	const char *dev = veItemId(droot);
	const struct dev_info *info;
	struct VeDbus *dbus;
	int dev_instance;
	char dev_id[32];
	char path[64];
	char name[64];

	dbus = veItemDbus(droot);
	if (dbus)
		return 0;

	info = veItemCtx(droot)->ptr;
	if (!info)
		return -1;

	snprintf(dev_id, sizeof(dev_id), "%s%s", info->dev_prefix, dev);
	snprintf(path, sizeof(path), "Settings/Devices/%s", dev_id);

	dev_instance = veDbusGetVrmDeviceInstance(dev_id, info->role,
						  info->dev_instance);
	if (dev_instance < 0)
		return -1;

	ble_dbus_set_str(droot, "Mgmt/ProcessName", pltProgramName());
	ble_dbus_set_str(droot, "Mgmt/ProcessVersion", VERSION);
	ble_dbus_set_str(droot, "Mgmt/Connection", "Bluetooth LE");
	ble_dbus_set_int(droot, "Connected", 1);
	ble_dbus_set_int(droot, "DeviceInstance", dev_instance);
	ble_dbus_set_str(droot, "ProductName",
			 veProductGetName(info->product_id));
	ble_dbus_set_int(droot, "Status", 0);
	veItemCreateProductId(droot, info->product_id);

	snprintf(name, sizeof(name), "com.victronenergy.%s.%s",
		 info->role, dev_id);

	dbus = veDbusConnectString(veDbusGetDefaultConnectString());
	if (!dbus) {
		printf("%s: dbus connection failed\n", dev);
		return -1;
	}

	veDbusItemInit(dbus, droot);
	veDbusChangeName(dbus, name);

	return 0;
}

int ble_dbus_set_regs(struct VeItem *droot,
		      const struct reg_info *regs, int nregs,
		      const uint8_t *data, int len)
{
	int i;

	for (i = 0; i < nregs; i++)
		set_reg(droot, &regs[i], data, len);

	return 0;
}

int ble_dbus_set_name(struct VeItem *droot, const char *name)
{
	const char *dev = veItemId(droot);
	const struct dev_info *info = veItemCtx(droot)->ptr;
	const char *dname = name;
	struct VeItem *ctl;
	struct VeItem *cname;
	VeVariant v;
	char buf[64];

	cname = veItemByUid(droot, "CustomName");

	if (veItemIsValid(cname)) {
		veItemLocalValue(cname, &v);
		dname = v.value.Ptr;
		if (!dname[0])
			dname = name;
	}

	ctl = get_control();
	snprintf(buf, sizeof(buf), "Devices/%s%s/Name", info->dev_prefix, dev);
	ble_dbus_set_str(droot, "DeviceName", name);
	ble_dbus_set_str(ctl, buf, dname);

	return 0;
}

int ble_dbus_update(struct VeItem *droot)
{
	ble_dbus_connect(droot);
	veItemSendPendingChanges(droot);

	return 0;
}

static void ble_dbus_expire(void)
{
	struct VeItem *dev = veItemFirstChild(devices);

	while (dev) {
		struct VeItem *next = veItemNextChild(dev);
		VeVariant val;

		veItemLocalValue(dev, &val);
		veVariantToN32(&val);

		if (tick - val.value.UN32 > 1800 * TICKS_PER_SEC) {
			struct VeDbus *dbus = veItemDbus(dev);

			if (dbus)
				veDbusDisconnect(dbus);

			veItemDeleteBranch(dev);
		}

		dev = next;
	}
}

void ble_dbus_tick(void)
{
	static uint32_t dev_expire = 10 * TICKS_PER_SEC;

	tick++;

	if (!--dev_expire) {
		dev_expire = 10 * TICKS_PER_SEC;
		ble_dbus_expire();
		veItemSendPendingChanges(get_control());
	}
}
