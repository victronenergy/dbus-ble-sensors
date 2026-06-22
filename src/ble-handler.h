#ifndef BLE_HANDLER_H
#define BLE_HANDLER_H

#include <bluetooth/bluetooth.h>

#define MFG_ID_GOBIUS	0x0F53
#define MFG_ID_NORDIC	0x0059
#define MFG_ID_RUUVI	0x0499
#define MFG_ID_SAFIERY	0x0067
#define MFG_ID_VICTRON	0x02E1
#define MFG_ID_GARNET	0x0CC0

enum data_source {
	DATA_SOURCE_BLE,
	DATA_SOURCE_GATEWAY,

	DATA_SOURCE_NONE,
};

int ble_handle_mfg(const bdaddr_t *bdaddr, uint16_t mfg_id, const uint8_t *data, int len,
		   enum data_source source);
void ble_handle_name(const bdaddr_t *bdaddr, const uint8_t *buf, int len);

int ble_parse_adv(const bdaddr_t *bdaddr, const uint8_t *buf, int len);

#endif
