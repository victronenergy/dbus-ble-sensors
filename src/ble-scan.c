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

#include <event2/event.h>

#include <velib/platform/plt.h>
#include <velib/utils/ve_todo.h>

#include "ble-dbus.h"
#include "ble-scan.h"
#include "ble-handler.h"
#include "task.h"

#define SCAN_INTERVAL	90
#define SCAN_WINDOW	15

#define SCAN_TYPE_PASSIVE	0
#define SCAN_TYPE_ACTIVE	1

#define SCAN_SETTING_DEFAULT	0
#define SCAN_SETTING_PASSIVE	1
#define SCAN_SETTING_ACTIVE	2

struct mgmt_hdr {
	uint16_t opcode;
	uint16_t index;
	uint16_t len;
} __attribute__ ((packed));

#define MGMT_HDR_SIZE			6
#define MGMT_EV_INDEX_ADDED		0x0004
#define MGMT_EV_INDEX_REMOVED		0x0005
#define MGMT_EV_UNCONF_INDEX_ADDED	0x001d
#define MGMT_EV_UNCONF_INDEX_REMOVED	0x001e
#define MGMT_EV_EXT_INDEX_ADDED		0x0020
#define MGMT_EV_EXT_INDEX_REMOVED	0x0021

#define NAME_SIZE sizeof(((struct hci_dev_info *)0)->name)

struct hci_device {
	uint16_t dev_id;
	int sock;
	int addr_type;
	char name[NAME_SIZE];
	struct event *ev;
};

static struct hci_device devices[HCI_MAX_DEV];
static int cont_scan;
static int ble_scan_enabled = 1;
static int hci_ctl_sock = -1;
static int scan_type = 0;
static struct event *hci_ctl_ev = NULL;

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

static struct VeSettingProperties scan_type_props = {
	.type		= VE_SN32,
	.def.value.SN32 = 0,
	.min.value.SN32 = 0,
	.max.value.SN32 = 2,
};

static int ble_scan_setup(struct hci_device *dev, int addr_type)
{
	int interval = cont_scan ? SCAN_WINDOW : SCAN_INTERVAL;
	int type = scan_type == SCAN_SETTING_ACTIVE ? SCAN_TYPE_ACTIVE : SCAN_TYPE_PASSIVE;
	int err;

	if (dev->sock < 0)
		return 0;

	hci_le_set_scan_enable(dev->sock, 0, 1, 1000);

	err = hci_le_set_scan_parameters(dev->sock, type,
					 htobs(interval), htobs(SCAN_WINDOW),
					 addr_type, 0, 1000);
	if (err < 0)
		return -2;

	err = hci_le_set_scan_enable(dev->sock, 1, 0, 1000);
	if (err < 0)
		return -1;

	dev->addr_type = addr_type;

	return 0;
}

static int ble_scan_parse_adv(const le_advertising_info *adv)
{
	if (!ble_scan_enabled)
		return 0;

	ble_parse_adv(&adv->bdaddr, adv->data, adv->length);

	return 0;
}

static void ble_scan_close_dev(struct hci_device *dev)
{
	int flags;

	if (dev->dev_id == HCI_DEV_NONE)
		return;

	fprintf(stderr, "closing hci%d (%s)\n", dev->dev_id, dev->name);

	if (dev->ev != NULL) {
		event_free(dev->ev);
		dev->ev = NULL;
	}

	if (dev->sock >= 0) {

		flags = fcntl(dev->sock, F_GETFL);
		if (flags > 0)
			fcntl(dev->sock, F_SETFL, flags & ~O_NONBLOCK);

		hci_le_set_scan_enable(dev->sock, 0, 1, 1000);
		hci_close_dev(dev->sock);
		dev->sock = -1;
	}

	if (dev->name[0]) {
		ble_dbus_invalidate_interface(dev->name);
		veItemSendPendingChanges(get_control());
		dev->name[0]   = '\0';
	}

	dev->dev_id    = HCI_DEV_NONE;
	dev->addr_type = LE_PUBLIC_ADDRESS;
}

