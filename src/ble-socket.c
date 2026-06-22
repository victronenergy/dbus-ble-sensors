#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <event2/event.h>

#include <velib/platform/plt.h>
#include <velib/types/ve_item.h>
#include <velib/utils/ve_item_utils.h>

#include "ble-dbus.h"
#include "ble-handler.h"
#include "ble-socket.h"
#include "task.h"

#define BLE_SOCKET_MAX_PACKET 128
#define BLE_SOCKET_MIN_SIZE_V1 11
#define BLE_SOCKET_MIN_SIZE_V2 14

struct ble_socket {
	int sock;
	struct event *ev;
	struct sockaddr_in bind_addr;
};

static struct ble_socket ble_sock = {
	.sock = -1,
};

static struct VeSettingProperties port_props = {
	.type		= VE_SN32,
	.def.value.SN32 = BLE_SOCKET_DEFAULT_PORT,
	.min.value.SN32 = 1,
	.max.value.SN32 = 65535,
};

static struct VeSettingProperties bind_props = {
	.type	       = VE_STR,
	.def.value.Ptr = BLE_SOCKET_DEFAULT_BIND,
};

static void ble_socket_stop(void)
{
	if (ble_sock.ev) {
		event_free(ble_sock.ev);
		ble_sock.ev = NULL;
	}

	if (ble_sock.sock >= 0) {
		close(ble_sock.sock);
		ble_sock.sock = -1;
	}
}

static void ble_socket_parse(const uint8_t *buf, int len)
{
	bdaddr_t bdaddr;
	uint8_t version;
	uint8_t flags;
	uint16_t mfg_id;
	uint8_t payload_len;
	const uint8_t *payload;
	int offset = 0;

	if (len < 1)
		return;

	version = buf[0];
	if (version == 1) {
		/* Format 1:
		 * byte 0: version (1)
		 * byte 1: flags
		 * bytes 2-7: bdaddr
		 * bytes 8-9: mfg_id
		 * byte 10: payload length
		 * bytes 11..(11+payload length): payload
		 * optional fields follow, depending on flags:
		 *   if flags & BLE_SOCKET_FLAG_RSSI:
		 *     byte (11+payload length): RSSI (signed)
		 *   if flags & BLE_SOCKET_FLAG_REPEATER:
		 *     bytes (12+payload length)..(17+payload length): repeater bdaddr
		 *   if flags & BLE_SOCKET_FLAG_NAME:
		 *     byte (12+payload length) or (18+payload length): name length N
		 *     bytes (13+payload length)..(12+payload length+N) or
		 *           (19+payload length)..(18+payload length+N): name (not zero ended)
		 */
		if (len < BLE_SOCKET_MIN_SIZE_V1) {
			return;
		}
		flags = buf[1];

		memcpy(&bdaddr, buf + 2, 6);

		mfg_id	    = buf[8] | (buf[9] << 8);
		payload_len = buf[10];
		payload	    = buf + 11;
		offset	    = 11 + payload_len;

		if (len < offset)
			return;
		ble_handle_mfg(&bdaddr, mfg_id, payload, payload_len, DATA_SOURCE_GATEWAY);

		if (flags & BLE_SOCKET_FLAG_RSSI) {
			offset += 1;
			if (len < offset)
				return;
		}

		if (flags & BLE_SOCKET_FLAG_REPEATER) {
			offset += 6;
			if (len < offset)
				return;
		}

		if (flags & BLE_SOCKET_FLAG_NAME) {
			offset += 1;
			if (len < offset)
				return;

			uint8_t name_len = buf[offset - 1];
			offset += name_len;
			if (len < offset)
				return;

			ble_handle_name(&bdaddr, buf + offset - name_len, name_len);
		}
	} else if (version == 2) {
		/* Format 2:
		 * byte 0: version (2)
		 * bytes 1-6: repeater bdaddr
		 * bytes 7-12: advertisement bdaddr
		 * byte 13: rssi (signed), 0x7F means invalid
		 * bytes 14-...: raw advertisement data (same format as in BLE scan results)
		 */
		if (len < BLE_SOCKET_MIN_SIZE_V2) {
			return;
		}
		memcpy(&bdaddr, buf + 7, 6);
		payload_len = len - 14;
		payload	    = buf + 14;

		ble_parse_adv(&bdaddr, payload, payload_len);
	}


	return;
}

