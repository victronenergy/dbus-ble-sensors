/**
 * Victron BLE Bridge
 *
 * Continuously scans BLE advertisements and forwards packets
 * immediately via HTTP POST.
 * 
 * Designed for Shelly Gen2 Plus devices with BLE.
 * This script also works with Shelly Gen2 Pro and Gen3 devices, but it is adviced to use the 
 * victron-ble-bridge-pro.js script instead which is easier to use.
 * Not compatible with Gen1 Shelly devices.
 *
 * Configuration via KVS (Advanced -> KVS in web UI):
 * - venus-host: Hostname or IP of Venus device (e.g., "venus.local")
 * - (optional) venus-password: GX password, cleared after successful auth
 *
 * Authentication: Either put your GX device in pairing mode (preferred), or set venus-password.
 */

/************ USER CONFIG ************/
const DBG = false;
/********** END USER CONFIG **********/

/************ CONSTANTS ************/
const POST_INTERVAL_MS = 2000;
const MAX_DEVICES_PER_POST = 25;
/************ CONSTANTS ************/

let gwMacNoColons = "000000000000";
let gwMacWithColons = "00:00:00:00:00:00";

let venusHost = null;
let tokenAuthBase64 = null;
let usedKvsPassword = false;
let bleAdvQueue = {};
let bleAdvQueueCount = 0;
let postTimer = null;
let postInProgress = false;

function handleGenerateTokenResponse(res, errCode, errMsg) {
    if (errCode !== 0) {
        print('ERROR: Token generation failed: ' + errCode + ' - ' + errMsg);
        print('Check: Is venus-host correct? Is the venus device reachable?');
        print('Verify in web UI: Advanced -> KVS -> venus-host');
        print('STOPPING SCRIPT');
        die();
    }
    
    if (DBG) {
        print('Token response: ' + JSON.stringify(res));
    }
    
    if (!res || res.code < 200 || res.code >= 300) {
        print('ERROR: Token generation HTTP error: ' + (res ? res.code : 'no response'));
        if (res && res.body) {
            print('Response body: ' + res.body);
        }
        if (res && res.code === 401) {
            print('Check: Is pairing mode enabled on the GX device?');
        } else {
            print('Check: Is the Venus device running and accessible?');
        }
        print('STOPPING SCRIPT');
        die();
    }
    
    try {
        let data = JSON.parse(res.body);
        if (data.token_name && data.password) {
            let tokenAuth = data.token_name + ':' + data.password;
            tokenAuthBase64 = btoa(tokenAuth);
            
            // Store token in Script.storage
            Script.storage.setItem('venus-token', tokenAuth);
            print('Token stored securely');
            
            // Clear venus-password from KVS if we used one
            if (usedKvsPassword) {
                Shelly.call('KVS.Set', {
                    key: 'venus-password',
                    value: ''
                }, function(setRes, setErr) {
                    if (setErr !== 0) {
                        print('WARNING: Failed to clear KVS password: ' + setErr);
                    } else {
                        print('venus-password cleared');
                    }
                });
                usedKvsPassword = false;
            }
            
            print('Authentication successful, starting BLE scanner...');
            startBLEScanning();
        } else {
            print('ERROR: Invalid token response - missing token_name or password');
            print('Check: Venus device may need firmware update or BLE bridge not enabled');
            print('STOPPING SCRIPT');
            die();
        }
    } catch (e) {
        print('ERROR: Failed to parse token response: ' + e.message);
        print('Check: Venus device may have returned unexpected response');
        print('STOPPING SCRIPT');
        die();
    }
}

function generateToken(host, password) {
    print('Authenticating with Venus host...');
    let url = 'https://' + host + '/ble-gw/';
    let authString = 'remoteconsole:' + password;
    let auth = btoa(authString);
    let body = JSON.stringify({
        generate_token: {
            device_id: 'shelly_' + gwMacNoColons
        }
    });

    if (DBG) {
        print('URL: ' + url);
        print('Body: ' + body);
    }

    Shelly.call('HTTP.Request', {
        method: 'POST',
        url: url,
        body: body,
        headers: {
            'Authorization': 'Basic ' + auth,
            'Content-Type': 'application/json'
        },
        ssl_ca: '*',
        timeout: 10
    }, handleGenerateTokenResponse);
}

function handlePostBLEDataResponse(res, errCode) {
    postInProgress = false;
    
    if (errCode !== 0 || !res || res.code < 200 || res.code >= 300) {
        print('ERROR: BLE POST failed: ' + (res ? 'HTTP ' + res.code : 'err ' + errCode));
        if (DBG && res) {
            print('Response: ' + JSON.stringify(res));
        }

        if (res && res.code === 403) {
            print('ERROR: Token invalid (403 Unauthorized)');
            if (res.body) {
                print('Response body: ' + res.body);
            }
            tokenAuthBase64 = null;
            Script.storage.removeItem('venus-token');
            stopBLEScanning();
            print('Authentication failed. To re-authenticate:');
            print('1. Put GX device in pairing mode');
            print('2. Restart this script');
            print('STOPPING SCRIPT');
            die();
        }
    } else {
        if (DBG) {
            print('BLE POST success: HTTP ' + res.code);
            if (res.body) print('Response: ' + res.body);
        }
    }
}