static void ble_scan_close_dev_id(uint16_t device_id) {
	int i;

	for (i = 0; i < ARRAY_LENGTH(devices); i++) {
		if (devices[i].dev_id == device_id) {
			ble_scan_close_dev(&devices[i]);
			return;
		}
	}
}

static void on_dev_socket_readable(evutil_socket_t fd, short events, void *ctx)
{
	struct hci_device *dev = ctx;
	uint8_t buf[HCI_MAX_EVENT_SIZE];
	hci_event_hdr *evt;
	evt_le_meta_event *mev;
	le_advertising_info *adv;
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
		msg += EVT_LE_META_EVENT_SIZE + 1;
		len -= EVT_LE_META_EVENT_SIZE + 1;

		if (mev->subevent != EVT_LE_ADVERTISING_REPORT)
			continue;

		if (len < LE_ADVERTISING_INFO_SIZE)
			continue;

		adv = (le_advertising_info *)msg;
		msg += LE_ADVERTISING_INFO_SIZE;
		len -= LE_ADVERTISING_INFO_SIZE;

		if (len < adv->length)
			continue;

		ble_scan_parse_adv(adv);
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

static void ble_scan_open_dev(int id)
{
	struct hci_dev_info info = { .dev_id = id };
	struct hci_filter filter;
	char addr[18];
	socklen_t len;
	int hci_sock;
	int flags;
	int err;
	struct hci_device *dev = ble_scan_first_free_device();

	fprintf(stderr, "opening hci%d\n", id);
	if (!dev) {
		fprintf(stderr, "no free device slot for hci%d\n", id);
		pltExit(-1);
	}
	dev->dev_id = id;

	hci_sock = hci_open_dev(id);
	if (hci_sock < 0) {
		perror("hci_open_dev");
		return;
	}
	dev->sock = hci_sock;

	err = ioctl(hci_sock, HCIGETDEVINFO, &info);
	if (err) {
		perror("HCIGETDEVINFO");
		goto err;
	}

	err = ioctl(hci_sock, HCIDEVUP, id);
	if (err && errno != EALREADY) {
		perror("HCIDEVUP");
		goto err;
	}

	err = ble_scan_setup(dev, LE_RANDOM_ADDRESS);
	if (err < 0)
		err = ble_scan_setup(dev, LE_PUBLIC_ADDRESS);

	if (err < 0) {
		if (err == -2)
			perror("hci_le_set_scan_parameters");
		else
			perror("hci_le_set_scan_enable");

		goto err;
	}

	len = sizeof(filter);
	err = getsockopt(hci_sock, SOL_HCI, HCI_FILTER, &filter, &len);
	if (err < 0) {
		perror("getsockopt");
		goto err;
	}

	hci_filter_set_ptype(HCI_EVENT_PKT, &filter);
	hci_filter_set_event(EVT_LE_META_EVENT, &filter);

	err = setsockopt(hci_sock, SOL_HCI, HCI_FILTER,
			 &filter, sizeof(filter));
	if (err < 0) {
		perror("setsockopt");
		goto err;
	}

	flags = fcntl(hci_sock, F_GETFL);
	if (flags < 0)
		goto err;

	err = fcntl(hci_sock, F_SETFL, flags | O_NONBLOCK);
	if (err < 0)
		goto err;

	ba2str(&info.bdaddr, addr);
	ble_dbus_add_interface(info.name, addr);
	veItemSendPendingChanges(get_control());
	memcpy(dev->name, info.name, NAME_SIZE);

	dev->ev = event_new(pltGetLibEventBase(), hci_sock,
			    EV_READ | EV_PERSIST, on_dev_socket_readable, dev);
	if (dev->ev == NULL) {
		perror("event_new");
		goto err;
	}

	if (event_add(dev->ev, NULL) < 0) {
		perror("event_add");
		goto err;
	}

	return;

err:
	ble_scan_close_dev(dev);
}

static int ble_scan_read_dev_list(uint16_t *device_ids, size_t nr_devices)
{
	struct hci_dev_list_req *dl;
	int sock;
	int i, n = 0;

	sock = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
	if (sock < 0) {
		perror("socket");
		return 0;
	}

	dl = calloc(1, sizeof(*dl) + nr_devices * sizeof(*dl->dev_req));
	if (!dl)
		goto out;

	dl->dev_num = nr_devices;
	if (ioctl(sock, HCIGETDEVLIST, dl)) {
		perror("HCIGETDEVLIST");
		goto out;
	}

	for (i = 0; i < dl->dev_num; i++) {
		device_ids[i] = dl->dev_req[i].dev_id;
	}
	n = dl->dev_num;

out:
	close(sock);
	free(dl);

	return n;
}

static int ble_scan_in_dev_id_list(uint16_t dev_id, uint16_t *list, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		if (list[i] == dev_id)
			return 1;
	}

	return 0;
}

