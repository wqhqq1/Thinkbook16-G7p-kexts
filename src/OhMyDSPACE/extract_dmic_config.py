"""Extract the ThinkBook's 48 kHz stereo PDM configuration from an NHLT dump."""

import argparse
from hashlib import sha256
import json
from pathlib import Path
from struct import Struct

ACPI_HEADER_SIZE = 36
ENDPOINT = Struct('<IBBHHHIBBB')
WAVE = Struct('<HHIIHHHHI16s')
LENGTH = Struct('<I')
PCM = bytes.fromhex('0100000000001000800000aa00389b71')


def sized_block(data, offset):
    if offset + LENGTH.size > len(data):
        raise ValueError('Missing configuration length')
    start = offset + LENGTH.size
    end = start + LENGTH.unpack_from(data, offset)[0]
    if end > len(data):
        raise ValueError('Truncated configuration')
    return data[start:end], end


def extract(table):
    if (len(table) <= ACPI_HEADER_SIZE or table[:4] != b'NHLT' or
            LENGTH.unpack_from(table, 4)[0] != len(table) or sum(table) % 256):
        raise ValueError('Invalid NHLT header, length or checksum')

    matches = []
    offset = ACPI_HEADER_SIZE + 1
    for _ in range(table[ACPI_HEADER_SIZE]):
        if offset + ENDPOINT.size > len(table):
            raise ValueError('Truncated endpoint header')
        length, link, instance, vendor, device, _, _, _, direction, bus = ENDPOINT.unpack_from(table, offset)
        end = offset + length
        if length < ENDPOINT.size or end > len(table):
            raise ValueError('Invalid NHLT endpoint length')
        endpoint = table[offset:end]
        offset = end
        if (link, vendor, device, direction, bus) != (2, 0x8086, 0xae20, 1, 0):
            continue

        device_config, cursor = sized_block(endpoint, ENDPOINT.size)
        if cursor == len(endpoint):
            raise ValueError('Missing PDM format count')
        count = endpoint[cursor]
        cursor += 1
        for _ in range(count):
            if cursor + WAVE.size > len(endpoint):
                raise ValueError('Truncated PDM format')
            tag, channels, rate, byte_rate, align, bits, ext_size, valid, mask, guid = WAVE.unpack_from(endpoint, cursor)
            payload, cursor = sized_block(endpoint, cursor + WAVE.size)
            if (tag, channels, rate, byte_rate, align, bits, ext_size, valid, mask, guid) == (
                0xfffe, 2, 48000, 384000, 8, 32, 22, 24, 3, PCM
            ) and payload:
                matches.append((payload, {
                    'instance': instance,
                    'virtual_bus': bus,
                    'channels': channels,
                    'sample_rate': rate,
                    'container_bits': bits,
                    'valid_bits': valid,
                    'channel_mask': mask,
                    'device_config_hex': device_config.hex(),
                    'format_config_size': len(payload),
                    'format_config_sha256': sha256(payload).hexdigest(),
                    'nhlt_sha256': sha256(table).hexdigest(),
                }))

    if len(matches) != 1:
        raise ValueError(f'Expected one matching PDM capture format, found {len(matches)}')
    return matches[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('nhlt', type=Path)
    parser.add_argument('--payload', type=Path, help='Write the opaque format capability data to a new file')
    args = parser.parse_args()
    try:
        payload, profile = extract(args.nhlt.read_bytes())
        if args.payload:
            with args.payload.open('xb') as output:
                output.write(payload)
    except (ValueError, OSError) as error:
        parser.exit(1, f'{error}\n')
    print(json.dumps(profile, indent=2))


if __name__ == '__main__':
    main()
