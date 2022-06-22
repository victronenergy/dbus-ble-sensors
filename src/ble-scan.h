#ifndef BLE_SCAN_H
#define BLE_SCAN_H

#define MFG_ID_RUUVI	0x0499

int ble_scan_open(void);
void ble_scan_continuous(int cont);
void ble_scan(void);
void ble_scan_close(void);
void ble_scan_tick(void);

#endif
