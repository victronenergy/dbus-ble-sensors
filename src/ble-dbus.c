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
	struct dev_info		info;
	struct VeItem		*ctl;
	const void		*data;
	uint32_t		last_tick[DATA_SOURCE_NONE];
	uint32_t		last_seqno;
	enum data_source	active_source;
	int			deferred_created;
	char			pdata[];
};

const VeVariantUnitFmt veUnitHectoPascal = { 0, "hPa" };
const VeVariantUnitFmt veUnitG2Dec = { 2, "g" };
const VeVariantUnitFmt veUnitdBm = { 0, "dBm" };
const VeVariantUnitFmt veUnitcm = { 1, "cm" };
const VeVariantUnitFmt veUnitm3 = { 3, "m3" };
const VeVariantUnitFmt veUnitPPM = { 0, "ppm" };
const VeVariantUnitFmt veUnitUgM3 = { 1, "ug/m3" };
const VeVariantUnitFmt veUnitLux = { 2, "lux" };
const VeVariantUnitFmt veUnitIndex = { 0, "" };

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

static struct VeSettingProperties dedup_window_props = {
	.type		= VE_SN32,
	.def.value.SN32 = 2000,
	.min.value.SN32 = 0,
	.max.value.SN32 = 10000,
};

static int dedup_window_ticks = 2 * TICKS_PER_SEC;

static const char *data_source_str[] = { "Bluetooth LE", "BLE Gateway", "None" };

static veBool readOnlySetValue(struct VeItem *item, void *ctx, VeVariant *variant)
{
	VE_UNUSED(item);
	VE_UNUSED(ctx);
	VE_UNUSED(variant);

	return veFalse;
}

struct VeItem *ble_dbus_create_item(struct VeItem *root, const char *path, VeVariant *val,
				    const void *format)
{
	struct VeItem *item = veItemGetOrCreateUid(root, path);
	if (!item) {
		fprintf(stderr, "failed to create item %s : %p\n", path, root);
		pltExit(-1);
	}
	veItemSetSetter(item, readOnlySetValue, NULL);
	veItemSetFmt(item, veVariantFmt, format);
	veItemOwnerSet(item, val);
	return item;
}

struct VeItem *ble_dbus_create_str(struct VeItem *root, const char *path, const char *str)
{
	VeVariant val;
	return ble_dbus_create_item(root, path, veVariantHeapStr(&val, str), &veUnitNone);
}

struct VeItem *ble_dbus_create_int(struct VeItem *root, const char *path, int num)
{
	VeVariant val;
	return ble_dbus_create_item(root, path, veVariantSn32(&val, num), &veUnitNone);
}

int ble_dbus_set_item(struct VeItem *root, const char *path, VeVariant *val)
{
	struct VeItem *item = veItemByUid(root, path);

	if (!item) {
		char buf[256];
		veItemUid(root, buf, sizeof(buf));
		fprintf(stderr, "set: item is not yet created %s/%s\n", buf, path);
		return -1;
	}
	return veItemOwnerSet(item, val) ? 0 : -2;
}

int ble_dbus_set_str(struct VeItem *root, const char *path, const char *str)
{
	VeVariant val;
	return ble_dbus_set_item(root, path, veVariantHeapStr(&val, str));
}

int ble_dbus_set_int(struct VeItem *root, const char *path, int num)
{
	VeVariant val;
	return ble_dbus_set_item(root, path, veVariantSn32(&val, num));
}

int ble_dbus_set_float(struct VeItem *root, const char *path, float num)
{
	VeVariant val;
	return ble_dbus_set_item(root, path, veVariantFloat(&val, num));
}

int ble_dbus_set_invalid(struct VeItem *root, const char *path)
{
	struct VeItem *item = veItemByUid(root, path);
	if (!item) {
		char buf[256];
		veItemUid(root, buf, sizeof(buf));
		fprintf(stderr, "set_invalid: item is not yet created %s/%s\n", buf, path);
		return -1;
	}
	veItemInvalidate(item);
	return 0;
}

