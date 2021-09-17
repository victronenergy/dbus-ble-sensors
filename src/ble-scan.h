#ifndef BLE_SCAN_H
#define BLE_SCAN_H

int ble_scan_open(const char *dev);
void ble_scan(void);
void ble_scan_close(void);
void ble_scan_tick(void);

#endif
