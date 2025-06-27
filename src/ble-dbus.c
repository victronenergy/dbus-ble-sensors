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

#include "ble-dbus.h"
#include "ble-scan.h"
#include "task.h"

struct device {
	const struct dev_info	*info;
	const void		*data;
	char			pdata[];
};

const VeVariantUnitFmt veUnitHectoPascal = { 0, "hPa" };
const VeVariantUnitFmt veUnitG2Dec = { 2, "g" };
const VeVariantUnitFmt veUnitdBm = { 0, "dBm" };
const VeVariantUnitFmt veUnitcm = { 1, "cm" };
const VeVariantUnitFmt veUnitm3 = { 3, "m3" };

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

static void free_item_data(struct VeItem *item)
{
	free(veItemCtx(item)->ptr);
}

static void *alloc_item_data(struct VeItem *item, size_t size)
{
	void *p = calloc(1, size);

	veItemCtx(item)->ptr = p;
	veItemSetAboutToRemoved(item, free_item_data);

	return p;
}

static inline const struct dev_info *get_dev_info(struct VeItem *root)
{
	struct device *d = veItemCtx(root)->ptr;
	return d->info;
}

static inline const void *get_dev_data(struct VeItem *root)
{
	struct device *d = veItemCtx(root)->ptr;
	return d->data;
}

static const struct dev_class null_class;

