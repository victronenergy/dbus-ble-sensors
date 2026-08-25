/**
 * Victron BLE Bridge
 *
 * Continuously scans BLE advertisements and forwards packets
 * immediately via HTTP POST.
 * 
 * Designed for Shelly Gen2 Pro and Gen3 devices with BLE.
 * Not compatible with Gen1 and Gen2 plus Shelly devices.
 * 
 * When this script, the necessary UI components are created on the home page of the Shelly web interface.
 * Fill in the venus-host (and press save), put the GX device in pairing mode and press the 
 * "Authenticate" button. When the authentication is successful, a restart of the script (after a reboot 
 * of the Shelly device) does not require a re-authentication as long as the script is not deleted.
 *
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

let tokenAuthBase64 = null;
let bleAdvQueue = {};
let bleAdvQueueCount = 0;  // Cache queue size
let postTimer = null;
let postInProgress = false;
let operational = false;

let statusLabelVcId = null;
let venusHostVcId = null;
let gxPasswordVcId = null;
let generateTokenVcId = null;
let gatewayGroupVcId = null;
let debugEnabledVcId = null;
let numberDevicesVcId = null;
let authMethodVcId = null;

let statusLabelHandle = null;
let venusHostHandle = null;
let gxPasswordHandle = null;
let debugEnabledHandle = null;
let numberDevicesHandle = null;
let authMethodHandle = null;

function updateGroupMembership() {
    if (gatewayGroupVcId === null) return;

    let memberKeys = [];
    memberKeys.push('text:' + statusLabelVcId);
    memberKeys.push('text:' + venusHostVcId);

    if (!tokenAuthBase64){
        memberKeys.push('button:' + generateTokenVcId);
    }

    if (!tokenAuthBase64 && authMethodHandle && authMethodHandle.getValue() === 'password') {
        memberKeys.push('text:' + gxPasswordVcId);
    }
    
    if (debugEnabled()) {
        memberKeys.push('enum:' + authMethodVcId);
        memberKeys.push('boolean:' + debugEnabledVcId);
        memberKeys.push('number:' + numberDevicesVcId);
    }

    Shelly.call('Group.Set', {
        id: gatewayGroupVcId,
        value: memberKeys
    });
}

function debugEnabled() {
    return DBG || (debugEnabledHandle && debugEnabledHandle.getValue());
}

function setStatus(msg, forcePrint) {
    if (forcePrint || debugEnabled()) print(msg);
    if (statusLabelHandle !== null) {
        statusLabelHandle.setValue(msg);
    }
}

function handleGenerateTokenResponse(res, errCode, errMsg) {
    if (errCode !== 0) {
        setStatus('Token gen failed: ' + errCode, true);
        return;
    }
    if (debugEnabled()) {
        print('Full response object: ' + JSON.stringify(res));
    }
    if (!res || res.code < 200 || res.code >= 300) {
        if (authMethodHandle && authMethodHandle.getValue() === 'password') {
            setStatus('Authentication failed: HTTP ' + (res ? res.code : 'error') + '. Make sure that the GX password is correct', true);
        } else {
            setStatus('Authentication failed: HTTP ' + (res ? res.code : 'error') + '. Make sure that the GX is in pairing mode', true);
        }
        return;
    }
    try {
        let data = JSON.parse(res.body);
        if (data.token_name && data.password) {
            let tokenAuth = data.token_name + ':' + data.password;
            Script.storage.setItem('victron_bletoken', tokenAuth);
            tokenAuthBase64 = btoa(tokenAuth);
            if (gxPasswordHandle) gxPasswordHandle.setValue('');
            updateGroupMembership();
            setStatus('Authentication OK, starting BLE...');
            startBLEScanning();
        } else {
            setStatus('Invalid token response');
        }
    } catch (e) {
        setStatus('Token parse error');
    }
}

function generateToken(host, password) {
    setStatus('Authenticating...');
    let url = 'https://' + host + '/ble-gw/';
    let authString = 'remoteconsole:' + password;
    let auth = btoa(authString);
    let body = JSON.stringify({
        generate_token: {
            device_id: 'shelly_' + gwMacNoColons
        }
    });

    if (debugEnabled()) {
        print('Auth string length: ' + authString.length);
        print('Auth header: Basic ' + auth);
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
        operational = false;
        setStatus('BLE POST failed: ' + (res ? 'HTTP ' + res.code : 'err ' + errCode), true);
        if (debugEnabled() && res) print('BLE POST response: ' + JSON.stringify(res));

        // If unauthorized (403), clear tokens and require re-authentication
        if (res && res.code === 403) {
            tokenAuthBase64 = null;
            Script.storage.removeItem('victron_bletoken');
            updateGroupMembership();
            stopBLEScanning();
            setStatus('Authentication failed - enable pairing mode and authenticate again', true);
        }
    } else {
        if (debugEnabled()) {
            print('BLE POST success: HTTP ' + res.code);
            if (res.body) print('BLE POST response body: ' + res.body);
        }
        if (!operational) {
            operational = true;
            setStatus('BLE Bridge operational');
        }
    }
}

function postBLEData() {
    if (!tokenAuthBase64 || !venusHostHandle) return;
    if (bleAdvQueueCount === 0) return;
    if (postInProgress) {
        if (debugEnabled()) {
            print('POST already in progress, skipping');
        }
        return;
    }

    postInProgress = true;

    if (numberDevicesHandle) {
        numberDevicesHandle.setValue(bleAdvQueueCount);
    }
    let body = JSON.stringify({
        data: {
            gw_mac: gwMacWithColons,
            tags: bleAdvQueue
        }
    });
    bleAdvQueue = {};
    bleAdvQueueCount = 0;

    let url = 'https://' + venusHostHandle.getValue() + '/ble-gw/';

    if (debugEnabled()) {
        print('BLE POST URL: ' + url);
        print('BLE POST body: ' + body);
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
        setStatus('BLE scanning stopped', true);
        if (postTimer !== null) {
            startBLEScanning();
        }
        return;
    }
    if (ev === BLE.Scanner.SCAN_STARTED) {
        setStatus('BLE scanning started', true);
        return;
    }
    if (ev !== BLE.Scanner.SCAN_RESULT || !res || !res.addr || (!res.advData && !res.scanRsp)) return;
    
    let data = (res.advData ? btoh(res.advData) : '') + (res.scanRsp ? btoh(res.scanRsp) : '');

    // Get existing entry or check if queue is full
    let entry = bleAdvQueue[res.addr];
    if (entry) {
        // Update existing entry
        entry.rssi = res.rssi;
        entry.data = data;
    } else if (bleAdvQueueCount < MAX_DEVICES_PER_POST) {
        // Add new entry
        bleAdvQueue[res.addr] = {
            rssi: res.rssi,
            data: data
        };
        bleAdvQueueCount++;
    }
    // else: queue full, silently drop new device
}

function stopBLEScanning() {
    if (postTimer !== null) {
        Timer.clear(postTimer);
        postTimer = null;
    }
    BLE.Scanner.Stop();
}

function startBLEScanning() {
    BLE.Scanner.Subscribe(onBLEScan);
    BLE.Scanner.Stop();
    BLE.Scanner.Start({ duration_ms: BLE.Scanner.INFINITE_SCAN, active: true });

    if (postTimer === null) {
        postTimer = Timer.set(POST_INTERVAL_MS, true, postBLEData);
    }

    setStatus('BLE scanning active', true);
}

function ensureVirtualComponents(manifest, done) {
    var VC_HELPER_DELAY_MS = 150;
    var state = {
        existing: [],
        ids: {},
        keys: {},
        handles: {},
        ok: true
    };

    function log(msg) {
        print("[VC] " + msg);
    }

    function componentKey(type, id) {
        return type + ":" + String(id);
    }

    function shallowConfigMatches(desired, current) {
        var k;

        if (!desired || !current) return false;

        for (k in desired) {
            if (k === "meta") {
                if (JSON.stringify(desired.meta) !== JSON.stringify(current.meta || {})) return false;
            } else if (typeof desired[k] === "object" && desired[k] !== null) {
                if (JSON.stringify(desired[k]) !== JSON.stringify(current[k])) return false;
            } else if (desired[k] !== current[k]) {
                return false;
            }
        }

        return true;
    }

    function normalizeComponent(spec) {
        if (!spec.config) spec.config = {};
        if (!spec.config.name) spec.config.name = spec.key;
        return spec;
    }

    function findExistingByName(type, name) {
        var i;
        var c;
        for (i = 0; i < state.existing.length; i++) {
            c = state.existing[i];
            if (c.type === type && c.name === name) return c;
        }
        return null;
    }

    function remember(spec, id) {
        var key = componentKey(spec.type, id);
        state.ids[spec.key] = id;
        state.keys[spec.key] = key;
        state.handles[spec.key] = Virtual.getHandle(key);
    }

    function rememberGroup(spec, id) {
        var key = componentKey("group", id);
        state.ids[spec.key] = id;
        state.keys[spec.key] = key;
        state.handles[spec.key] = Virtual.getHandle(key);
    }

    function getConfig(type, id) {
        return Shelly.getComponentConfig(type, id);
    }

    function deleteComponent(key, cb) {
        Shelly.call("Virtual.Delete", { key: key }, function (res, errCode, errMsg) {
            if (errCode !== 0) {
                log("Virtual.Delete skipped for " + key + ": " + String(errCode) + " " + String(errMsg));
            }
            Timer.set(VC_HELPER_DELAY_MS, false, cb);
        });
    }

    function addComponent(spec, cb) {
        var params = { type: spec.type, config: spec.config };
        if (spec.id !== undefined && spec.id !== null) params.id = spec.id;

        Shelly.call("Virtual.Add", params, function (res, errCode, errMsg) {
            var id;

            if (errCode !== 0) {
                log("Virtual.Add failed for " + spec.key + ": " + String(errCode) + " " + String(errMsg));
                state.ok = false;
                cb(false);
                return;
            }

            id = spec.id;
            if ((id === undefined || id === null) && res && res.id !== undefined) id = res.id;
            if (id === undefined || id === null) {
                log("Virtual.Add did not return id for " + spec.key);
                state.ok = false;
                cb(false);
                return;
            }

            remember(spec, id);
            log("Created " + state.keys[spec.key] + " " + spec.config.name);
            Timer.set(VC_HELPER_DELAY_MS, false, function () { cb(true); });
        });
    }

    function ensureOne(spec, cb) {
        var current;
        var existing;
        var key;

        spec = normalizeComponent(spec);

        if (spec.id !== undefined && spec.id !== null) {
            current = getConfig(spec.type, spec.id);
            key = componentKey(spec.type, spec.id);

            if (current) {
                if (shallowConfigMatches(spec.config, current)) {
                    remember(spec, spec.id);
                    cb(true);
                    return;
                }

                log("Recreating mismatched " + key + " " + spec.config.name);
                deleteComponent(key, function () { addComponent(spec, cb); });
                return;
            }

            addComponent(spec, cb);
            return;
        }

        existing = findExistingByName(spec.type, spec.config.name);
        if (existing && shallowConfigMatches(spec.config, existing.config)) {
            remember(spec, existing.id);
            cb(true);
            return;
        }

        if (existing) {
            log("Existing " + existing.key + " does not fit " + spec.config.name + "; creating a new one");
        }
        addComponent(spec, cb);
    }

    function ensureList(index, cb) {
        var list = manifest.components || [];
        if (index >= list.length) {
            cb();
            return;
        }

        ensureOne(list[index], function () {
            Timer.set(VC_HELPER_DELAY_MS, false, function () {
                ensureList(index + 1, cb);
            });
        });
    }

    function createGroupConfig(name) {
        return { name: name, meta: { ui: { view: "group" } } };
    }

    function groupMembers(group) {
        var members = [];
        var i;
        var logicalKey;

        for (i = 0; i < group.components.length; i++) {
            logicalKey = group.components[i];
            if (state.keys[logicalKey]) members.push(state.keys[logicalKey]);
        }

        return members;
    }

    function ensureGroup(index, cb) {
        var groups = manifest.groups || [];
        var group;
        var cfg;
        var current;
        var key;

        if (index >= groups.length) {
            cb();
            return;
        }

        group = groups[index];
        cfg = createGroupConfig(group.name);
        key = componentKey("group", group.id);
        current = getConfig("group", group.id);

        function setMembersAndContinue() {
            let members = groupMembers(group);
            if (debugEnabled()) {
                print("Setting group " + key + " members: " + JSON.stringify(members));
            }

            // Store group ID if group has a key
            if (group.key) {
                rememberGroup(group, group.id);
            }

            Shelly.call("Group.Set", { id: group.id, value: members }, function (res, errCode, errMsg) {
                if (errCode !== 0) {
                    log("Group.Set failed for " + key + ": " + String(errCode) + " " + String(errMsg));
                    state.ok = false;
                }
                Timer.set(VC_HELPER_DELAY_MS, false, function () { ensureGroup(index + 1, cb); });
            });
        }

        if (current && shallowConfigMatches(cfg, current)) {
            setMembersAndContinue();
            return;
        }

        function addGroup() {
            Shelly.call("Virtual.Add", { type: "group", id: group.id, config: cfg }, function (res, errCode, errMsg) {
                if (errCode !== 0) {
                    log("Virtual.Add group failed for " + key + ": " + String(errCode) + " " + String(errMsg));
                    state.ok = false;
                    Timer.set(VC_HELPER_DELAY_MS, false, function () { ensureGroup(index + 1, cb); });
                    return;
                }
                setMembersAndContinue();
            });
        }

        if (current) {
            deleteComponent(key, addGroup);
        } else {
            addGroup();
        }
    }

    function readExistingPage(offset, cb) {
        Shelly.call("Shelly.GetComponents", { dynamic_only: true, offset: offset }, function (res, errCode, errMsg) {
            var raw;
            var total;
            var i;
            var c;
            var cfg;
            var keyParts;

            if (errCode !== 0) {
                log("Shelly.GetComponents failed: " + String(errCode) + " " + String(errMsg));
                state.ok = false;
                cb();
                return;
            }

            raw = (res && res.components) ? res.components : [];
            total = res ? (res.total || raw.length) : raw.length;

            for (i = 0; i < raw.length; i++) {
                c = raw[i];
                cfg = c.config || {};
                keyParts = (c.key || "").split(":");
                state.existing.push({
                    key: c.key || componentKey(c.type || keyParts[0], cfg.id),
                    type: c.type || keyParts[0],
                    id: cfg.id,
                    name: cfg.name,
                    config: cfg
                });
            }

            if (offset + raw.length < total && raw.length > 0) {
                readExistingPage(offset + raw.length, cb);
            } else {
                cb();
            }
        });
    }

    readExistingPage(0, function () {
        ensureList(0, function () {
            state.existing = null;  // no longer needed
            ensureGroup(0, function () {
                done(state.ok, {
                    ids: state.ids,
                    keys: state.keys,
                    handles: state.handles
                });
            });
        });
    });
}

function handleGenerateTokenButtonPress() {
    if (debugEnabled()) {
        print('Generate Token button pressed!');
    }
    if (venusHostHandle !== null && gxPasswordHandle !== null) {
        let host = venusHostHandle.getValue();
        let pass = gxPasswordHandle.getValue();
        if (!host) {
            setStatus('Venus host required');
        } else {
            setStatus('Authenticating with Venus host...');
            generateToken(host, pass || '');
        }
    }
}

function handleVirtualComponentsReady(ok, vc) {
    if (!ok) {
        print("Virtual Component setup failed");
        return;
    }

    statusLabelVcId = vc.ids.statusLabel;
    venusHostVcId = vc.ids.venusHost;
    gxPasswordVcId = vc.ids.gxPassword;
    generateTokenVcId = vc.ids.generateToken;
    gatewayGroupVcId = vc.ids.gatewayGroup;
    debugEnabledVcId = vc.ids.debugEnabled;
    numberDevicesVcId = vc.ids.numberDevices;
    authMethodVcId = vc.ids.authMethod;

    statusLabelHandle = vc.handles.statusLabel;
    venusHostHandle = vc.handles.venusHost;
    gxPasswordHandle = vc.handles.gxPassword;
    debugEnabledHandle = vc.handles.debugEnabled;
    numberDevicesHandle = vc.handles.numberDevices;
    authMethodHandle = vc.handles.authMethod;

    vc.handles.generateToken.on('single_push', handleGenerateTokenButtonPress);
    vc.handles.authMethod.on('change', updateGroupMembership);
    vc.handles.debugEnabled.on('change', updateGroupMembership);

    // Load stored tokens and auto-start if available
    let tokenAuth = Script.storage.getItem('victron_bletoken');
    tokenAuthBase64 = tokenAuth ? btoa(tokenAuth) : null;
    updateGroupMembership();
    if (tokenAuthBase64) {
        setStatus('Authentication loaded, starting BLE...');
        startBLEScanning();
    } else {
        setStatus('Enable pairing mode and press the Authenticate button');
    }

    print('Victron BLE Bridge UI components ready.');
}

let deviceInfo = Shelly.getDeviceInfo();
gwMacNoColons = deviceInfo.mac.toUpperCase();
gwMacWithColons = gwMacNoColons[0] + gwMacNoColons[1] + ':' +
    gwMacNoColons[2] + gwMacNoColons[3] + ':' +
    gwMacNoColons[4] + gwMacNoColons[5] + ':' +
    gwMacNoColons[6] + gwMacNoColons[7] + ':' +
    gwMacNoColons[8] + gwMacNoColons[9] + ':' +
    gwMacNoColons[10] + gwMacNoColons[11];
deviceInfo = null;  // free memory

print("Shelly MAC: " + gwMacWithColons);

let VIRTUAL_COMPONENTS_MANIFEST = {
    components: [
        {
            key: "statusLabel",
            type: "text",
            config: {
                name: 'Status',
                default_value: "Ready",
                persisted: false,
                meta: { ui: { view: "label", maxLength: 255 } }
            }
        },
        {
            key: "venusHost",
            type: "text",
            config: {
                name: 'Venus Host',
                default_value: "venus.local",
                persisted: true,
                meta: { ui: { view: "field", maxLength: 128 } }
            }
        },
        {
            key: "gxPassword",
            type: "text",
            config: {
                name: 'GX password',
                default_value: "",
                persisted: false,
                meta: { ui: { view: "field", maxLength: 128, password: true } }
            }
        },
        {
            key: "authMethod",
            type: "enum",
            config: {
                name: 'Authentication method',
                default_value: "pairing",
                persisted: false,
                options: ["pairing", "password"],
                meta: { ui: { view: "dropdown", 
                    titles: {pairing: "Pairing mode", password: "GX password"}, 
                    icons: null, 
                    images: {pairing: null, password: null} } }
            }
        },
        {
            key: "generateToken",
            type: "button",
            config: {
                name: 'Authenticate',
                meta: { ui: { view: "button" } }
            }
        },
        {
            key: "debugEnabled",
            type: "boolean",
            config: {
                name: 'Debug logging',
                default_value: false,
                persisted: false,
                meta: { ui: { view: "toggle", titles: ["Disabled", "Enabled"], buttonIcons: null, icon: null } }
            }
        },
        {
            key: "numberDevices",
            type: "number",
            config: {
                name: 'BLE devices sent',
                default_value: 0,
                min: 0,
                max: 1000,
                persisted: false,
                meta: { ui: { view: "label", unit: "devices", step: 1, icon: null } }
            }
        }
    ],
    groups: [
        { key: "gatewayGroup", id: 200, name: 'Victron BLE Bridge', components: [] }
    ]
};

ensureVirtualComponents(VIRTUAL_COMPONENTS_MANIFEST, handleVirtualComponentsReady);
VIRTUAL_COMPONENTS_MANIFEST = null;  // free memory
