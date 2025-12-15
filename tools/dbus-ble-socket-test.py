#!/usr/bin/env python3
"""
Test script for sending BLE sensor data via UDP socket.

Packet format (v1):
  Offset  Size  Description
  ------  ----  -----------
  0       1     Version (0x01)
  1       1     Flags
                - bit 0: RSSI present
                - bit 1: Repeater MAC present
  2       6     Sensor BD address
  8       2     Manufacturer ID (little-endian)
  10      1     Payload length (N)
  11      N     Manufacturer data payload
  11+N    ...   Optional fields (in flag order):
                - RSSI: 1 byte (int8_t)
                - Repeater MAC: 6 bytes
"""

import socket
import struct
import argparse

# Manufacturer IDs
MFG_ID_RUUVI = 0x0499
MFG_ID_MOPEKA = 0x0059  # Nordic
MFG_ID_GOBIUS = 0x0F53
MFG_ID_SAFIERY = 0x0067
MFG_ID_SOLARSENSE = 0x02E1

# Packet flags
FLAG_RSSI = 0x01
FLAG_REPEATER = 0x02


def mac_to_bytes(mac_str):
    """Convert MAC address string to bytes (bdaddr_t order: b[0]..b[5])"""
    parts = mac_str.replace('-', ':').split(':')
    # bdaddr_t stores in reverse order (LSB first)
    return bytes(int(p, 16) for p in reversed(parts))


def build_packet(sensor_mac, mfg_id, payload, rssi=None, repeater_mac=None):
    """Build a UDP packet for the BLE socket listener."""
    flags = 0
    if rssi is not None:
        flags |= FLAG_RSSI
    if repeater_mac is not None:
        flags |= FLAG_REPEATER

    packet = struct.pack('<BB', 1, flags)  # version, flags
    packet += mac_to_bytes(sensor_mac)     # sensor address
    packet += struct.pack('<HB', mfg_id, len(payload))  # mfg_id, payload_len
    packet += payload                       # payload

    if rssi is not None:
        packet += struct.pack('b', rssi)
    if repeater_mac is not None:
        packet += mac_to_bytes(repeater_mac)

    return packet


def build_ruuvi_rawv2(temp_c, humidity_pct, pressure_hpa, battery_v):
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
    payload += struct.pack('>H', 0)          # sequence number
    payload += b'\x00' * 6                   # MAC (not used)

    return payload


def send_ruuvi_example(sock, addr):
    """Send example Ruuvi sensor data."""
    sensor_mac = "AA:BB:CC:DD:EE:01"
    payload = build_ruuvi_rawv2(
        temp_c=22.5,
        humidity_pct=45.0,
        pressure_hpa=1013.25,
        battery_v=2.95
    )
    print(f"Payload length: {len(payload)} bytes")
    print(f"Payload hex: {payload.hex()}")
    packet = build_packet(sensor_mac, MFG_ID_RUUVI, payload, rssi=-65)
    print(f"Total packet length: {len(packet)} bytes")
    print(f"Packet hex: {packet.hex()}")
    sock.sendto(packet, addr)
    print(f"Sent Ruuvi data from {sensor_mac}")


def build_solarsense(power_w, yield_kwh, irradiance_wm2, cell_temp_c, battery_v, nonce=0):
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


def send_solarsense_example(sock, addr):
    """Send example SolarSense sensor data."""
    sensor_mac = "AA:BB:CC:DD:EE:02"
    payload = build_solarsense(
        power_w=5000,           # 5kW installation
        yield_kwh=12.5,         # 12.5 kWh today
        irradiance_wm2=850.0,   # 850 W/m2
        cell_temp_c=45.0,       # 45°C cell temperature
        battery_v=3.1           # 3.1V battery
    )
    print(f"Payload length: {len(payload)} bytes")
    print(f"Payload hex: {payload.hex()}")
    packet = build_packet(sensor_mac, MFG_ID_SOLARSENSE, payload, rssi=-55)
    print(f"Total packet length: {len(packet)} bytes")
    print(f"Packet hex: {packet.hex()}")
    sock.sendto(packet, addr)
    print(f"Sent SolarSense data from {sensor_mac}")


def send_raw(sock, addr, sensor_mac, mfg_id, payload_hex, rssi=None):
    """Send raw payload."""
    payload = bytes.fromhex(payload_hex)
    packet = build_packet(sensor_mac, mfg_id, payload, rssi=rssi)
    sock.sendto(packet, addr)
    print(f"Sent raw data from {sensor_mac}, mfg_id=0x{mfg_id:04x}, "
          f"payload={payload_hex}")


def main():
    parser = argparse.ArgumentParser(description='Send BLE data via UDP')
    parser.add_argument('--host', default='127.0.0.1',
                        help='Target host (default: 127.0.0.1)')
    parser.add_argument('--port', type=int, default=18542,
                        help='Target port (default: 18542)')
    parser.add_argument('--mac', default='AA:BB:CC:DD:EE:01',
                        help='Sensor MAC address')
    parser.add_argument('--mfg-id', type=lambda x: int(x, 0),
                        help='Manufacturer ID (hex or decimal)')
    parser.add_argument('--payload', help='Payload as hex string')
    parser.add_argument('--rssi', type=int, help='RSSI value (-127 to 0)')
    parser.add_argument('--example', choices=['ruuvi', 'solarsense'],
                        help='Send example data')

    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    addr = (args.host, args.port)

    if args.example == 'ruuvi':
        send_ruuvi_example(sock, addr)
    elif args.example == 'solarsense':
        send_solarsense_example(sock, addr)
    elif args.mfg_id and args.payload:
        send_raw(sock, addr, args.mac, args.mfg_id, args.payload,
                 rssi=args.rssi)
    else:
        parser.print_help()
        print("\nExamples:")
        print("  Send Ruuvi example:")
        print("    ./ble-socket-test.py --example ruuvi")
        print("  Send SolarSense example:")
        print("    ./ble-socket-test.py --example solarsense")
        print("  Send raw Ruuvi format 5:")
        print("    ./ble-socket-test.py --mfg-id 0x0499 "
              "--payload '0500112233...'")


if __name__ == '__main__':
    main()