static inline const struct dev_class *get_dev_class(const struct dev_info *info)
{
	return info->dev_class ?: &null_class;
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

static uint64_t zext(uint64_t v, int b)
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
	int bits = reg->bits;
	uint64_t v;
	int size;
	float f;
	int i;

	if (!bits)
		bits = 8 * type_size(type);

	size = (bits + reg->shift + 7) >> 3;

	if (len < size)
		return -1;

	if (reg->flags & REG_FLAG_BIG_ENDIAN) {
		for (v = 0, i = 0; i < size; i++)
			v = v << 8 | *buf++;
	} else {
		for (v = 0, i = 0; i < size; i++)
			v |= *buf++ << (8 * i);
	}

	v = zext(v >> reg->shift, bits);

	if ((reg->flags & REG_FLAG_INVALID) && v == reg->inval)
		return -1;

	if (reg->xlate) {
		return reg->xlate(root, val, v);
	} else if (scale) {
		if (type_issigned(type))
			f = sext(v, bits);
		else
			f = v;
		veVariantFloat(val, f / scale + bias);
	} else if (type_issigned(type)) {
		veVariantSn32(val, sext(v, bits));
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
		fprintf(stderr, "failed to create item %s\n", path);
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
	const struct dev_info *info = get_dev_info(droot);
	const char *dev = veItemId(droot);
	struct VeItem *ctl = get_control();
	char name[64];

	snprintf(name, sizeof(name), "Devices/%s%s/Enabled",
		 info->dev_prefix, dev);

	return veItemValueInt(ctl, name) == 1;
}

void *ble_dbus_get_pdata(struct VeItem *root)
{
	struct device *d = veItemCtx(root)->ptr;
	return d->pdata;
}

void *ble_dbus_get_cdata(struct VeItem *root)
{
	struct device *d = veItemCtx(root)->ptr;
	return d->pdata + alloc_size(d->info->pdata_size);
}

struct setting_data {
	struct VeItem			*root;
	const struct dev_setting	*setting;
};

static int settings_path(struct VeItem *droot, char *buf, size_t size)
{
	const char *dev = veItemId(droot);
	const struct dev_info *info = get_dev_info(droot);

	return snprintf(buf, size, "Settings/Devices/%s%s",
			info->dev_prefix, dev);
}

static void on_setting_changed(struct VeItem *item)
{
	struct setting_data *d = veItemCtx(item)->ptr;
	const struct dev_setting *ds = d->setting;
	const void *data = get_dev_data(d->root);

	ds->onchange(d->root, item, data);
}

int ble_dbus_add_settings(struct VeItem *droot,
			  const struct dev_setting *dev_settings,
			  int num_settings)
{
	struct VeItem *settings = get_settings();
	struct VeItem *item;
	char path[64];
	int i;

	settings_path(droot, path, sizeof(path));

	for (i = 0; i < num_settings; i++) {
		const struct dev_setting *ds = &dev_settings[i];
		struct setting_data *d;

		item = veItemCreateSettingsProxy(settings, path, droot,
			ds->name, veVariantFmt, &veUnitNone, ds->props);

		if (ds->onchange) {
			d = alloc_item_data(item, sizeof(*d));
			d->root = droot;
			d->setting = ds;
			veItemSetChanged(item, on_setting_changed);
		}
	}

	return 0;
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

static void init_dev(struct VeItem *root, const struct dev_info *info,
		     const void *data)
{
	const struct dev_class *dclass = get_dev_class(info);
	int pdata_size = alloc_size(info->pdata_size) + dclass->pdata_size;
	struct device *d;

	d = alloc_item_data(root, sizeof(*d) + pdata_size);
	d->info = info;
	d->data = data;
}

struct VeItem *ble_dbus_create(const char *dev, const struct dev_info *info,
			       const void *data)
{
	const struct dev_class *dclass = get_dev_class(info);
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
	init_dev(droot, info, data);

	snprintf(dev_id, sizeof(dev_id), "%s%s", info->dev_prefix, dev);
	snprintf(path, sizeof(path), "Settings/Devices/%s", dev_id);

	snprintf(name, sizeof(name), "Devices/%s/Enabled", dev_id);
	ena = veItemCreateSettingsProxyId(settings, path, ctl,
		"Enabled", veVariantFmt, &veUnitNone, &bool_val, name);
	veItemCtx(ena)->ptr = droot;
	veItemSetChanged(ena, on_enabled_changed);

	veItemCreateSettingsProxy(settings, path, droot, "CustomName",
				  veVariantFmt, &veUnitNone, &empty_string);

	ble_dbus_add_settings(droot, dclass->settings, dclass->num_settings);

	if (dclass->init)
		dclass->init(droot, data);

	ble_dbus_add_settings(droot, info->settings, info->num_settings);

	if (info->init)
		info->init(droot, data);

	veItemSendPendingChanges(ctl);

out:
	veItemLocalSet(droot, veVariantUn32(&val, tick));

	return droot;
}

static int ble_dbus_connect(struct VeItem *droot)
{
	const char *dev = veItemId(droot);
	const struct dev_class *dclass;
	const struct dev_info *info;
	struct VeDbus *dbus;
	const char *role;
	int dev_instance;
	char dev_id[32];
	char name[64];

	dbus = veItemDbus(droot);
	if (dbus)
		return 0;

	info = get_dev_info(droot);
	if (!info)
		return -1;

	dclass = get_dev_class(info);
	role = info->role ?: dclass->role;

	snprintf(dev_id, sizeof(dev_id), "%s%s", info->dev_prefix, dev);

	dev_instance = veDbusGetVrmDeviceInstance(dev_id, role,
						  info->dev_instance);
	if (dev_instance < 0)
		return -1;

	ble_dbus_set_str(droot, "Mgmt/ProcessName", pltProgramName());
	ble_dbus_set_str(droot, "Mgmt/ProcessVersion", VERSION);
	ble_dbus_set_str(droot, "Mgmt/Connection", "Bluetooth LE");
	ble_dbus_set_int(droot, "Connected", 1);
	ble_dbus_set_int(droot, "Devices/0/ProductId", info->product_id);
	ble_dbus_set_int(droot, "Devices/0/DeviceInstance", dev_instance);
	ble_dbus_set_int(droot, "DeviceInstance", dev_instance);
	ble_dbus_set_str(droot, "ProductName",
			 veProductGetName(info->product_id));
	ble_dbus_set_int(droot, "Status", 0);
	veItemCreateProductId(droot, info->product_id);

	snprintf(name, sizeof(name), "com.victronenergy.%s.%s", role, dev_id);

	dbus = veDbusConnectString(veDbusGetDefaultConnectString());
	if (!dbus) {
		fprintf(stderr, "%s: dbus connection failed\n", dev);
		return -1;
	}

	veDbusItemInit(dbus, droot);
	veDbusChangeName(dbus, name);

	return 0;
}

int ble_dbus_set_regs(struct VeItem *droot, const uint8_t *data, int len)
{
	const struct dev_info *info = get_dev_info(droot);
	int i;

	for (i = 0; i < info->num_regs; i++)
		set_reg(droot, &info->regs[i], data, len);

	return 0;
}

int ble_dbus_set_name(struct VeItem *droot, const char *name)
{
	const char *dev = veItemId(droot);
	const struct dev_info *info = get_dev_info(droot);
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

static int alarm_name(const struct alarm *alarm, char *buf, size_t size)
{
	if (alarm->flags & ALARM_FLAG_CONFIG)
		return snprintf(buf, size, "Alarms/%s/State", alarm->name);

	return snprintf(buf, size, "Alarms/%s", alarm->name);
}

static float alarm_level(struct VeItem *droot, const struct alarm *alarm,
			 int active)
{
	float level;

	if (alarm->get_level)
		level = alarm->get_level(droot, alarm);
	else
		level = alarm->level;

	if (active)
		level += alarm->hyst;

	return level;
}

static void update_alarm(struct VeItem *droot, const struct alarm *alarm)
{
	struct VeItem *alarm_item;
	struct VeItem *item;
	VeVariant val;
	float level;
	int active = 0;
	char buf[64];

	item = veItemByUid(droot, alarm->item);
	if (!item || !veItemIsValid(item))
		return;

	alarm_name(alarm, buf, sizeof(buf));
	alarm_item = veItemGetOrCreateUid(droot, buf);
	if (!alarm_item)
		return;

	if (veItemIsValid(alarm_item)) {
		veItemLocalValue(alarm_item, &val);
		veVariantToN32(&val);
		active = val.value.UN32;
	}

	level = alarm_level(droot, alarm, active);

	veItemLocalValue(item, &val);
	veVariantToFloat(&val);

	if (alarm->flags & ALARM_FLAG_HIGH)
		active = val.value.Float > level;
	else
		active = val.value.Float < level;

	veVariantUn32(&val, active);
	veItemOwnerSet(alarm_item, &val);
}

void ble_dbus_update_alarms(struct VeItem *droot)
{
	const struct dev_info *info = get_dev_info(droot);
	const struct dev_class *dclass = get_dev_class(info);
	int i;

	for (i = 0; i < dclass->num_alarms; i++)
		update_alarm(droot, &dclass->alarms[i]);

	for (i = 0; i < info->num_alarms; i++)
		update_alarm(droot, &info->alarms[i]);
}

int ble_dbus_update(struct VeItem *droot)
{
	const struct dev_info *info = get_dev_info(droot);
	const struct dev_class *dclass = get_dev_class(info);
	const void *data = get_dev_data(droot);

	if (dclass->update)
		dclass->update(droot, data);

	ble_dbus_update_alarms(droot);
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
