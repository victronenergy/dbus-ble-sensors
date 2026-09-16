#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include <dbus/dbus.h>
#include <event2/event.h>

#include <velib/platform/plt.h>
#include <velib/utils/ve_logger.h>
#include <velib/utils/ve_todo.h>

#include "ble-dbus.h"
#include "ble-scan.h"
#include "ble-handler.h"
#include "task.h"

#define BLUEZ_SERVICE		"org.bluez"
#define BLUEZ_ROOT_PATH		"/"
#define BLUEZ_ADAPTER_IFACE	"org.bluez.Adapter1"
#define OBJMGR_IFACE		"org.freedesktop.DBus.ObjectManager"
#define PROPS_IFACE		"org.freedesktop.DBus.Properties"

/*
 * Matches nothing, so bluez does not export a device object for every
 * advertiser it sees.  The advertisements are read from the raw hci socket.
 */
#define SCAN_PATTERN		"FF:FF:FF:FF:FF:FF"

#define DBUS_CALL_TIMEOUT	2000
#define PATH_SIZE		64

#define BLUEZ_ERR_IN_PROGRESS	"org.bluez.Error.InProgress"

#define MODULE			"ble-scan"

#ifndef EVT_LE_EXT_ADVERTISING_REPORT
#define EVT_LE_EXT_ADVERTISING_REPORT	0x0d
#endif

typedef struct {
	uint16_t evt_type;
	uint8_t bdaddr_type;
	bdaddr_t bdaddr;
	uint8_t primary_phy;
	uint8_t secondary_phy;
	uint8_t sid;
	uint8_t tx_power;
	int8_t rssi;
	uint16_t interval;
	uint8_t direct_bdaddr_type;
	bdaddr_t direct_bdaddr;
	uint8_t length;
	uint8_t data[0];
} __attribute__ ((packed)) le_ext_advertising_info;

#define NAME_SIZE sizeof(((struct hci_dev_info *)0)->name)

struct hci_device {
	uint16_t dev_id;
	int sock;
	int discovering;
	char path[PATH_SIZE];
	char name[NAME_SIZE];
	struct event *ev;
};

static struct hci_device devices[HCI_MAX_DEV];
static int cont_scan;
static int ble_scan_enabled = 1;
static DBusConnection *bluez_bus;
static struct event *dispatch_ev;

static struct VeSettingProperties ble_enabled_props = {
	.type		= VE_SN32,
	.def.value.SN32 = 1,
	.min.value.SN32 = 0,
	.max.value.SN32 = 1,
};

static struct VeSettingProperties continuous_scan_props = {
	.type		= VE_SN32,
	.def.value.SN32 = 0,
	.min.value.SN32 = 0,
	.max.value.SN32 = 1,
};

static void ble_scan_parse_adv(const uint8_t *msg, int len, int num)
{
	while (num-- > 0) {
		const le_advertising_info *adv;
		int size;

		if (len < LE_ADVERTISING_INFO_SIZE)
			return;

		adv = (const le_advertising_info *)msg;

		/* A single rssi byte follows the advertising data. */
		size = LE_ADVERTISING_INFO_SIZE + adv->length + 1;
		if (len < size)
			return;

		ble_parse_adv(&adv->bdaddr, adv->data, adv->length,
			      DATA_SOURCE_BLE);

		msg += size;
		len -= size;
	}
}

static void ble_scan_parse_ext_adv(const uint8_t *msg, int len, int num)
{
	while (num-- > 0) {
		const le_ext_advertising_info *adv;
		int size;

		if (len < (int)sizeof(*adv))
			return;

		adv = (const le_ext_advertising_info *)msg;

		size = sizeof(*adv) + adv->length;
		if (len < size)
			return;

		ble_parse_adv(&adv->bdaddr, adv->data, adv->length,
			      DATA_SOURCE_BLE);

		msg += size;
		len -= size;
	}
}

