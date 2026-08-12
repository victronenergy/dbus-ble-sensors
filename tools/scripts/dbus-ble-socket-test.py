#!/usr/bin/env python3
"""
Test script for sending BLE sensor data via UDP socket.
"""

import argparse
import base64
import json
import socket
import ssl
import struct
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

"""
PACKET FORMATS

Packet format (v1):
    Offset  Size  Description
    ------  ----  -----------
    0       1     Version (0x01)
    1       1     Flags
                                - bit 0: RSSI present
                                - bit 1: Repeater MAC present
                                - bit 2: Name present
    2       6     Sensor BD address
    8       2     Manufacturer ID (little-endian)
    10      1     Payload length (N)
    11      N     Manufacturer data payload
    11+N    ...   Optional fields (in flag order):
                                - RSSI: 1 byte (int8_t)
                                - Repeater MAC: 6 bytes
                                - Name: 1 byte length + UTF-8 name bytes

Packet format (v2):
    Offset  Size  Description
    ------  ----  -----------
    0       1     Version (0x02)
    1       6     Repeater BD address
    7       6     Advertisement BD address
    13      1     RSSI (int8_t), 0x7F means invalid
    14      ...   Raw BLE advertisement AD structures:
                                - [len][type][data...]
                                - type 0x09: Complete Local Name
                                - type 0xFF: Manufacturer Specific Data
"""

# Manufacturer IDs
MFG_ID_RUUVI = 0x0499
MFG_ID_MOPEKA = 0x0059  # Nordic
MFG_ID_GOBIUS = 0x0F53
MFG_ID_SAFIERY = 0x0067
MFG_ID_SOLARSENSE = 0x02E1
MFG_ID_GARNET = 0x0CC0

# Packet flags
FLAG_RSSI = 0x01
FLAG_REPEATER = 0x02
FLAG_NAME = 0x04


def mac_to_bytes(mac_str):
    """Convert MAC address string to bytes (bdaddr_t order: b[0]..b[5])"""
    parts = mac_str.replace('-', ':').split(':')
    # bdaddr_t stores in reverse order (LSB first)
    return bytes(int(p, 16) for p in reversed(parts))


def build_packet_v1(sensor_mac, mfg_id, payload, rssi=None, gw_mac=None, name=None):
    """Build a V1 UDP packet for the BLE socket listener."""
    flags = 0
    if rssi is not None:
        flags |= FLAG_RSSI
    if gw_mac is not None:
        flags |= FLAG_REPEATER
    if name is not None:
        flags |= FLAG_NAME

    packet = struct.pack('<BB', 1, flags)  # version, flags
    packet += mac_to_bytes(sensor_mac)     # sensor address
    packet += struct.pack('<HB', mfg_id, len(payload))  # mfg_id, payload_len
    packet += payload                       # payload

    if rssi is not None:
        packet += struct.pack('b', rssi)
    if gw_mac is not None:
        packet += mac_to_bytes(gw_mac)
    if name is not None:
        utf8_name = name.encode('utf-8')
        packet += struct.pack('B', len(utf8_name))
        packet += utf8_name
    return packet


def ad_struct(ad_type, ad_data):
    """Build one BLE advertisement AD structure: [len][type][data]."""
    if len(ad_data) > 254:
        raise ValueError("AD structure too long")
    return struct.pack('B', len(ad_data) + 1) + struct.pack('B', ad_type) + ad_data


def build_adv_data(mfg_id, payload, name=None):
    """Build BLE advertisement data with manufacturer data and optional complete name."""
    adv_data = ad_struct(0xFF, struct.pack('<H', mfg_id) + payload)
    if name is not None:
        adv_data += ad_struct(0x09, name.encode('utf-8'))
    return adv_data


def build_packet_v2(sensor_mac, mfg_id, payload, rssi=None, gw_mac=None, name=None):
    """Build a V2 UDP packet for the BLE socket listener."""
    if not gw_mac:
        gw_mac = "00:00:00:00:00:00"

    adv_data = build_adv_data(mfg_id, payload, name=name)

    rssi_byte = 0x7F if rssi is None else struct.pack('b', rssi)[0]

    packet = struct.pack('B', 2)
    packet += mac_to_bytes(gw_mac)
    packet += mac_to_bytes(sensor_mac)
    packet += struct.pack('B', rssi_byte)
    packet += adv_data
    return packet


