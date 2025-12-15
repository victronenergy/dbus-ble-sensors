#ifndef BLE_SCAN_H
#define BLE_SCAN_H

#include <stdint.h>

#define MFG_ID_GOBIUS		0x0F53
#define MFG_ID_NORDIC		0x0059
#define MFG_ID_RUUVI		0x0499
#define MFG_ID_SAFIERY		0x0067
#define MFG_ID_SOLARSENSE	0x02E1

#define BLE_SOURCE_LOCAL	0
#define BLE_SOURCE_SOCKET	1

int ble_scan_init(void);
int ble_scan_open(void);
void ble_scan_continuous(int cont);
void ble_scan(void);
void ble_scan_close(void);
void ble_scan_tick(void);

int ble_handle_mfg(const uint8_t addr[6], uint16_t mfg_id,
		   const uint8_t *buf, int len, int source);
int ble_get_current_source(void);

#endif