static DBusMessage *ble_scan_call_reply(DBusMessage *msg, int *in_progress)
{
	const char *path, *member;
	DBusMessage *reply;
	DBusError err;

	if (!msg)
		return NULL;

	path   = dbus_message_get_path(msg);
	member = dbus_message_get_member(msg);

	logI(MODULE, "calling %s %s", path, member);

	dbus_error_init(&err);
	reply = dbus_connection_send_with_reply_and_block(bluez_bus, msg,
							 DBUS_CALL_TIMEOUT, &err);

	if (reply) {
		logI(MODULE, "%s %s: ok", path, member);
	} else {
		int busy = dbus_error_has_name(&err, BLUEZ_ERR_IN_PROGRESS);

		if (in_progress)
			*in_progress = busy;

		if (busy)
			logI(MODULE, "%s %s: %s", path, member, err.name);
		else
			fprintf(stderr, "bluez: %s %s: %s: %s\n", path, member,
				err.name, err.message);

		dbus_error_free(&err);
	}

	dbus_message_unref(msg);

	return reply;
}

/* Returns 0 on success, 1 if bluez is already in the requested state, -1 on error. */
static int ble_scan_call(DBusMessage *msg)
{
	int in_progress = 0;
	DBusMessage *reply = ble_scan_call_reply(msg, &in_progress);

	if (!reply)
		return in_progress ? 1 : -1;

	dbus_message_unref(reply);

	return 0;
}

static void ble_scan_append_dict_str(DBusMessageIter *dict, const char *key,
				     const char *val)
{
	DBusMessageIter entry, var;

	dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
	dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT,
					 DBUS_TYPE_STRING_AS_STRING, &var);
	dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &val);
	dbus_message_iter_close_container(&entry, &var);
	dbus_message_iter_close_container(dict, &entry);
}

static void ble_scan_append_dict_bool(DBusMessageIter *dict, const char *key,
				      dbus_bool_t val)
{
	DBusMessageIter entry, var;

	dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
	dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT,
					 DBUS_TYPE_BOOLEAN_AS_STRING, &var);
	dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &val);
	dbus_message_iter_close_container(&entry, &var);
	dbus_message_iter_close_container(dict, &entry);
}

static int ble_scan_get_bool(struct hci_device *dev, const char *prop)
{
	const char *iface = BLUEZ_ADAPTER_IFACE;
	DBusMessageIter iter, var;
	DBusMessage *msg, *reply;
	dbus_bool_t on;

	msg = dbus_message_new_method_call(BLUEZ_SERVICE, dev->path,
					   PROPS_IFACE, "Get");
	if (!msg)
		return -1;

	dbus_message_append_args(msg, DBUS_TYPE_STRING, &iface,
				 DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID);

	reply = ble_scan_call_reply(msg, NULL);
	if (!reply)
		return -1;

	if (!dbus_message_iter_init(reply, &iter) ||
	    dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT) {
		dbus_message_unref(reply);
		return -1;
	}

	dbus_message_iter_recurse(&iter, &var);
	if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_BOOLEAN) {
		dbus_message_unref(reply);
		return -1;
	}

	dbus_message_iter_get_basic(&var, &on);
	dbus_message_unref(reply);

	logI(MODULE, "%s: %s = %d", dev->name, prop, on ? 1 : 0);

	return on ? 1 : 0;
}

static void ble_scan_set_powered(struct hci_device *dev)
{
	const char *iface = BLUEZ_ADAPTER_IFACE;
	const char *prop  = "Powered";
	DBusMessageIter iter, var;
	dbus_bool_t on = TRUE;
	DBusMessage *msg;

	msg = dbus_message_new_method_call(BLUEZ_SERVICE, dev->path,
					   PROPS_IFACE, "Set");
	if (!msg)
		return;

	dbus_message_iter_init_append(msg, &iter);
	dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &iface);
	dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &prop);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT,
					 DBUS_TYPE_BOOLEAN_AS_STRING, &var);
	dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &on);
	dbus_message_iter_close_container(&iter, &var);

	/* bluez reports InProgress while it is still powering the adapter */
	ble_scan_call(msg);
}