def build_packet(version, sensor_mac, mfg_id, payload, rssi=None, gw_mac=None, name=None):
    """Build a UDP packet in either V1 or V2 format."""
    if version == 1:
        return build_packet_v1(sensor_mac, mfg_id, payload, rssi=rssi,
                               gw_mac=gw_mac, name=name)
    if version == 2:
        return build_packet_v2(sensor_mac, mfg_id, payload, rssi=rssi,
                               gw_mac=gw_mac, name=name)
    raise ValueError(f"Unsupported packet version: {version}")


def build_ruuvi_rawv2(temp_c, humidity_pct, pressure_hpa, battery_v, seqnr):
    """Build Ruuvi RAWv2 (format 5) payload."""
    # Temperature: value * 200
    temp = int(temp_c * 200)
    # Humidity: value * 400
    hum = int(humidity_pct * 400)
    # Pressure: (value - 500) * 100
    press = int((pressure_hpa - 500) * 100)
    # Battery: (value - 1.6) * 1000, packed with TX power
    batt = int((battery_v - 1.6) * 1000)
    tx_power = 4  # 0 dBm
    batt_txp = (batt << 5) | tx_power

    # Format 5 payload (24 bytes total including format byte)
    payload = struct.pack('>B', 5)           # format
    payload += struct.pack('>h', temp)       # temperature
    payload += struct.pack('>H', hum)        # humidity
    payload += struct.pack('>H', press)      # pressure
    payload += struct.pack('>hhh', 0, 0, 0)  # accel X, Y, Z
    payload += struct.pack('>H', batt_txp)   # battery + tx power
    payload += struct.pack('B', 0)           # movement counter
    payload += struct.pack('>H', seqnr)      # sequence number
    payload += b'\x00' * 6                   # MAC (not used)

    return payload


def build_ruuvi_example(seqnr):
    """Build example Ruuvi sensor reading."""
    payload = build_ruuvi_rawv2(
        temp_c=22.5,
        humidity_pct=45.0,
        pressure_hpa=1013.25,
        battery_v=2.95,
        seqnr=seqnr
    )
    return {
        'sensor_mac': 'AA:BB:CC:DD:EE:01',
        'mfg_id': MFG_ID_RUUVI,
        'payload': payload,
        'rssi': -65,
    }


def build_solarsense(power_w, yield_kwh, irradiance_wm2, cell_temp_c, battery_v, nonce):
    """
    Build SolarSense payload (minimum 22 bytes).

    Packet structure from solarsense.c validation:
    - buf[0] == 0x10
    - buf[4] == 0xff
    - buf[5] == Nonce (8-bit counter for deduplication)
    - buf[7] == 0x01

    Register layout:
    - offset 8:  ErrorCode (4 bytes)
    - offset 12: ChrErrorCode (1 byte, 0xff = invalid)
    - offset 13: InstallationPower (20 bits)
    - offset 15.4: TodaysYield (20 bits, scale 100)
    - offset 18: Irradiance (14 bits, scale 10)
    - offset 19.6: CellTemperature (11 bits, scale 10, bias -60)
    - offset 21.2: BatteryVoltage (8 bits, scale 100, bias 1.7)
    - offset 22.2: TxPowerLevel (1 bit)
    - offset 22.3: TimeSinceLastSun (7 bits)
    """
    payload = bytearray(23)

    # Magic bytes for validation
    payload[0] = 0x10
    payload[4] = 0xff
    payload[5] = nonce & 0xff
    payload[6] = nonce >> 8
    payload[7] = 0x01

    # ErrorCode at offset 8 (4 bytes, little-endian by default in load_int)
    # Set to 0 for no error
    struct.pack_into('<I', payload, 8, 0)

    # ChrErrorCode at offset 12 (0xff = invalid/no error)
    payload[12] = 0xff

    # InstallationPower at offset 13 (20 bits, no scale)
    # Stored as 3 bytes little-endian
    power_raw = min(power_w, 0xfffff)
    payload[13] = power_raw & 0xff
    payload[14] = (power_raw >> 8) & 0xff
    payload[15] = (power_raw >> 16) & 0x0f  # only lower 4 bits

    # TodaysYield at offset 15, shift 4 (20 bits, scale 100)
    # value = raw / 100, so raw = value * 100
    yield_raw = min(int(yield_kwh * 100), 0xfffff)
    payload[15] |= (yield_raw & 0x0f) << 4
    payload[16] = (yield_raw >> 4) & 0xff
    payload[17] = (yield_raw >> 12) & 0xff

    # Irradiance at offset 18 (14 bits, scale 10)
    # value = raw / 10, so raw = value * 10
    irr_raw = min(int(irradiance_wm2 * 10), 0x3fff)
    payload[18] = irr_raw & 0xff
    payload[19] = (irr_raw >> 8) & 0x3f  # only lower 6 bits

    # CellTemperature at offset 19, shift 6 (11 bits, scale 10, bias -60)
    # value = raw / 10 - 60, so raw = (value + 60) * 10
    temp_raw = min(int((cell_temp_c + 60) * 10), 0x7ff)
    payload[19] |= (temp_raw & 0x03) << 6
    payload[20] = (temp_raw >> 2) & 0xff
    payload[21] = (temp_raw >> 10) & 0x01  # only 1 bit

    # BatteryVoltage at offset 21, shift 2 (8 bits, scale 100, bias 1.7)
    # value = raw / 100 + 1.7, so raw = (value - 1.7) * 100
    batt_raw = min(max(int((battery_v - 1.7) * 100), 0), 0xff)
    payload[21] |= (batt_raw & 0x3f) << 2
    payload[22] = (batt_raw >> 6) & 0x03

    # TxPowerLevel at offset 22, shift 2 (1 bit) - set to 0
    # TimeSinceLastSun at offset 22, shift 3 (7 bits) - set to 0
    # Already 0 from initialization

    return bytes(payload)