int ble_dbus_set_remote_item(struct VeItem *root, const char *path, VeVariant *val)
{
	struct VeItem *item = veItemByUid(root, path);

	if (!item) {
		char buf[256];
		veItemUid(root, buf, sizeof(buf));
		fprintf(stderr, "set: item is not yet created %s/%s\n", buf, path);
		return -1;
	}
	return veItemSet(item, val) ? 0 : -2;
}

int ble_dbus_set_remote_str(struct VeItem *root, const char *path, const char *str)
{
	VeVariant val;
	return ble_dbus_set_remote_item(root, path, veVariantHeapStr(&val, str));
}

int ble_dbus_set_remote_int(struct VeItem *root, const char *path, int num)
{
	VeVariant val;
	return ble_dbus_set_remote_item(root, path, veVariantSn32(&val, num));
}

int ble_dbus_set_remote_float(struct VeItem *root, const char *path, float num)
{
	VeVariant val;
	return ble_dbus_set_remote_item(root, path, veVariantFloat(&val, num));
}

int ble_dbus_set_remote_invalid(struct VeItem *root, const char *path)
{
	struct VeItem *item = veItemByUid(root, path);
	VeVariant val;
	if (!item) {
		char buf[256];
		veItemUid(root, buf, sizeof(buf));
		fprintf(stderr, "set_invalid: item is not yet created %s/%s\n", buf, path);
		return -1;
	}
	veItemLocalValue(item, &val);
	veVariantInvalidate(&val);
	return veItemSet(item, &val) ? 0 : -2;
}

struct VeItem *ble_dbus_get_item(struct VeItem *root, const char *path)
{
	return veItemByUid(root, path);
}

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
	return &d->info;
}

static inline const void *get_dev_data(struct VeItem *root)
{
	struct device *d = veItemCtx(root)->ptr;
	return d->data;
}