static int ble_scan_set_filter(struct hci_device *dev)
{
	DBusMessageIter iter, dict;
	DBusMessage *msg;

	msg = dbus_message_new_method_call(BLUEZ_SERVICE, dev->path,
					   BLUEZ_ADAPTER_IFACE,
					   "SetDiscoveryFilter");
	if (!msg)
		return -1;

	dbus_message_iter_init_append(msg, &iter);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict);
	ble_scan_append_dict_str(&dict, "Transport", "le");
	ble_scan_append_dict_str(&dict, "Pattern", SCAN_PATTERN);
	ble_scan_append_dict_bool(&dict, "DuplicateData", TRUE);
	dbus_message_iter_close_container(&iter, &dict);

	return ble_scan_call(msg);
}

static int ble_scan_discovery(struct hci_device *dev, int start)
{
	DBusMessage *msg;
	int rc;

	msg = dbus_message_new_method_call(BLUEZ_SERVICE, dev->path,
					   BLUEZ_ADAPTER_IFACE,
					   start ? "StartDiscovery" : "StopDiscovery");

	rc = ble_scan_call(msg);
	if (rc < 0 && start)
		return -1;

	/* On InProgress bluez has a pending request, check what it did. */
	if (rc > 0)
		dev->discovering = ble_scan_get_bool(dev, "Discovering") == 1;
	else
		dev->discovering = start;

	logI(MODULE, "%s: discovering = %d", dev->name, dev->discovering);

	return dev->discovering == start ? 0 : -1;
}

static int ble_scan_start(struct hci_device *dev)
{
	if (ble_scan_get_bool(dev, "Powered") <= 0) {
		/* Retry once the adapter is powered up. */
		ble_scan_set_powered(dev);
		return -1;
	}

	if (ble_scan_set_filter(dev) < 0)
		return -1;

	return ble_scan_discovery(dev, 1);
}

static void ble_scan_close_dev(struct hci_device *dev)
{
	int flags;

	if (dev->dev_id == HCI_DEV_NONE)
		return;

	fprintf(stderr, "closing hci%d (%s)\n", dev->dev_id, dev->name);

	if (dev->discovering)
		ble_scan_discovery(dev, 0);

	if (dev->ev != NULL) {
		event_free(dev->ev);
		dev->ev = NULL;
	}

	if (dev->sock >= 0) {

		flags = fcntl(dev->sock, F_GETFL);
		if (flags > 0)
			fcntl(dev->sock, F_SETFL, flags & ~O_NONBLOCK);

		hci_close_dev(dev->sock);
		dev->sock = -1;
	}

	if (dev->name[0]) {
		ble_dbus_invalidate_interface(dev->name);
		veItemSendPendingChanges(get_control());
		dev->name[0] = '\0';
	}

	dev->path[0] = '\0';
	dev->dev_id  = HCI_DEV_NONE;
}

static void on_dev_socket_readable(evutil_socket_t fd, short events, void *ctx)
{
	struct hci_device *dev = ctx;
	uint8_t buf[HCI_MAX_EVENT_SIZE];
	hci_event_hdr *evt;
	evt_le_meta_event *mev;
	int num;
	int len;

	for (;;) {
		uint8_t *msg = buf;

		len = read(dev->sock, buf, sizeof(buf));
		if (len < 0 && errno != EAGAIN) {
			fprintf(stderr, "%s: read: %s\n", dev->name, strerror(errno));
			ble_scan_close_dev(dev);
			return;
		}

		if (len <= 0)
			break;

		if (msg[0] != HCI_EVENT_PKT)
			continue;

		msg++;
		len--;

		if (len < HCI_EVENT_HDR_SIZE)
			continue;

		evt = (hci_event_hdr *)msg;
		msg += HCI_EVENT_HDR_SIZE;
		len -= HCI_EVENT_HDR_SIZE;

		if (evt->evt != EVT_LE_META_EVENT)
			continue;

		if (evt->plen != len)
			continue;

		if (len < EVT_LE_META_EVENT_SIZE + 1)
			continue;

		mev = (evt_le_meta_event *)msg;
		msg += EVT_LE_META_EVENT_SIZE;
		len -= EVT_LE_META_EVENT_SIZE;

		if (len < 1)
			continue;

		num = *msg++;
		len--;

		if (!ble_scan_enabled)
			continue;

		if (mev->subevent == EVT_LE_ADVERTISING_REPORT)
			ble_scan_parse_adv(msg, len, num);
		else if (mev->subevent == EVT_LE_EXT_ADVERTISING_REPORT)
			ble_scan_parse_ext_adv(msg, len, num);
	}
}