def build_solarsense_example(seqnr):
    """Build example SolarSense sensor reading."""
    payload = build_solarsense(
        power_w=5000,           # 5kW installation
        yield_kwh=12.5,         # 12.5 kWh today
        irradiance_wm2=850.0,   # 850 W/m2
        cell_temp_c=45.0,       # 45°C cell temperature
        battery_v=3.1,          # 3.1V battery
        nonce=seqnr
    )
    return {
        'sensor_mac': 'AA:BB:CC:DD:EE:02',
        'mfg_id': MFG_ID_SOLARSENSE,
        'payload': payload,
        'rssi': -55,
    }


def build_garnet(fresh1, grey1, black1, fresh2, grey2, black2, galley, lpg, battery_v):
    payload = bytearray(14)

    payload[0] = 0x89
    payload[1] = 0x04
    payload[2] = 0x00

    payload[3] = fresh1 if fresh1 is not None else 110
    payload[4] = grey1 if grey1 is not None else 110
    payload[5] = black1 if black1 is not None else 110
    payload[6] = fresh2 if fresh2 is not None else 110
    payload[7] = grey2 if grey2 is not None else 110
    payload[8] = black2 if black2 is not None else 110
    payload[9] = galley if galley is not None else 110
    payload[10] = lpg if lpg is not None else 110

    payload[11] = min(max(int(battery_v * 10), 0), 0xff)

    payload[12] = 0x00  # reserved
    payload[13] = 0x00  # reserved
    return bytes(payload)

def build_garnet_soul_example(_seqnr):
    """Build example Garnet Soul sensor reading."""
    payload = build_garnet(
        fresh1=22.5,
        grey1=45.0,
        black1=None,
        fresh2=None,
        grey2=None,
        black2=None,
        galley=None,
        lpg=None,
        battery_v=13.6
    )
    return {
        'sensor_mac': 'AA:BB:CC:DD:EE:01',
        'mfg_id': MFG_ID_GARNET,
        'payload': payload,
        'rssi': -65,
        'name': 'Soul',
    }


