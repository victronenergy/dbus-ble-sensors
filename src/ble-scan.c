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

/*
 * LE extended scanning (Bluetooth 5). A controller accepts either the legacy
 * or the extended LE command set, whichever is used first after a reset, and
 * rejects the other with Command Disallowed. The kernel uses the extended
 * commands whenever the controller supports them, so scanning these with the
 * legacy commands locks BlueZ out of them: its scans and connections fail.
 * Use the same command set as the kernel does.
 */
#define OCF_LE_SET_EXT_SCAN_PARAMETERS	0x0041
#define OCF_LE_SET_EXT_SCAN_ENABLE	0x0042
#define EVT_LE_EXT_ADVERTISING_REPORT	0x0d

/* Supported Commands octet 37, as checked by the kernel's use_ext_scan() */
#define CMD_LE_SET_EXT_SCAN_PARAMETERS	0x20
#define CMD_LE_SET_EXT_SCAN_ENABLE	0x40

#define LE_SCAN_PHY_1M			0x01
#define LE_SCAN_PASSIVE			0x00

/* Event type, bits 5-6: data status */
#define LE_EXT_ADV_DATA_STATUS_MASK	0x0060
#define LE_EXT_ADV_DATA_COMPLETE	0x0000

typedef struct {
	uint8_t		own_addr_type;
	uint8_t		filter_policy;
	uint8_t		scanning_phys;
	uint8_t		type;
	uint16_t	interval;
	uint16_t	window;
} __attribute__ ((packed)) le_set_ext_scan_parameters_cp;

typedef struct {
	uint8_t		enable;
	uint8_t		filter_dup;
	uint16_t	duration;
	uint16_t	period;
} __attribute__ ((packed)) le_set_ext_scan_enable_cp;

typedef struct {
	uint16_t	evt_type;
	uint8_t		bdaddr_type;
	bdaddr_t	bdaddr;
	uint8_t		primary_phy;
	uint8_t		secondary_phy;
	uint8_t		sid;
	int8_t		tx_power;
	int8_t		rssi;
	uint16_t	interval;
	uint8_t		direct_bdaddr_type;
	bdaddr_t	direct_bdaddr;
	uint8_t		length;
	uint8_t		data[0];
} __attribute__ ((packed)) le_ext_advertising_info;
#define LE_EXT_ADVERTISING_INFO_SIZE	24

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
	int ext;	/* use the LE extended scanning commands */
	char name[NAME_SIZE];
	struct event *ev;
};

static struct hci_device devices[HCI_MAX_DEV];
static int cont_scan;
static int ble_scan_enabled = 1;
static int hci_ctl_sock = -1;
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

static int ble_le_send(int sock, uint16_t ocf, void *cp, int clen)
{
	struct hci_request rq = {
		.ogf	= OGF_LE_CTL,
		.ocf	= ocf,
		.cparam	= cp,
		.clen	= clen,
	};
	uint8_t status;

	rq.rparam = &status;
	rq.rlen = 1;

	if (hci_send_req(sock, &rq, 1000) < 0)
		return -1;

	if (status) {
		errno = EIO;
		return -1;
	}

	return 0;
}

static int ble_scan_enable(struct hci_device *dev, uint8_t enable, uint8_t filter_dup)
{
	le_set_ext_scan_enable_cp cp = {
		.enable		= enable,
		.filter_dup	= filter_dup,
	};

	if (!dev->ext)
		return hci_le_set_scan_enable(dev->sock, enable, filter_dup, 1000);

	return ble_le_send(dev->sock, OCF_LE_SET_EXT_SCAN_ENABLE, &cp, sizeof(cp));
}

static int ble_scan_parameters(struct hci_device *dev, int interval, int addr_type)
{
	le_set_ext_scan_parameters_cp cp = {
		.own_addr_type	= addr_type,
		.scanning_phys	= LE_SCAN_PHY_1M,
		.type		= LE_SCAN_PASSIVE,
		.interval	= htobs(interval),
		.window		= htobs(SCAN_WINDOW),
	};

	if (!dev->ext)
		return hci_le_set_scan_parameters(dev->sock, LE_SCAN_PASSIVE,
						  htobs(interval), htobs(SCAN_WINDOW),
						  addr_type, 0, 1000);

	return ble_le_send(dev->sock, OCF_LE_SET_EXT_SCAN_PARAMETERS, &cp, sizeof(cp));
}

/* Whether the kernel drives this controller with the extended commands */
static int ble_scan_use_ext(int sock)
{
	uint8_t commands[64];

	if (hci_read_local_commands(sock, commands, 1000) < 0)
		return 0;

	return (commands[37] & CMD_LE_SET_EXT_SCAN_PARAMETERS) &&
	       (commands[37] & CMD_LE_SET_EXT_SCAN_ENABLE);
}

static int ble_scan_setup(struct hci_device *dev, int addr_type)
{
	int interval = cont_scan ? SCAN_WINDOW : SCAN_INTERVAL;
	int err;

	if (dev->sock < 0)
		return 0;

	ble_scan_enable(dev, 0, 1);

	err = ble_scan_parameters(dev, interval, addr_type);
	if (err < 0)
		return -2;

	err = ble_scan_enable(dev, 1, 0);
	if (err < 0)
		return -1;

	dev->addr_type = addr_type;

	return 0;
}

static int ble_scan_parse_adv(const le_advertising_info *adv)
{
	if (!ble_scan_enabled)
		return 0;

	ble_parse_adv(&adv->bdaddr, adv->data, adv->length, DATA_SOURCE_BLE);

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

		ble_scan_enable(dev, 0, 1);
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
	dev->ext       = 0;
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

static void ble_scan_parse_ext_adv(const uint8_t *msg, int len)
{
	const le_ext_advertising_info *adv;
	int num_reports;

	if (len < 1)
		return;

	num_reports = msg[0];
	msg++;
	len--;

	while (num_reports-- > 0 && len >= LE_EXT_ADVERTISING_INFO_SIZE) {
		adv = (const le_ext_advertising_info *)msg;
		msg += LE_EXT_ADVERTISING_INFO_SIZE;
		len -= LE_EXT_ADVERTISING_INFO_SIZE;

		if (len < adv->length)
			return;

		/* Skip advertising data split over several reports */
		if ((btohs(adv->evt_type) & LE_EXT_ADV_DATA_STATUS_MASK) == LE_EXT_ADV_DATA_COMPLETE &&
		    ble_scan_enabled)
			ble_parse_adv(&adv->bdaddr, adv->data, adv->length, DATA_SOURCE_BLE);

		msg += adv->length;
		len -= adv->length;
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
		msg += EVT_LE_META_EVENT_SIZE;
		len -= EVT_LE_META_EVENT_SIZE;

		if (mev->subevent == EVT_LE_EXT_ADVERTISING_REPORT) {
			ble_scan_parse_ext_adv(msg, len);
			continue;
		}

		if (mev->subevent != EVT_LE_ADVERTISING_REPORT)
			continue;

		/* Skip the number of reports; only the first one is parsed */
		msg++;
		len--;

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

	dev->ext = ble_scan_use_ext(hci_sock);
	fprintf(stderr, "hci%d: %s scanning\n", id, dev->ext ? "extended" : "legacy");

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
			ble_scan_enable(&devices[i], 1, 0);
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

	return 0;
}