function postBLEData() {
    if (!tokenAuthBase64 || !venusHost) return;
    if (bleAdvQueueCount === 0) return;
    if (postInProgress) {
        if (DBG) {
            print('POST already in progress, skipping');
        }
        return;
    }

    postInProgress = true;
    // print("Posting data of " + bleAdvQueueCount + " BLE devices to Venus host...");
    let body = JSON.stringify({
        data: {
            gw_mac: gwMacWithColons,
            tags: bleAdvQueue
        }
    });
    bleAdvQueue = {};
    bleAdvQueueCount = 0;

    let url = 'https://' + venusHost + '/ble-gw/';

    if (DBG) {
        print('POST URL: ' + url);
        print('POST body length: ' + body.length + ' bytes');
    }

    Shelly.call('HTTP.Request', {
        method: 'POST',
        url: url,
        body: body,
        headers: {
            'Authorization': 'Basic ' + tokenAuthBase64,
            'Content-Type': 'application/json'
        },
        ssl_ca: '*',
        timeout: 5
    }, handlePostBLEDataResponse);
}

function onBLEScan(ev, res) {
    if (ev === BLE.Scanner.SCAN_STOPPED) {
        if (DBG) {
            print('BLE scanning stopped');
        }
        if (postTimer !== null) {
            startBLEScanning();
        }
        return;
    }
    if (ev === BLE.Scanner.SCAN_STARTED) {
        if (DBG) {
            print('BLE scanning started');
        }
        return;
    }
    if (ev !== BLE.Scanner.SCAN_RESULT || !res || !res.addr) return;
    
    if (!res.advData && !res.scanRsp) return;
    
    let data = (res.advData ? btoh(res.advData) : '') + (res.scanRsp ? btoh(res.scanRsp) : '');

    let entry = bleAdvQueue[res.addr];
    if (entry) {
        entry.rssi = res.rssi;
        entry.data = data;
    } else if (bleAdvQueueCount < MAX_DEVICES_PER_POST) {
        bleAdvQueue[res.addr] = {
            rssi: res.rssi,
            data: data
        };
        bleAdvQueueCount++;
    }
}

function stopBLEScanning() {
    if (postTimer !== null) {
        Timer.clear(postTimer);
        postTimer = null;
    }
    BLE.Scanner.Stop();
    print('BLE scanning stopped');
}

function startBLEScanning() {
    BLE.Scanner.Subscribe(onBLEScan);
    BLE.Scanner.Stop();
    BLE.Scanner.Start({ duration_ms: BLE.Scanner.INFINITE_SCAN, active: true });

    if (postTimer === null) {
        postTimer = Timer.set(POST_INTERVAL_MS, true, postBLEData);
    }

    print('BLE scanning active');
}

function handleVenusHost(res, errCode, errMsg) {
    if (errCode !== 0) {
        print('ERROR: Failed to read venus-host from KVS: ' + errCode + ' - ' + errMsg);
        print('To fix: Go to Advanced -> KVS in the web UI');
        print('Add key "venus-host" with your Venus hostname (e.g., venus.local or IP address)');
        print('Then restart this script.');
        print('STOPPING SCRIPT');
        die();
    }
    
    venusHost = res.value;
    if (!venusHost) {
        print('ERROR: venus-host is empty');
        print('To fix: Go to Advanced -> KVS in the web UI');
        print('Set "venus-host" to your Venus hostname (e.g., venus.local or IP address)');
        print('Then restart this script.');
        print('STOPPING SCRIPT');
        die();
    }
    
    print('Venus host: ' + venusHost);
    
    // Check if we have a stored token first
    let tokenAuth = Script.storage.getItem('venus-token');
    if (tokenAuth) {
        tokenAuthBase64 = btoa(tokenAuth);
        print('Using stored token, starting BLE scanner...');
        startBLEScanning();
    } else {
        // No stored token, check if venus-password is set in KVS
        Shelly.call('KVS.Get', { key: 'venus-password' }, function(pwRes, pwErr) {
            let password = '';
            if (pwErr === 0 && pwRes && pwRes.value) {
                password = pwRes.value;
                usedKvsPassword = true;
                print('Using password from KVS...');
            } else {
                print('No password in KVS, assuming GX is in pairing mode...');
            }
            generateToken(venusHost, password);
        });
    }
}

// Initialize
let deviceInfo = Shelly.getDeviceInfo();
gwMacNoColons = deviceInfo.mac.toUpperCase();
gwMacWithColons = gwMacNoColons[0] + gwMacNoColons[1] + ':' +
    gwMacNoColons[2] + gwMacNoColons[3] + ':' +
    gwMacNoColons[4] + gwMacNoColons[5] + ':' +
    gwMacNoColons[6] + gwMacNoColons[7] + ':' +
    gwMacNoColons[8] + gwMacNoColons[9] + ':' +
    gwMacNoColons[10] + gwMacNoColons[11];

print('===== Victron BLE Bridge =====');
print('Shelly MAC: ' + gwMacWithColons);
print('Debug: ' + (DBG ? 'enabled' : 'disabled'));
print('Loading configuration from KVS...');

Shelly.call('KVS.Get', { key: 'venus-host' }, handleVenusHost);