static struct hci_device* ble_scan_first_free_device(void)
{
	int i;

	for (i = 0; i < ARRAY_LENGTH(devices); i++) {
		if (devices[i].dev_id == HCI_DEV_NONE)
			return &devices[i];
	}

	return NULL;
}

static struct hci_device *ble_scan_find_dev(const char *path)
{
	int i;

	for (i = 0; i < ARRAY_LENGTH(devices); i++) {
		if (devices[i].dev_id != HCI_DEV_NONE &&
		    !strcmp(devices[i].path, path))
			return &devices[i];
	}

	return NULL;
}

/* The advertisements are read from the raw hci socket, not from bluez. */
static int ble_scan_open_sock(struct hci_device *dev)
{
	struct hci_filter filter;
	socklen_t len;
	int flags;
	int err;

	dev->sock = hci_open_dev(dev->dev_id);
	if (dev->sock < 0) {
		perror("hci_open_dev");
		return -1;
	}

	len = sizeof(filter);
	err = getsockopt(dev->sock, SOL_HCI, HCI_FILTER, &filter, &len);
	if (err < 0) {
		perror("getsockopt");
		return -1;
	}

	hci_filter_set_ptype(HCI_EVENT_PKT, &filter);
	hci_filter_set_event(EVT_LE_META_EVENT, &filter);

	err = setsockopt(dev->sock, SOL_HCI, HCI_FILTER,
			 &filter, sizeof(filter));
	if (err < 0) {
		perror("setsockopt");
		return -1;
	}

	flags = fcntl(dev->sock, F_GETFL);
	if (flags < 0)
		return -1;

	err = fcntl(dev->sock, F_SETFL, flags | O_NONBLOCK);
	if (err < 0)
		return -1;

	dev->ev = event_new(pltGetLibEventBase(), dev->sock,
			    EV_READ | EV_PERSIST, on_dev_socket_readable, dev);
	if (dev->ev == NULL) {
		perror("event_new");
		return -1;
	}

	if (event_add(dev->ev, NULL) < 0) {
		perror("event_add");
		return -1;
	}

	return 0;
}

static const char *ble_scan_get_str_prop(const DBusMessageIter *props,
					 const char *name)
{
	DBusMessageIter iter = *props;

	while (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter entry, var;
		const char *key;
		const char *val;

		dbus_message_iter_recurse(&iter, &entry);
		dbus_message_iter_get_basic(&entry, &key);
		dbus_message_iter_next(&entry);
		dbus_message_iter_recurse(&entry, &var);

		if (!strcmp(key, name) &&
		    dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_STRING) {
			dbus_message_iter_get_basic(&var, &val);
			return val;
		}

		dbus_message_iter_next(&iter);
	}

	return NULL;
}

static int ble_scan_get_bool_prop(const DBusMessageIter *props, const char *name)
{
	DBusMessageIter iter = *props;

	while (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter entry, var;
		const char *key;
		dbus_bool_t val;

		dbus_message_iter_recurse(&iter, &entry);
		dbus_message_iter_get_basic(&entry, &key);
		dbus_message_iter_next(&entry);
		dbus_message_iter_recurse(&entry, &var);

		if (!strcmp(key, name) &&
		    dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_BOOLEAN) {
			dbus_message_iter_get_basic(&var, &val);
			return val ? 1 : 0;
		}

		dbus_message_iter_next(&iter);
	}

	return -1;
}

static void ble_scan_open_dev(const char *path, const DBusMessageIter *props)
{
	struct hci_device *dev;
	const char *addr;
	unsigned int id;

	if (!ble_scan_enabled)
		return;

	if (ble_scan_find_dev(path))
		return;

	if (sscanf(path, "/org/bluez/hci%u", &id) != 1 || id >= HCI_MAX_DEV) {
		fprintf(stderr, "ignoring adapter %s\n", path);
		return;
	}

	addr = ble_scan_get_str_prop(props, "Address");
	if (!addr) {
		fprintf(stderr, "no address for adapter %s\n", path);
		return;
	}

	dev = ble_scan_first_free_device();
	if (!dev) {
		fprintf(stderr, "no free device slot for %s\n", path);
		return;
	}

	fprintf(stderr, "opening hci%u\n", id);

	dev->dev_id = id;
	snprintf(dev->path, sizeof(dev->path), "%s", path);
	snprintf(dev->name, sizeof(dev->name), "hci%u", id);

	if (ble_scan_open_sock(dev) < 0) {
		ble_scan_close_dev(dev);
		return;
	}

	ble_dbus_add_interface(dev->name, addr);
	veItemSendPendingChanges(get_control());

	ble_scan_start(dev);
}

