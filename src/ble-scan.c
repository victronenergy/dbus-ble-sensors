#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include <velib/platform/plt.h>

#include "ble-dbus.h"
#include "ble-scan.h"
#include "mopeka.h"
#include "ruuvi.h"
#include "task.h"

#define SCAN_INTERVAL	90
#define SCAN_WINDOW	15

struct hci_device {
	int id;
	int sock;
	int addr_type;
};

static struct hci_device devices[HCI_MAX_DEV];
static int num_devices;
static int cont_scan;

static int ble_scan_setup(struct hci_device *dev, int addr_type)
{
	int interval = cont_scan ? SCAN_WINDOW : SCAN_INTERVAL;
	int err;

	hci_le_set_scan_enable(dev->sock, 0, 1, 1000);

	err = hci_le_set_scan_parameters(dev->sock, 0,
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

static int ble_scan_open_dev(int id, struct hci_device *dev)
{
	struct hci_filter filter;
	socklen_t len;
	int hci_sock;
	int flags;
	int err;

	fprintf(stderr, "opening hci%d\n", id);

	hci_sock = hci_open_dev(id);
	if (hci_sock < 0) {
		perror("hci_open_dev");
		return -1;
	}

	err = ioctl(hci_sock, HCIDEVUP, id);
	if (err && errno != EALREADY) {
		perror("HCIDEVUP");
		return -1;
	}

	dev->id = id;
	dev->sock = hci_sock;

	err = ble_scan_setup(dev, LE_RANDOM_ADDRESS);
	if (err < 0)
		err = ble_scan_setup(dev, LE_PUBLIC_ADDRESS);

	if (err < 0) {
		if (err == -2)
			perror("hci_le_set_scan_parameters");
		else
			perror("hci_le_set_scan_enable");

		return -1;
	}

	len = sizeof(filter);
	err = getsockopt(hci_sock, SOL_HCI, HCI_FILTER, &filter, &len);
	if (err < 0) {
		perror("getsockopt");
		return -1;
	}

	hci_filter_set_ptype(HCI_EVENT_PKT, &filter);
	hci_filter_set_event(EVT_LE_META_EVENT, &filter);

	err = setsockopt(hci_sock, SOL_HCI, HCI_FILTER,
			 &filter, sizeof(filter));
	if (err < 0) {
		perror("setsockopt");
		return -1;
	}

	flags = fcntl(hci_sock, F_GETFL);
	if (flags < 0)
		return -1;

	err = fcntl(hci_sock, F_SETFL, flags | O_NONBLOCK);
	if (err < 0)
		return -1;

	pltWatchFileDescriptor(hci_sock);

	return 0;
}

int ble_scan_open(void)
{
	struct hci_dev_list_req *dl;
	int sock;
	int err = -1;
	int i, j;

	sock = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
	if (sock < 0) {
		perror("socket");
		return -1;
	}

	dl = malloc(sizeof(*dl) + HCI_MAX_DEV * sizeof(*dl->dev_req));
	if (!dl)
		goto out;

	dl->dev_num = HCI_MAX_DEV;
	err = ioctl(sock, HCIGETDEVLIST, dl);
	if (err) {
		perror("HCIGETDEVLIST");
		goto out;
	}

	for (i = 0, j = 0; i < dl->dev_num; i++) {
		err = ble_scan_open_dev(dl->dev_req[i].dev_id, &devices[j]);
		if (!err)
			j++;
	}

	num_devices = j;

out:
	close(sock);
	free(dl);

	return num_devices ? 0 : -1;
}

void ble_scan_continuous(int cont)
{
	int i;

	if (cont == cont_scan)
		return;

	cont_scan = cont;

	for (i = 0; i < num_devices; i++)
		ble_scan_setup(&devices[i], devices[i].addr_type);
}

static int ble_handle_name(const bdaddr_t *addr, const uint8_t *buf, int len)
{
	char name[256];
	char dev[16];

	snprintf(dev, sizeof(dev), "%02x%02x%02x%02x%02x%02x",
		 addr->b[5], addr->b[4], addr->b[3],
		 addr->b[2], addr->b[1], addr->b[0]);

	memcpy(name, buf, len);
	name[len] = 0;

	return ble_dbus_set_name(dev, name);
}

static int ble_handle_mfg(const bdaddr_t *addr, const uint8_t *buf, int len)
{
	int mfg;

	if (len < 2)
		return -1;

	mfg = bt_get_le16(buf);
	buf += 2;
	len -= 2;

	if (mfg == MFG_ID_RUUVI)
		return ruuvi_handle_mfg(buf, len);

	else if (mfg == MFG_ID_MOPEKA)
		return mopeka_handle_mfg(buf, len);

	return 0;
}

static int ble_parse_adv(const le_advertising_info *adv)
{
	const uint8_t *buf = adv->data;
	int len = adv->length;

	while (len >= 2) {
		int adlen = *buf++ - 1;
		int adtyp = *buf++;

		len -= 2;

		if (len < adlen)
			break;

		switch (adtyp) {
		case 0x09:	/* Complete Local Name */
			ble_handle_name(&adv->bdaddr, buf, adlen);
			break;

		case 0xff:	/* Manufacturer Specific Data */
			ble_handle_mfg(&adv->bdaddr, buf, adlen);
			break;
		}

		buf += adlen;
		len -= adlen;
	}

	return 0;
}

static void ble_read_dev(struct hci_device *dev)
{
	uint8_t buf[HCI_MAX_EVENT_SIZE];
	hci_event_hdr *evt;
	evt_le_meta_event *mev;
	le_advertising_info *adv;
	int len;

	for (;;) {
		uint8_t *msg = buf;

		len = read(dev->sock, buf, sizeof(buf));
		if (len < 0 && errno != EAGAIN) {
			fprintf(stderr, "hci%d: read: %s\n", dev->id,
				strerror(errno));
			pltExit(1);
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

		ble_parse_adv(adv);
	}
}

void ble_scan(void)
{
	int i;

	for (i = 0; i < num_devices; i++)
		ble_read_dev(&devices[i]);
}

static void ble_close_dev(struct hci_device *dev)
{
	int flags;

	flags = fcntl(dev->sock, F_GETFL);
	if (flags > 0)
		fcntl(dev->sock, F_SETFL, flags & ~O_NONBLOCK);

	hci_le_set_scan_enable(dev->sock, 0, 1, 1000);
	hci_close_dev(dev->sock);
}

void ble_scan_close(void)
{
	int i;

	for (i = 0; i < num_devices; i++)
		ble_close_dev(&devices[i]);
}

void ble_scan_tick(void)
{
	static uint32_t ticks = 10 * TICKS_PER_SEC;
	int i;

	if (!--ticks) {
		ticks = 10 * TICKS_PER_SEC;
		for (i = 0; i < num_devices; i++)
			hci_le_set_scan_enable(devices[i].sock, 1, 0, 1000);
	}
}