static void ble_socket_read(evutil_socket_t fd, short events, void *arg)
{
	uint8_t buf[BLE_SOCKET_MAX_PACKET];
	struct sockaddr_in src;
	socklen_t srclen = sizeof(src);
	int len;

	len = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&src, &srclen);
	if (len < 0) {
		if (errno != EAGAIN)
			perror("socket recvfrom");
		return;
	}

	ble_socket_parse(buf, len);
}

static int ble_socket_start(const char *bind_addr, int port)
{
	int sock;
	int err;

	if (ble_sock.sock >= 0)
		return 0;

	if (!bind_addr || !strlen(bind_addr)) {
		fprintf(stderr, "Socket disabled.\n");
		return 0;
	}

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		perror("socket start");
		return -1;
	}

	memset(&ble_sock.bind_addr, 0, sizeof(ble_sock.bind_addr));
	ble_sock.bind_addr.sin_family = AF_INET;
	ble_sock.bind_addr.sin_port   = htons(port);

	err = inet_pton(AF_INET, bind_addr, &ble_sock.bind_addr.sin_addr);
	if (err != 1) {
		fprintf(stderr, "invalid bind address %d: %s\n", err, bind_addr);
		close(sock);
		return -1;
	}

	err = bind(sock, (struct sockaddr *)&ble_sock.bind_addr, sizeof(ble_sock.bind_addr));
	if (err < 0) {
		perror("socket bind");
		close(sock);
		return -1;
	}

	ble_sock.ev = event_new(pltGetLibEventBase(), sock, EV_READ | EV_PERSIST, ble_socket_read, NULL);
	if (!ble_sock.ev) {
		fprintf(stderr, "event_new failed\n");
		close(sock);
		return -1;
	}

	event_add(ble_sock.ev, NULL);
	ble_sock.sock = sock;

	fprintf(stderr, "Socket enabled. Listening on %s:%d\n", bind_addr, port);
	return 0;
}

void ble_socket_open(void)
{
	struct VeItem *ctl = get_control();
	struct VeItem *item;
	VeVariant val;
	const char *bind_addr;
	int port;

	port = veItemValueInt(ctl, "Socket/Port");
	item = veItemByUid(ctl, "Socket/BindAddress");
	if (item && veItemIsValid(item)) {
		veItemLocalValue(item, &val);
		bind_addr = val.value.Ptr;
	} else {
		bind_addr = BLE_SOCKET_DEFAULT_BIND;
	}

	if (ble_sock.sock >= 0)
		ble_socket_stop();

	ble_socket_start(bind_addr, port);
}

static void on_socket_setting_changed(struct VeItem *item)
{
	ble_socket_open();
}

int ble_socket_init(void)
{
	struct VeItem *settings = get_settings();
	struct VeItem *ctl	= get_control();
	struct VeItem *item;

	item = veItemCreateSettingsProxySync(settings, "Settings/BleSensors", ctl, "Socket/Port",
					     veVariantFmt, &veUnitNone, &port_props);
	veItemSetChanged(item, on_socket_setting_changed);

	item = veItemCreateSettingsProxySync(settings, "Settings/BleSensors", ctl, "Socket/BindAddress",
					     veVariantFmt, &veUnitNone, &bind_props);
	veItemSetChanged(item, on_socket_setting_changed);

	return 0;
}

void ble_socket_close(void)
{
	ble_socket_stop();
}