def send_example(sock, addr, transport, post_url, packet_version, gw_mac,
                 http_poster, example, name_override=None):
    """Send one built example over either UDP or HTTP(S) POST."""
    sensor_mac = example['sensor_mac']
    mfg_id = example['mfg_id']
    payload = example['payload']
    rssi = example.get('rssi')
    name = name_override if name_override is not None else example.get('name')

    print(f"Payload length: {len(payload)} bytes")
    print(f"Payload hex: {payload.hex()}")

    if transport == 'udp':
        packet = build_packet(packet_version, sensor_mac, mfg_id, payload,
                              rssi=rssi, gw_mac=gw_mac, name=name)
        print(f"Total packet length: {len(packet)} bytes")
        print(f"Packet hex: {packet.hex()}")
        try:
            sock.sendto(packet, addr)
        except OSError as e:
            print(f"Error: Failed to send UDP packet to {addr}: {e}", file=sys.stderr)
            sys.exit(1)
        print(f"Sent data from {sensor_mac}")
        return

    send_post_ble_gw(http_poster, post_url, sensor_mac, mfg_id, payload,
                     rssi=rssi, gw_mac=gw_mac, name=name)
    print(f"Posted data from {sensor_mac}")


def send_raw(sock, addr, packet_version, sensor_mac, mfg_id, payload_hex,
             rssi=None, gw_mac=None, name=None):
    """Send raw payload."""
    payload = bytes.fromhex(payload_hex)
    packet = build_packet(packet_version, sensor_mac, mfg_id, payload,
                          rssi=rssi, gw_mac=gw_mac, name=name)
    try:
        sock.sendto(packet, addr)
    except OSError as e:
        print(f"Error: Failed to send UDP packet to {addr}: {e}", file=sys.stderr)
        sys.exit(1)
    print(f"Sent raw data from {sensor_mac}, mfg_id=0x{mfg_id:04x}, "
          f"payload={payload_hex}")


class HttpPoster:
    """HTTP(S) poster with optional token-based Basic Auth."""

    def __init__(self, host, insecure_tls=False, timeout=3.0, token=None):
        self.host = host
        self.insecure_tls = insecure_tls
        self.timeout = timeout
        self.token = token

        handlers = []
        if insecure_tls:
            context = ssl._create_unverified_context()
            handlers.append(urllib.request.HTTPSHandler(context=context))

        self.opener = urllib.request.build_opener(*handlers)

    def post_json(self, url, msg, auth=None):
        body = json.dumps(msg).encode('utf-8')
        """POST JSON to url. auth can be 'user:password' for Basic Auth override."""
        headers = {'Content-Type': 'application/json; charset=utf-8'}

        # Use explicit auth if provided, otherwise fall back to token
        credentials_str = auth if auth is not None else self.token
        if credentials_str is not None:
            parts = credentials_str.split(':', 1)
            if len(parts) != 2:
                raise RuntimeError('Invalid auth format, expected user:password')
            credentials = f"{parts[0]}:{parts[1]}".encode()
            headers['Authorization'] = 'Basic ' + base64.b64encode(credentials).decode('ascii')

        req = urllib.request.Request(
            url,
            data=body,
            headers=headers,
            method='POST'
        )

        with self.opener.open(req, timeout=self.timeout) as resp:
            final_url = resp.geturl()
            resp_body = resp.read().decode('utf-8', errors='replace')
            return resp.status, final_url, resp_body


def create_token(host, device_id, password=None, insecure_tls=False, timeout=3.0):
    """Create a new token via the /ble-gw/ endpoint."""
    url = f"https://{host}/ble-gw/"
    msg = {"generate_token": {"device_id": device_id}}

    poster = HttpPoster(host, insecure_tls=insecure_tls, timeout=timeout)
    auth = f"remoteconsole:{password}" if password else None

    print(f"Creating token for device_id '{device_id}'...")
    if not password:
        print("(no password provided, assuming GX is in pairing mode)")
    print("POST JSON:")
    print(json.dumps(msg, separators=(',', ':'), sort_keys=True))
    print(f"POST {url}");

    try:
        status, final_url, resp_body = poster.post_json(url, msg, auth=auth)
        print(f"-> HTTP {status}")
    except urllib.error.HTTPError as e:
        print(f"-> Error: HTTP {e.code} - {e.reason}", file=sys.stderr)
        sys.exit(1)
    except urllib.error.URLError as e:
        print(f"-> Error: {e.reason}", file=sys.stderr)
        sys.exit(1)

    try:
        resp_json = json.loads(resp_body)
        token_name = resp_json.get('token_name')
        token_password = resp_json.get('password')
        if token_name and token_password:
            print(f"\nToken created successfully!")
            print(f"  token_name: {token_name}")
            print(f"  password:   {token_password}")
            print(f"\nUse with: --token '{token_name}:{token_password}'")
        else:
            print(f"Response: {resp_body}")
    except json.JSONDecodeError:
        print(f"Response: {resp_body}")