static void ble_scan_remove_dev(const char *path)
{
	struct hci_device *dev = ble_scan_find_dev(path);

	if (!dev)
		return;

	/* The adapter is gone, no point in talking to bluez about it. */
	dev->discovering = 0;
	ble_scan_close_dev(dev);
}

static void ble_scan_parse_interfaces(const char *path, DBusMessageIter *ifaces)
{
	while (dbus_message_iter_get_arg_type(ifaces) == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter entry, props;
		const char *iface;

		dbus_message_iter_recurse(ifaces, &entry);
		dbus_message_iter_get_basic(&entry, &iface);
		dbus_message_iter_next(&entry);

		if (!strcmp(iface, BLUEZ_ADAPTER_IFACE)) {
			dbus_message_iter_recurse(&entry, &props);
			ble_scan_open_dev(path, &props);
		}

		dbus_message_iter_next(ifaces);
	}
}

static void ble_scan_refresh_devices(void)
{
	DBusMessageIter iter, objs;
	DBusMessage *msg, *reply;
	DBusError err;

	msg = dbus_message_new_method_call(BLUEZ_SERVICE, BLUEZ_ROOT_PATH,
					   OBJMGR_IFACE, "GetManagedObjects");
	if (!msg)
		return;

	dbus_error_init(&err);
	reply = dbus_connection_send_with_reply_and_block(bluez_bus, msg,
							 DBUS_CALL_TIMEOUT, &err);
	dbus_message_unref(msg);

	if (!reply) {
		fprintf(stderr, "bluez: %s: %s\n", err.name, err.message);
		dbus_error_free(&err);
		return;
	}

	if (!dbus_message_iter_init(reply, &iter) ||
	    dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
		dbus_message_unref(reply);
		return;
	}

	dbus_message_iter_recurse(&iter, &objs);

	while (dbus_message_iter_get_arg_type(&objs) == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter entry, ifaces;
		const char *path;

		dbus_message_iter_recurse(&objs, &entry);
		dbus_message_iter_get_basic(&entry, &path);
		dbus_message_iter_next(&entry);
		dbus_message_iter_recurse(&entry, &ifaces);

		ble_scan_parse_interfaces(path, &ifaces);

		dbus_message_iter_next(&objs);
	}

	dbus_message_unref(reply);
}

static void ble_scan_close_all(void)
{
	int i;

	for (i = 0; i < ARRAY_LENGTH(devices); i++)
		ble_scan_close_dev(&devices[i]);
}

static void on_interfaces_added(DBusMessage *msg)
{
	DBusMessageIter iter, ifaces;
	const char *path;

	if (!dbus_message_iter_init(msg, &iter))
		return;

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_OBJECT_PATH)
		return;

	dbus_message_iter_get_basic(&iter, &path);
	dbus_message_iter_next(&iter);

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY)
		return;

	dbus_message_iter_recurse(&iter, &ifaces);
	ble_scan_parse_interfaces(path, &ifaces);
}

static void on_interfaces_removed(DBusMessage *msg)
{
	DBusMessageIter iter, ifaces;
	const char *path;

	if (!dbus_message_iter_init(msg, &iter))
		return;

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_OBJECT_PATH)
		return;

	dbus_message_iter_get_basic(&iter, &path);
	dbus_message_iter_next(&iter);

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY)
		return;

	dbus_message_iter_recurse(&iter, &ifaces);

	while (dbus_message_iter_get_arg_type(&ifaces) == DBUS_TYPE_STRING) {
		const char *iface;

		dbus_message_iter_get_basic(&ifaces, &iface);
		if (!strcmp(iface, BLUEZ_ADAPTER_IFACE))
			ble_scan_remove_dev(path);

		dbus_message_iter_next(&ifaces);
	}
}