static inline struct VeItem *get_dev_control(struct VeItem *root)
{
	struct device *d = veItemCtx(root)->ptr;
	return d->ctl;
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

static int set_reg(struct VeItem *root, const struct reg_info *reg,
		    const uint8_t *buf, int len)
{
	VeVariant val;
	int err;

	err = load_reg(reg, &val, buf, len, root);
	if (err)
		veVariantInvalidType(&val, reg->type);

	return ble_dbus_set_item(root, reg->name, &val);
}

static void create_regs(struct VeItem *root)
{
	const struct dev_info *info = get_dev_info(root);
	VeVariant val;
	int i;

	for (i = 0; i < info->num_regs; i++) {
		const struct reg_info *reg = &info->regs[i];
		ble_dbus_create_item(root, reg->name, veVariantInvalidType(&val, reg->type), reg->format);
	}
}

static struct VeItem *devices;
static uint32_t tick;

static void on_dedup_window_changed(struct VeItem *item)
{
	VeVariant val;

	veItemLocalValue(item, &val);
	if (veVariantIsValid(&val)) {
		dedup_window_ticks = (val.value.SN32 * TICKS_PER_SEC) / 1000;
	}
}

int ble_dbus_init(void)
{
	struct VeItem *settings = get_settings();
	struct VeItem *ctl = get_control();
	struct VeItem *dedup;

	devices = veItemAlloc(NULL, "");
	if (!devices)
		return -1;

	dedup = veItemCreateSettingsProxy(settings, "Settings/BleSensors", ctl, "DeduplicationWindow",
					  veVariantFmt, &veUnitNone, &dedup_window_props);
	veItemSetChanged(dedup, on_dedup_window_changed);

	return 0;
}

int ble_dbus_add_interface(const char *name, const char *addr)
{
	struct VeItem *ctl = get_control();
	char buf[256];

	snprintf(buf, sizeof(buf), "Interfaces/%s/Address", name);
	ble_dbus_create_str(ctl, buf, addr);

	return 0;
}

int ble_dbus_invalidate_interface(const char *name)
{
	struct VeItem *ctl = get_control();
	char buf[256];

	snprintf(buf, sizeof(buf), "Interfaces/%s/Address", name);
	return ble_dbus_set_invalid(ctl, buf);
}

struct VeItem *ble_dbus_get_dev(const char *dev)
{
	return veItemByUid(devices, dev);
}

int ble_dbus_is_enabled(struct VeItem *droot)
{
	struct VeItem *ctl = get_dev_control(droot);
	return veItemValueInt(ctl, "Enabled") == 1;
}

void *ble_dbus_get_pdata(struct VeItem *root)
{
	struct device *d = veItemCtx(root)->ptr;
	return d->pdata;
}

void *ble_dbus_get_cdata(struct VeItem *root)
{
	struct device *d = veItemCtx(root)->ptr;
	return d->pdata + alloc_size(d->info.pdata_size);
}

struct setting_data {
	struct VeItem			*root;
	setting_changed_fn		onchange;
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
	const void *data = get_dev_data(d->root);

	if (!veItemIsValid(item))
		return;

	d->onchange(d->root, item, data);
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

		item = veItemCreateSettingsProxySync(settings, path, droot,
			ds->name, veVariantFmt, &veUnitNone, ds->props);

		if (ds->onchange) {
			d = alloc_item_data(item, sizeof(*d));
			d->root = droot;
			d->onchange = ds->onchange;
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

static struct device *init_dev(struct VeItem *root, const struct dev_info *info,
			       const void *data, struct VeItem *ctl)
{
	const struct dev_class *dclass = get_dev_class(info);
	int pdata_size = alloc_size(info->pdata_size) + dclass->pdata_size;
	struct device *d;

	d = alloc_item_data(root, sizeof(*d) + pdata_size);
	d->info = *info;
	d->data = data;
	d->ctl = ctl;
	d->active_source = DATA_SOURCE_NONE;

	return d;
}

static int deferred_create(struct VeItem *droot)
{
	const struct dev_info *info = get_dev_info(droot);
	const struct dev_class *dclass = get_dev_class(info);
	struct device *d = veItemCtx(droot)->ptr;

	if (d->deferred_created)
		return 0;

	/* Add settings and alarms */
	ble_dbus_add_settings(droot, dclass->settings, dclass->num_settings);
	ble_dbus_add_alarms(droot, dclass->alarms, dclass->num_alarms);

	if (dclass->init)
		dclass->init(droot, get_dev_data(droot));

	ble_dbus_add_settings(droot, info->settings, info->num_settings);
	ble_dbus_add_alarms(droot, info->alarms, info->num_alarms);

	if (info->init)
		info->init(droot, get_dev_data(droot));

	d->deferred_created = 1;

	return 0;
}

struct VeItem *ble_dbus_create(const char *dev, const struct dev_info *info,
			       const void *data)
{
	struct VeItem *droot;
	struct VeItem *settings = get_settings();
	struct VeItem *ctl = get_control();
	struct VeItem *dev_ctl;
	struct device *d;
	struct VeItem *ena;
	VeVariant val;
	char path[64];
	char name[64];

	droot = ble_dbus_get_dev(dev);
	if (droot)
		goto out;

	snprintf(name, sizeof(name), "Devices/%s%s", info->dev_prefix, dev);
	dev_ctl = veItemGetOrCreateUid(ctl, name);
	droot = veItemGetOrCreateUid(devices, dev);
	d = init_dev(droot, info, data, dev_ctl);

	snprintf(path, sizeof(path), "Settings/Devices/%s", veItemId(dev_ctl));
	ena = veItemCreateSettingsProxySync(settings, path, dev_ctl, "Enabled", veVariantFmt,
 					    &veUnitNone, &bool_val);
	veItemCtx(ena)->ptr = droot;
	veItemSetChanged(ena, on_enabled_changed);
	ble_dbus_create_item(dev_ctl, "Age", veVariantSn32(&val, 0), &veUnitIndex);
	ble_dbus_create_item(dev_ctl, "Name", veVariantInvalidType(&val, VE_HEAP_STR), &veUnitIndex);

	veItemCreateSettingsProxy(settings, path, droot, "CustomName",
				  veVariantFmt, &veUnitNone, &empty_string);

	ble_dbus_create_item(droot, "DeviceName", veVariantInvalidType(&val, VE_HEAP_STR),
			     &veUnitIndex);

	ble_dbus_create_str(droot, "Mgmt/ProcessName", pltProgramName());
	ble_dbus_create_str(droot, "Mgmt/ProcessVersion", VERSION);
	ble_dbus_create_str(droot, "Mgmt/Connection", data_source_str[d->active_source]);
	ble_dbus_create_int(droot, "Connected", 1);
	ble_dbus_create_int(droot, "Devices/0/ProductId", info->product_id);
	ble_dbus_create_int(droot, "Devices/0/DeviceInstance", 0);
	ble_dbus_create_int(droot, "DeviceInstance", 0);
	ble_dbus_create_str(droot, "ProductName",
			    veProductGetName(info->product_id)
			    ?: info->unknown_name
			    ?: "Unknown product");
	ble_dbus_create_int(droot, "Status", 0);
	veItemCreateProductId(droot, info->product_id);

	create_regs(droot);

	veItemSendPendingChanges(ctl);

out:
	if (ble_dbus_is_enabled(droot))
		deferred_create(droot);

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

	ble_dbus_set_int(droot, "Devices/0/DeviceInstance", dev_instance);
	ble_dbus_set_int(droot, "DeviceInstance", dev_instance);

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

	for (i = 0; i < info->num_regs; i++) {
		const struct reg_info *reg = &info->regs[i];

		if ((reg->flags & REG_FLAG_KEY) && reg->key != info->reg_key)
			continue;

		set_reg(droot, reg, data, len);
	}

	return 0;
}

int ble_dbus_set_name(struct VeItem *droot, const char *name)
{
	const char *dname = name;
	struct VeItem *cname;
	VeVariant v;

	cname = veItemByUid(droot, "CustomName");

	if (veItemIsValid(cname)) {
		veItemLocalValue(cname, &v);
		dname = v.value.Ptr;
		if (!dname[0])
			dname = name;
	}

	ble_dbus_set_str(droot, "DeviceName", name);
	ble_dbus_set_str(get_dev_control(droot), "Name", dname);

	return 0;
}

static int alarm_name(const struct alarm *alarm, char *buf, size_t size)
{
	if (alarm->flags & ALARM_FLAG_CONFIG)
		return snprintf(buf, size, "Alarms/%s/State", alarm->name);

	return snprintf(buf, size, "Alarms/%s", alarm->name);
}

static void add_alarm_config(struct VeItem *droot, const struct alarm *alarm)
{
	struct VeItem *settings = get_settings();
	char path[64];
	char buf[64];

	settings_path(droot, path, sizeof(path));

	snprintf(buf, sizeof(buf), "Alarms/%s/Enable", alarm->name);
	veItemCreateSettingsProxy(settings, path, droot, buf, veVariantFmt,
				  &veUnitNone, &bool_val);

	snprintf(buf, sizeof(buf), "Alarms/%s/Active", alarm->name);
	veItemCreateSettingsProxy(settings, path, droot, buf, veVariantFmt,
				  &veUnitNone, alarm->active);

	snprintf(buf, sizeof(buf), "Alarms/%s/Restore", alarm->name);
	veItemCreateSettingsProxy(settings, path, droot, buf, veVariantFmt,
				  &veUnitNone, alarm->restore);
}

int ble_dbus_add_alarms(struct VeItem *droot, const struct alarm *alarms,
			int num_alarms)
{
	VeVariant val;
	char buf[64];
	int i;

	for (i = 0; i < num_alarms; i++) {
		const struct alarm *alarm = &alarms[i];

		alarm_name(alarm, buf, sizeof(buf));
		ble_dbus_create_item(droot, buf, veVariantUn32(&val, 0), &veUnitNone);
		if (alarm->flags & ALARM_FLAG_CONFIG)
			add_alarm_config(droot, alarm);
	}

	return 0;
}

static int alarm_enabled(struct VeItem *droot, const struct alarm *alarm)
{
	char buf[64];

	if (alarm->flags & ALARM_FLAG_CONFIG) {
		snprintf(buf, sizeof(buf), "Alarms/%s/Enable", alarm->name);
		return veItemValueInt(droot, buf);
	}

	return 1;
}

static float alarm_level(struct VeItem *droot, const struct alarm *alarm,
			 int active)
{
	char buf[64];
	float level;

	if (alarm->flags & ALARM_FLAG_CONFIG) {
		snprintf(buf, sizeof(buf), "Alarms/%s/%s", alarm->name,
			 active ? "Restore" : "Active");
		return veItemValueFloat(droot, buf);
	}

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

	if (alarm_enabled(droot, alarm)) {
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
	}

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

static void set_active_source(struct VeItem *root, enum data_source source)
{
	struct device *d = veItemCtx(root)->ptr;
	if (source == d->active_source)
		return;

	d->active_source = source;
	ble_dbus_set_str(root, "Mgmt/Connection", data_source_str[d->active_source]);
}

veBool ble_dbus_check_dup(struct VeItem *root, enum data_source source)
{
	struct device *d = veItemCtx(root)->ptr;
	// When it comes from the same source as the last active one, it is never considered a duplicate
	d->last_tick[source] = tick;
	if (source == d->active_source)
		return veFalse;

	if (source == DATA_SOURCE_BLE) {
		// When it is received via BLE, it is never considered a duplicate
		set_active_source(root, source);
		return veFalse;
	} else if (!d->last_tick[DATA_SOURCE_BLE]
		   || tick - d->last_tick[DATA_SOURCE_BLE] > dedup_window_ticks) {
		// When we haven't received data from BLE for a while, consider the source changed and not a
		// duplicate
		set_active_source(root, source);
		return veFalse;
	}

	return veTrue;
}

veBool ble_dbus_check_dup_seq(struct VeItem *root, enum data_source source, uint32_t seqnr)
{
	struct device *d     = veItemCtx(root)->ptr;
	d->last_tick[source] = tick;

	if (d->active_source != DATA_SOURCE_NONE) {
		if (seqnr == d->last_seqno)
			return veFalse; // Return false, so that the data is processed and it doesn't timeout

		// Check if the distance between seqnr and d->last_seqno is negative and smaller
		// than d->seqnr_window.
		uint32_t mask	= (1u << d->info.seqnr_bits) - 1;
		uint32_t window = d->info.seqnr_window;
		if (((seqnr - d->last_seqno + window) & mask) < window)
			return veTrue;
	}
	d->last_seqno = seqnr;
	set_active_source(root, source);

	return veFalse;
}

void ble_dbus_send_pending_changes(struct VeItem *root)
{
	veItemSendPendingChanges(root);
}

static void ble_dbus_delete(struct VeItem *droot)
{
	struct VeItem *ctl_item = get_dev_control(droot);
	struct VeDbus *dbus;

	veItemDeleteBranch(ctl_item);

	dbus = veItemDbus(droot);
	if (dbus)
		veDbusDisconnect(dbus);

	veItemDeleteBranch(droot);
}

static void ble_dbus_expire(void)
{
	struct VeItem *dev = veItemFirstChild(devices);

	while (dev) {
		struct VeItem *next = veItemNextChild(dev);
		VeVariant val;
		uint32_t dev_ticks = veItemLocalValue(dev, &val)->value.UN32;
		uint32_t age       = tick - dev_ticks;

		ble_dbus_set_int(get_dev_control(dev), "Age", age / TICKS_PER_SEC);

		if (age > 1800 * TICKS_PER_SEC) {
			ble_dbus_delete(dev);
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