def send_post_ble_gw(http_poster, url, sensor_mac, mfg_id, payload, rssi=None,
                     gw_mac=None, name=None):
    """Send one BLE reading to a GX /ble-gw/ endpoint via HTTP(S) POST."""
    adv_data = build_adv_data(mfg_id, payload, name=name)

    msg = {
        "data": {
            "tags": {
                sensor_mac: {
                    "data": adv_data.hex(),
                }
            }
        }
    }

    if rssi is not None:
        msg["data"]["tags"][sensor_mac]["rssi"] = int(rssi)
    if gw_mac:
        msg["data"]["gw_mac"] = gw_mac

    print("POST JSON:")
    print(json.dumps(msg, separators=(',', ':'), sort_keys=True))
    print(f"POST {url}")

    try:
        status, final_url, resp_body = http_poster.post_json(url, msg)
        print(f"-> HTTP {status}")
        if final_url != url:
            raise RuntimeError(f'Unexpected redirect to {final_url}')
        print(f"Response: {resp_body}")
    except urllib.error.HTTPError as e:
        print(f"-> Error: HTTP {e.code} - {e.reason}", file=sys.stderr)
        sys.exit(1)
    except urllib.error.URLError as e:
        print(f"-> Error: {e.reason}", file=sys.stderr)
        sys.exit(1)


def send_post_raw(http_poster, url, sensor_mac, mfg_id, payload_hex,
                  rssi=None, gw_mac=None, name=None):
    """Send raw manufacturer payload as POST to /ble-gw/."""
    payload = bytes.fromhex(payload_hex)
    send_post_ble_gw(http_poster, url, sensor_mac, mfg_id, payload,
                     rssi=rssi, gw_mac=gw_mac, name=name)
    print(f"Posted raw data from {sensor_mac}, mfg_id=0x{mfg_id:04x}, payload={payload_hex}")