static int ble_scan_in_dev_list(uint16_t dev_id)
{
	int i;

	for (i = 0; i < ARRAY_LENGTH(devices); i++) {
		if (devices[i].dev_id == dev_id)
			return 1;
	}

	return 0;
}

static void ble_scan_refresh_devices(void)
{
	uint16_t device_ids[ARRAY_LENGTH(devices)];

	int i, n;

	n = ble_scan_read_dev_list(device_ids, ARRAY_LENGTH(device_ids));

	for (i = 0; i < ARRAY_LENGTH(devices); i++) {
		if (devices[i].dev_id == HCI_DEV_NONE)
			continue;
		if (!ble_scan_in_dev_id_list(devices[i].dev_id, device_ids, n)) {
			ble_scan_close_dev(&devices[i]);
		}
	}

	for (i = 0; i < n; i++) {
		if (!ble_scan_in_dev_list(device_ids[i]))
			ble_scan_open_dev(device_ids[i]);
	}
}

static void ble_scan_close_ctl(void)
{
	if (hci_ctl_ev != NULL) {
		event_free(hci_ctl_ev);
		hci_ctl_ev = NULL;
	}
	if (hci_ctl_sock >= 0) {
		close(hci_ctl_sock);
		hci_ctl_sock = -1;
	}
}

static void on_ctl_socket_readable(evutil_socket_t fd, short events, void *ctx)
{
	uint8_t buf[1024];
	veBool refresh = veFalse;

	for (;;) {
		struct mgmt_hdr *hdr;
		uint16_t event;
		int len;
		uint16_t dev_id;
		uint16_t plen;

		len = read(hci_ctl_sock, buf, sizeof(buf));
		if (len < 0) {
			if (errno != EAGAIN)
				perror("hci control read");
			break;
		}

		if (len <= 0)
			break;

		if (len < MGMT_HDR_SIZE)
			continue;

		hdr   = (struct mgmt_hdr *)buf;
		event = btohs(hdr->opcode);
		dev_id = btohs(hdr->index);
		plen   = btohs(hdr->len);

		if (plen != len - MGMT_HDR_SIZE)
			continue;

		switch (event) {
		case MGMT_EV_INDEX_REMOVED:
		case MGMT_EV_UNCONF_INDEX_REMOVED:
		case MGMT_EV_EXT_INDEX_REMOVED:
			/* Close device immediately to avoid FD reuse race conditions */
			ble_scan_close_dev_id(dev_id);
			refresh = veTrue;
			break;

		case MGMT_EV_INDEX_ADDED:
		case MGMT_EV_UNCONF_INDEX_ADDED:
		case MGMT_EV_EXT_INDEX_ADDED:
			/* Device availability changes: refresh to detect changes */
			refresh = veTrue;
			break;
		}
	}

	if (refresh)
		ble_scan_refresh_devices();
}

