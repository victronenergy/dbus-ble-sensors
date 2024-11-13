#ifndef BLE_SCAN_H
#define BLE_SCAN_H

#define MFG_ID_NORDIC	0x0059
#define MFG_ID_RUUVI	0x0499
#define MFG_ID_SAFIERY	0x0067
#define MFG_ID_SOLARSENSE	0x02E1

int ble_scan_open(void);
void ble_scan_continuous(int cont);
void ble_scan(void);
void ble_scan_close(void);
void ble_scan_tick(void);

#endif