def main():
    parser = argparse.ArgumentParser(description='Send BLE data via UDP')
    parser.add_argument('--host', default='127.0.0.1',
                        help='Target host (UDP socket host or GX host for POST, default: 127.0.0.1)')
    parser.add_argument('--port', type=int, default=18542,
                        help='Target port (default: 18542)')
    parser.add_argument('--transport', choices=['udp', 'http', 'https'], default='udp',
                        help='Output transport (default: udp)')
    # HTTP/HTTPS POST options
    parser.add_argument('--post-timeout', type=float, default=3.0,
                        help='HTTP(S) POST timeout in seconds (default: 3.0)')
    parser.add_argument('--post-insecure', action='store_true',
                        help='Disable TLS certificate verification for HTTPS POST')
    parser.add_argument('--password', default=None,
                        help='GX remoteconsole password (optional with --create-token if GX is in pairing mode)')
    parser.add_argument('--token', default=None,
                        help='GX token for auth in the form token_name:password')
    parser.add_argument('--create-token', metavar='DEVICE_ID', default=None,
                        help='Create a new token with given device_id and exit')
    # UDP packet options
    parser.add_argument('--packet-version', type=int, choices=[1, 2], default=2,
                        help='UDP socket packet version to emit (default: 2)')
    # Sensor data options
    parser.add_argument('--mac', default='AA:BB:CC:DD:EE:01',
                        help='Sensor MAC address')
    parser.add_argument('--mfg-id', type=lambda x: int(x, 0),
                        help='Manufacturer ID (hex or decimal)')
    parser.add_argument('--mfg-data', help='Manufacturer data as hex string')
    parser.add_argument('--name', type=str, default=None, help='Device name')
    parser.add_argument('--example', choices=['ruuvi', 'solarsense', 'garnet_soul'],
                        help='Send example data')
    parser.add_argument('--rssi', type=int, help='RSSI value (-127 to 126)')
    parser.add_argument('--gw-mac', default=None, help='Gateway MAC address')
    parser.add_argument('--seqnr', type=int, default=0, help='Sequence number')
    parser.add_argument('--repeat', type=int, default=1,
                        help='Number of times to send the packet (default: 1)')
    parser.add_argument('--seqnr_repeat', type=int, default=None,
                        help='Number of repeats of the same sequence number')
    parser.add_argument('--interval', type=float, default=None,
                        help='Interval between packets in seconds')

    args = parser.parse_args()

    # Handle --create-token action
    if args.create_token is not None:
        create_token(
            host=args.host,
            device_id=args.create_token,
            password=args.password,
            insecure_tls=args.post_insecure,
            timeout=args.post_timeout,
        )
        return

    if args.rssi is not None and not (-127 <= args.rssi <= 126):
        parser.error(f"RSSI must be between -127 and 126, got {args.rssi}")

    sock = None
    addr = None
    post_url = None
    http_poster = None
    if args.transport == 'udp':
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        addr = (args.host, args.port)
    else:
        post_url = f"{args.transport}://{args.host}/ble-gw/"
        http_poster = HttpPoster(
            host=args.host,
            insecure_tls=args.post_insecure,
            timeout=args.post_timeout,
            token=args.token,
        )

    name = args.name

    if args.example == 'ruuvi':
        interval = args.interval if args.interval is not None else 1.285
        seqnr_repeat = args.seqnr_repeat if args.seqnr_repeat is not None else 1
        def f(s):
            send_example(sock, addr, args.transport, post_url, args.packet_version,
                         args.gw_mac, http_poster,
                         build_ruuvi_example(s), name_override=name)
    elif args.example == 'solarsense':
        interval = args.interval if args.interval is not None else 0.16
        seqnr_repeat = args.seqnr_repeat if args.seqnr_repeat is not None else 7
        def f(s):
            send_example(sock, addr, args.transport, post_url, args.packet_version,
                         args.gw_mac, http_poster,
                         build_solarsense_example(s), name_override=name)
    elif args.example == 'garnet_soul':
        interval = args.interval if args.interval is not None else 0.125
        seqnr_repeat = args.seqnr_repeat if args.seqnr_repeat is not None else 1
        def f(s):
            send_example(sock, addr, args.transport, post_url, args.packet_version,
                         args.gw_mac, http_poster,
                         build_garnet_soul_example(s), name_override=name)
    elif args.mfg_id and args.mfg_data:
        interval = args.interval if args.interval is not None else 1.0
        seqnr_repeat = args.seqnr_repeat if args.seqnr_repeat is not None else 1
        if args.transport == 'udp':
            def f(s):
                send_raw(sock, addr, args.packet_version, args.mac, args.mfg_id,
                         args.mfg_data, rssi=args.rssi, gw_mac=args.gw_mac,
                         name=name)
        else:
            def f(s):
                send_post_raw(http_poster, post_url, args.mac, args.mfg_id, args.mfg_data,
                              rssi=args.rssi, gw_mac=args.gw_mac,
                              name=name)
    else:
        parser.print_help()
        print("\nExamples:")
        print("  Send Ruuvi example:")
        print("    ./ble-socket-test.py --example ruuvi")
        print("  Send Ruuvi example as V2 packet:")
        print("    ./ble-socket-test.py --example ruuvi --packet-version 2")
        print("  Post Ruuvi example to GX over HTTP:")
        print("    ./ble-socket-test.py  --example ruuvi --transport http --host 192.168.1.50")
        print("  Create a token for use with --token:")
        print("    ./ble-socket-test.py --transport https --host 192.168.1.50 --post-insecure --create-token my_device --password the_gx_password")
        print("  Post Ruuvi example to GX over HTTPS with token based authentication:")
        print("    ./ble-socket-test.py --transport https --host 192.168.1.50 --post-insecure --token token_name:token_password --example ruuvi")
        print("  Send SolarSense example:")
        print("    ./ble-socket-test.py --example solarsense --repeat 200")
        print("  Send raw Ruuvi format 5:")
        print("    ./ble-socket-test.py --mfg-id 0x0499 --mfg-data '0500112233...'")
        return

    try:
        for i in range(args.repeat):
            seqnr = args.seqnr + (i // seqnr_repeat)
            f(seqnr)
            if interval > 0 and i < args.repeat - 1:
                time.sleep(interval)
    finally:
        if sock:
            sock.close()

if __name__ == '__main__':
    main()