static int ble_scan_open_ctl(void)
{
	struct sockaddr_hci addr = {
		.hci_family  = AF_BLUETOOTH,
		.hci_dev     = HCI_DEV_NONE,
		.hci_channel = HCI_CHANNEL_CONTROL,
	};
	int flags;

	hci_ctl_sock = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
	if (hci_ctl_sock < 0) {
		perror("hci control socket");
		goto err;
	}

	if (bind(hci_ctl_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("hci control bind");
		goto err;
	}

	flags = fcntl(hci_ctl_sock, F_GETFL);
	if (flags < 0 || fcntl(hci_ctl_sock, F_SETFL, flags | O_NONBLOCK) < 0) {
		perror("hci control fcntl");
		goto err;
	}

	hci_ctl_ev = event_new(pltGetLibEventBase(), hci_ctl_sock,
			       EV_READ | EV_PERSIST, on_ctl_socket_readable, NULL);
	if (hci_ctl_ev == NULL) {
		perror("event_new");
		goto err;
	}

	if (event_add(hci_ctl_ev, NULL) < 0) {
		perror("event_add");
		goto err;
	}

	return 0;

err:
	ble_scan_close_ctl();
	return -1;
}

int ble_scan_open(void)
{
	if (!ble_scan_enabled)
		return 0;
	
	if (ble_scan_open_ctl() < 0)
		return -1;

	ble_scan_refresh_devices();

	return 0;
}

void ble_scan_continuous(int cont)
{
	int i;

	if (cont == cont_scan)
		return;

	cont_scan = cont;

	for (i = 0; i < ARRAY_LENGTH(devices); i++) {
		ble_scan_setup(&devices[i], devices[i].addr_type);
	}
}

void ble_scan_close(void)
{
	int i;

	for (i = 0; i < ARRAY_LENGTH(devices); i++)
		ble_scan_close_dev(&devices[i]);

	ble_scan_close_ctl();
}

void ble_scan_tick(void)
{
	static uint32_t ticks = 10 * TICKS_PER_SEC;
	int i;

	if (--ticks)
		return;

	ticks = 10 * TICKS_PER_SEC;
	for (i = 0; i < ARRAY_LENGTH(devices); i++) {
		if (devices[i].sock >= 0)
			hci_le_set_scan_enable(devices[i].sock, 1, 0, 1000);
	}
}

static void on_contscan_changed(struct VeItem *cont)
{
	VeVariant val;
	int i;

	veItemLocalValue(cont, &val);
	if (!veVariantIsValid(&val))
		return;
	if (cont_scan == val.value.SN32)
		return;
	cont_scan = val.value.SN32;
	for (i = 0; i < ARRAY_LENGTH(devices); i++) {
		ble_scan_setup(&devices[i], devices[i].addr_type);
	}
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

static void on_scan_type_changed(struct VeItem *item)
{
	VeVariant val;
	int i;

	veItemLocalValue(item, &val);
	if (!veVariantIsValid(&val))
		return;

	if (scan_type == val.value.SN32)
		return;

	scan_type = val.value.SN32;
	for (i = 0; i < ARRAY_LENGTH(devices); i++) {
		ble_scan_setup(&devices[i], devices[i].addr_type);
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
		devices[i].dev_id  = HCI_DEV_NONE;
		devices[i].sock	   = -1;
		devices[i].name[0] = '\0';
		devices[i].ev	   = NULL;
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

	item = veItemCreateSettingsProxySync(settings, "Settings/BleSensors", ctl, "ActiveScan",
					     veVariantFmt, &veUnitNone, &scan_type_props);
	veItemSetChanged(item, on_scan_type_changed);
	veItemLocalValue(item, &val);
	if (veVariantIsValid(&val)) {
		scan_type = val.value.SN32;
	}

	return 0;
}