static void on_name_owner_changed(DBusMessage *msg)
{
	const char *name, *old, *new;
	DBusError err;
	int i;

	dbus_error_init(&err);

	if (!dbus_message_get_args(msg, &err,
				   DBUS_TYPE_STRING, &name,
				   DBUS_TYPE_STRING, &old,
				   DBUS_TYPE_STRING, &new,
				   DBUS_TYPE_INVALID)) {
		dbus_error_free(&err);
		return;
	}

	if (strcmp(name, BLUEZ_SERVICE))
		return;

	if (!*new) {
		/* bluez is gone, its adapters are unusable */
		for (i = 0; i < ARRAY_LENGTH(devices); i++)
			devices[i].discovering = 0;

		ble_scan_close_all();
		return;
	}

	ble_scan_refresh_devices();
}

static void on_properties_changed(DBusMessage *msg)
{
	struct hci_device *dev = ble_scan_find_dev(dbus_message_get_path(msg));
	DBusMessageIter iter, props;

	if (!dev)
		return;

	if (!dbus_message_iter_init(msg, &iter))
		return;

	dbus_message_iter_next(&iter);

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY)
		return;

	dbus_message_iter_recurse(&iter, &props);

	if (ble_scan_get_bool_prop(&props, "Discovering") == 0) {
		logI(MODULE, "%s: bluez stopped discovering", dev->name);
		dev->discovering = 0;
	}

	/* The adapter may just have been powered up or stopped discovering. */
	if (!dev->discovering)
		ble_scan_start(dev);
}

