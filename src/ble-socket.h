#ifndef BLE_SOCKET_H
#define BLE_SOCKET_H

#define BLE_SOCKET_DEFAULT_PORT		18542
#define BLE_SOCKET_DEFAULT_BIND		"127.0.0.1"

/* Packet format version */
#define BLE_SOCKET_VERSION		1

/* Packet flags */
#define BLE_SOCKET_FLAG_RSSI		(1 << 0)
#define BLE_SOCKET_FLAG_REPEATER	(1 << 1)

int ble_socket_init(void);
void ble_socket_close(void);

#endif