static DBusHandlerResult on_dbus_message(DBusConnection *conn,
					 DBusMessage *msg, void *ctx)
{
	if (dbus_message_is_signal(msg, OBJMGR_IFACE, "InterfacesAdded"))
		on_interfaces_added(msg);
	else if (dbus_message_is_signal(msg, OBJMGR_IFACE, "InterfacesRemoved"))
		on_interfaces_removed(msg);
	else if (dbus_message_is_signal(msg, PROPS_IFACE, "PropertiesChanged"))
		on_properties_changed(msg);
	else if (dbus_message_is_signal(msg, DBUS_INTERFACE_DBUS, "NameOwnerChanged"))
		on_name_owner_changed(msg);

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void on_dbus_dispatch(evutil_socket_t fd, short events, void *ctx)
{
	while (dbus_connection_get_dispatch_status(bluez_bus) ==
	       DBUS_DISPATCH_DATA_REMAINS)
		dbus_connection_dispatch(bluez_bus);
}

static void on_dbus_dispatch_status(DBusConnection *conn,
				    DBusDispatchStatus status, void *ctx)
{
	static const struct timeval tv;

	if (status == DBUS_DISPATCH_DATA_REMAINS)
		event_add(dispatch_ev, &tv);
}

static void on_dbus_watch(evutil_socket_t fd, short events, void *ctx)
{
	DBusWatch *watch = ctx;
	unsigned int flags = 0;

	if (events & EV_READ)
		flags |= DBUS_WATCH_READABLE;
	if (events & EV_WRITE)
		flags |= DBUS_WATCH_WRITABLE;

	dbus_watch_handle(watch, flags);
}

static dbus_bool_t ble_scan_add_watch(DBusWatch *watch, void *ctx)
{
	struct event *ev;
	unsigned int flags;
	short cond;

	if (!dbus_watch_get_enabled(watch))
		return TRUE;

	flags = dbus_watch_get_flags(watch);
	cond = EV_PERSIST;
	if (flags & DBUS_WATCH_READABLE)
		cond |= EV_READ;
	if (flags & DBUS_WATCH_WRITABLE)
		cond |= EV_WRITE;

	ev = event_new(pltGetLibEventBase(), dbus_watch_get_unix_fd(watch),
		       cond, on_dbus_watch, watch);
	if (!ev)
		return FALSE;

	dbus_watch_set_data(watch, ev, NULL);

	if (event_add(ev, NULL) < 0) {
		dbus_watch_set_data(watch, NULL, NULL);
		event_free(ev);
		return FALSE;
	}

	return TRUE;
}

static void ble_scan_remove_watch(DBusWatch *watch, void *ctx)
{
	struct event *ev = dbus_watch_get_data(watch);

	if (ev)
		event_free(ev);

	dbus_watch_set_data(watch, NULL, NULL);
}

static void ble_scan_toggle_watch(DBusWatch *watch, void *ctx)
{
	if (dbus_watch_get_enabled(watch))
		ble_scan_add_watch(watch, ctx);
	else
		ble_scan_remove_watch(watch, ctx);
}

static void on_dbus_timeout(evutil_socket_t fd, short events, void *ctx)
{
	dbus_timeout_handle(ctx);
}

static dbus_bool_t ble_scan_add_timeout(DBusTimeout *timeout, void *ctx)
{
	struct event *ev;
	struct timeval tv;
	int ms;

	if (!dbus_timeout_get_enabled(timeout))
		return TRUE;

	ev = event_new(pltGetLibEventBase(), -1, EV_TIMEOUT | EV_PERSIST,
		       on_dbus_timeout, timeout);
	if (!ev)
		return FALSE;

	ms = dbus_timeout_get_interval(timeout);
	tv.tv_sec  = ms / 1000;
	tv.tv_usec = (ms % 1000) * 1000;

	dbus_timeout_set_data(timeout, ev, NULL);

	if (event_add(ev, &tv) < 0) {
		dbus_timeout_set_data(timeout, NULL, NULL);
		event_free(ev);
		return FALSE;
	}

	return TRUE;
}

static void ble_scan_remove_timeout(DBusTimeout *timeout, void *ctx)
{
	struct event *ev = dbus_timeout_get_data(timeout);

	if (ev)
		event_free(ev);

	dbus_timeout_set_data(timeout, NULL, NULL);
}

static void ble_scan_toggle_timeout(DBusTimeout *timeout, void *ctx)
{
	if (dbus_timeout_get_enabled(timeout))
		ble_scan_add_timeout(timeout, ctx);
	else
		ble_scan_remove_timeout(timeout, ctx);
}

static void ble_scan_close_bus(void)
{
	if (bluez_bus) {
		dbus_connection_remove_filter(bluez_bus, on_dbus_message, NULL);
		dbus_connection_close(bluez_bus);
		dbus_connection_unref(bluez_bus);
		bluez_bus = NULL;
	}

	if (dispatch_ev) {
		event_free(dispatch_ev);
		dispatch_ev = NULL;
	}
}

static int ble_scan_open_bus(void)
{
	DBusError err;

	if (bluez_bus)
		return 0;

	dbus_error_init(&err);

	bluez_bus = dbus_bus_get_private(DBUS_BUS_SYSTEM, &err);
	if (!bluez_bus) {
		fprintf(stderr, "bluez: %s: %s\n", err.name, err.message);
		dbus_error_free(&err);
		return -1;
	}

	dbus_connection_set_exit_on_disconnect(bluez_bus, FALSE);

	dispatch_ev = event_new(pltGetLibEventBase(), -1, EV_TIMEOUT,
				on_dbus_dispatch, NULL);
	if (!dispatch_ev)
		goto err;

	if (!dbus_connection_set_watch_functions(bluez_bus, ble_scan_add_watch,
						ble_scan_remove_watch,
						ble_scan_toggle_watch,
						NULL, NULL))
		goto err;

	if (!dbus_connection_set_timeout_functions(bluez_bus,
						  ble_scan_add_timeout,
						  ble_scan_remove_timeout,
						  ble_scan_toggle_timeout,
						  NULL, NULL))
		goto err;

	dbus_connection_set_dispatch_status_function(bluez_bus,
						     on_dbus_dispatch_status,
						     NULL, NULL);

	if (!dbus_connection_add_filter(bluez_bus, on_dbus_message, NULL, NULL))
		goto err;

	dbus_bus_add_match(bluez_bus,
			   "type='signal',sender='" BLUEZ_SERVICE "',"
			   "interface='" OBJMGR_IFACE "',"
			   "member='InterfacesAdded'", NULL);
	dbus_bus_add_match(bluez_bus,
			   "type='signal',sender='" BLUEZ_SERVICE "',"
			   "interface='" OBJMGR_IFACE "',"
			   "member='InterfacesRemoved'", NULL);
	dbus_bus_add_match(bluez_bus,
			   "type='signal',sender='" BLUEZ_SERVICE "',"
			   "interface='" PROPS_IFACE "',"
			   "member='PropertiesChanged',"
			   "arg0='" BLUEZ_ADAPTER_IFACE "'", NULL);
	dbus_bus_add_match(bluez_bus,
			   "type='signal',sender='" DBUS_SERVICE_DBUS "',"
			   "interface='" DBUS_INTERFACE_DBUS "',"
			   "member='NameOwnerChanged',"
			   "arg0='" BLUEZ_SERVICE "'", NULL);

	return 0;

err:
	fprintf(stderr, "failed to set up bluez dbus connection\n");
	ble_scan_close_bus();

	return -1;
}

int ble_scan_open(void)
{
	if (!ble_scan_enabled)
		return 0;

	if (ble_scan_open_bus() < 0)
		return -1;

	ble_scan_refresh_devices();

	return 0;
}

/* The scan duty cycle is controlled by bluez, this is only kept as state. */
void ble_scan_continuous(int cont)
{
	cont_scan = cont;
}

void ble_scan_close(void)
{
	ble_scan_close_all();
	ble_scan_close_bus();
}

void ble_scan_tick(void)
{
	static uint32_t ticks = 10 * TICKS_PER_SEC;
	int i;

	if (--ticks)
		return;

	ticks = 10 * TICKS_PER_SEC;

	if (!bluez_bus)
		return;

	/* Resync with bluez and restart discovery if it stopped. */
	for (i = 0; i < ARRAY_LENGTH(devices); i++) {
		struct hci_device *dev = &devices[i];

		if (dev->dev_id == HCI_DEV_NONE)
			continue;

		if (dev->discovering &&
		    ble_scan_get_bool(dev, "Discovering") == 0)
			dev->discovering = 0;

		if (!dev->discovering)
			ble_scan_start(dev);
	}
}

static void on_contscan_changed(struct VeItem *cont)
{
	VeVariant val;

	veItemLocalValue(cont, &val);
	if (!veVariantIsValid(&val))
		return;

	ble_scan_continuous(val.value.SN32 ? 1 : 0);
}

static void on_ble_enabled_changed(struct VeItem *item)
{
	VeVariant val;

	veItemLocalValue(item, &val);
	if (!veVariantIsValid(&val))
		return;

	if (ble_scan_enabled && !val.value.SN32) {
		ble_scan_enabled = 0;
		ble_scan_close();
	} else if (!ble_scan_enabled && val.value.SN32) {
		ble_scan_enabled = 1;
		ble_scan_open();
	}
}

int ble_scan_init(void)
{
	struct VeItem *settings = get_settings();
	struct VeItem *ctl	= get_control();
	struct VeItem *item;
	VeVariant val;
	int i;

	for (i = 0; i < ARRAY_LENGTH(devices); i++) {
		devices[i].dev_id	= HCI_DEV_NONE;
		devices[i].sock		= -1;
		devices[i].discovering	= 0;
		devices[i].name[0]	= '\0';
		devices[i].path[0]	= '\0';
		devices[i].ev		= NULL;
	}

	item = veItemCreateSettingsProxySync(settings, "Settings/BleSensors", ctl, "ContinuousScan",
					     veVariantFmt, &veUnitNone, &continuous_scan_props);
	veItemSetChanged(item, on_contscan_changed);
	veItemLocalValue(item, &val);
	if (veVariantIsValid(&val)) {
		cont_scan = val.value.SN32 ? 1 : 0;
	}

	item = veItemCreateSettingsProxySync(settings, "Settings/BleSensors", ctl, "Bluetooth/Enabled",
					     veVariantFmt, &veUnitNone, &ble_enabled_props);
	veItemSetChanged(item, on_ble_enabled_changed);
	veItemLocalValue(item, &val);
	if (veVariantIsValid(&val)) {
		ble_scan_enabled = val.value.SN32 ? 1 : 0;
	}

	return 0;
}
