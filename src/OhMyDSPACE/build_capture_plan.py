"""Build IPC4 copier payloads for the ThinkBook's stereo DMIC from pinned SOF data."""

import argparse
import json
import struct
from hashlib import sha256
from pathlib import Path

from extract_dmic_config import extract

FIRMWARE_SHA = '4f3fb3fc57d2a2c90216d2a6195e24be54921c099395c9d2d019a14bbdc09250'
TOPOLOGY_SHA = '6d1313cf7311dcc47ccb78a05841f25da9640c4e87cb04a346ddf56f43f13f7b'
NHLT_SHA = '5357d472aa0b4a8e3474ffc39d6c99cb8dff0f455681c48c5f7e7668033f3ed8'
DMIC = 'dai-copier.DMIC.dmic01.capture'
HOST = 'host-copier.6.capture'
PLAY_HOST = 'host-copier.0.playback'
PLAY_LINK = 'dai-copier.HDA.Analog.playback'


def u32(blob, offset):
    return struct.unpack_from('<I', blob, offset)[0]


def string_at(blob, offset, length):
    return blob[offset:offset + length].split(b'\0', 1)[0].decode('ascii')


def widgets(topology, expected):
    """Read the selected gateway widgets from the pinned ALSA ASoC v5 file."""
    found = {}
    offset = 0
    while offset < len(topology):
        magic, abi, _, kind, header, _, payload, _, count = struct.unpack_from('<9I', topology, offset)
        end = offset + header + payload
        if (magic, abi, header) != (0x41536f43, 5, 36) or end > len(topology):
            raise ValueError('Invalid ASoC block')
        if kind == 5:
            cursor = offset + header
            for _ in range(count):
                size, widget_type = struct.unpack_from('<II', topology, cursor)
                name = string_at(topology, cursor + 8, 44)
                controls, private_size = struct.unpack_from('<II', topology, cursor + size - 8)
                cursor += size
                if name in expected:
                    if name in found or widget_type != expected[name]:
                        raise ValueError('Unexpected gateway widget')
                    found[name] = tuples(topology[cursor:cursor + private_size])
                cursor += private_size
                for _ in range(controls):
                    control_size = u32(topology, cursor + u32(topology, cursor))
                    cursor += control_size + u32(topology, cursor + control_size - 4)
            if cursor != end:
                raise ValueError('Unexpected widget block length')
        offset = end
    if set(found) != set(expected):
        raise ValueError('Missing topology gateways')
    return found


def tuples(private):
    groups = []
    cursor = 0
    while cursor < len(private):
        size, kind, count = struct.unpack_from('<III', private, cursor)
        width = {0: 20, 1: 48, 2: 8, 3: 8, 4: 8, 5: 8}[kind]
        if size != 12 + width * count or cursor + size > len(private):
            raise ValueError('Invalid SOF vendor tuple length')
        values = {}
        for offset in range(cursor + 12, cursor + size, width):
            token = u32(private, offset)
            if kind == 0:
                value = private[offset + 4:offset + width]
            elif kind == 1:
                value = string_at(private, offset + 4, width - 4)
            else:
                value = u32(private, offset + 4)
            values[token] = value
        groups.append(values)
        cursor += size
    return groups


def token(groups, key):
    return next(group[key] for group in groups if key in group)


def pin(groups, start, size_token, expected_valid_bits, container_bits=32):
    """Select the 48 kHz stereo pin and preserve its on-wire format word."""
    for values in groups:
        if (values.get(start), values.get(start + 1), values.get(start + 2), values.get(start + 3)) != (
            48000, container_bits, expected_valid_bits, 2
        ):
            continue
        rate, bits, channel_map, channel_config, interleaving, format_config = (
            values[start + offset] for offset in (0, 1, 4, 5, 6, 7)
        )
        if (format_config & 0xffff) != ((expected_valid_bits << 8) | 2):
            raise ValueError('Unexpected format encoding')
        return (rate, bits, channel_map, channel_config, interleaving, format_config), values[size_token]
    raise ValueError('Requested pin format was not found')


def copier_manifest(image, expected_uuid, ibs, obs):
    fw_header = u32(image, 4) + 0x2000
    if image[fw_header:fw_header + 4] != b'$AM1':
        raise ValueError('Expected IPC4 firmware manifest')
    module_table = fw_header + u32(image, fw_header + 4)
    count = u32(image, fw_header + 36)
    configs = module_table + count * 116
    for index in range(count):
        row = module_table + index * 116
        if image[row:row + 4] != b'$AME':
            raise ValueError('Invalid IPC4 module descriptor')
        if image[row + 12:row + 28] == expected_uuid:
            if string_at(image, row + 4, 8) != 'COPIER':
                raise ValueError('Unexpected firmware module for gateway UUID')
            start, length = struct.unpack_from('<HH', image, row + 68)
            cfgs = [struct.unpack_from('<11I', image, configs + n * 44) for n in range(start, start + length)]
            cpc = max((config[9] for config in cfgs if config[6:8] == (ibs, obs)), default=0)
            if not cpc:
                raise ValueError('No firmware CPC for this pin size')
            return index, cpc, u32(image, row + 28)
    raise ValueError('COPIER module missing from base firmware')


def build(firmware, topology, nhlt, host_tag):
    for name, data, expected in (
        ('firmware', firmware, FIRMWARE_SHA), ('topology', topology, TOPOLOGY_SHA), ('NHLT', nhlt, NHLT_SHA)
    ):
        if sha256(data).hexdigest() != expected:
            raise ValueError(f'Unexpected {name} SHA-256')
    if not 1 <= host_tag <= 10:
        raise ValueError('The capture tag must be in the range 1 to 10')
    pdm_blob, nhlt_profile = extract(nhlt)
    groups = widgets(topology, {DMIC: 14, HOST: 12})
    dm = groups[DMIC]
    host = groups[HOST]
    copier_uuid = token(dm, 405)
    if copier_uuid != token(host, 405) or token(dm, 155) != nhlt_profile['virtual_bus']:
        raise ValueError('Gateway module or NHLT bus mismatch')

    d_in, d_ibs = pin(dm, 1900, 1970, 24)
    d_out, d_obs = pin(dm, 1930, 1971, 32)
    h_in, h_ibs = pin(host, 1900, 1970, 32)
    h_out, h_obs = pin(host, 1930, 1971, 32)
    if d_out != h_in or d_out != h_out or d_obs != h_ibs:
        raise ValueError('The gateway formats do not match')

    module, cpc, module_type = copier_manifest(firmware, copier_uuid, d_ibs, d_obs)
    host_module, host_cpc, host_type = copier_manifest(firmware, copier_uuid, h_ibs, h_obs)
    if module != 5 or host_module != module or host_type != module_type or not (module_type & 0x20):
        raise ValueError('The capture trial requires base-firmware LL copier module 5')

    config = {}
    for name, widget, pcm_in, pcm_out, ibs, obs, cycles, node_index, private in (
        ('dmic', dm, d_in, d_out, d_ibs, d_obs, cpc, token(dm, 155), pdm_blob),
        ('host', host, h_in, h_out, h_ibs, h_obs, host_cpc, host_tag - 1, bytes(4)),
    ):
        node = token(widget, 1980) << 8 | node_index
        dma_bytes = 4 * obs  # capture gateway uses four 1 ms periods
        instance_pages = token(widget, 409)
        words = (cycles, ibs, obs, instance_pages, *pcm_in, *pcm_out, 0, node, dma_bytes, len(private) // 4)
        payload = struct.pack('<20I', *words) + private
        if len(private) % 4 or len(payload) > 4096:
            raise ValueError('Gateway configuration does not fit in an IPC4 mailbox')
        # SOF IPC4 LL task accounting: pipeline object, instance, rounded task,
        # queue list entries, task list entry, and eight rounded data queues.
        task = 448 + 12 + instance_pages + 128 + 8 * 12 + 12
        queues = 8 * (128 + ((ibs + 63) & ~63))
        config[name] = {
            'node_id': node, 'cpc': cycles, 'input_format_words': pcm_in,
            'output_format_words': pcm_out, 'input_buffer_bytes': ibs,
            'output_buffer_bytes': obs, 'gateway_dma_bytes': dma_bytes,
            'gateway_config_dwords': len(private) // 4, 'payload': payload,
            'pipeline_pages': (task + queues + 4095) // 4096,
        }
    if sum(g['pipeline_pages'] for g in config.values()) != 4:
        raise ValueError('The capture pipeline requires four IPC4 pages')
    return module, config


def playback_profiles(firmware, topology):
    groups = widgets(topology, {PLAY_HOST: 11, PLAY_LINK: 13})
    host, link = groups[PLAY_HOST], groups[PLAY_LINK]
    if (token(host, 1980), token(link, 1980)) != (0, 8):
        raise ValueError('Unexpected analog playback gateway types')
    host_output, host_obs = pin(host, 1930, 1971, 32)
    link_input, link_ibs = pin(link, 1900, 1970, 32)
    link_output, link_obs = pin(link, 1930, 1971, 32)
    if (host_output, host_obs) != (link_input, link_ibs):
        raise ValueError('The analog playback pins do not match')

    def payload(widget, pcm_in, pcm_out, ibs, obs, dma_bytes):
        module, cpc, module_type = copier_manifest(firmware, token(widget, 405), ibs, obs)
        if module != 5 or not (module_type & 0x20):
            raise ValueError('Analog playback requires base-firmware LL copier module 5')
        return struct.pack('<21I', cpc, ibs, obs, token(widget, 409), *pcm_in, *pcm_out,
                           0, token(widget, 1980) << 8, dma_bytes, 1, 0)

    # The topology uses a full-width DSP/DAI format. The link descriptor and
    # codec still carry the format negotiated by AppleHDA for the HDA wire.
    dai_payload = payload(link, link_input, link_output, link_ibs, link_obs, 2 * link_obs)
    profiles = []
    for hda_format, bits, container in ((0x0011, 16, 16), (0x0031, 24, 32), (0x0041, 32, 32)):
        host_input, host_ibs = pin(host, 1900, 1970, bits, container)
        if bits == 24:
            # Intel HDA §4.5.1: the DMA buffer places the 24 sample bits at
            # the top of each dword. Select IPC4 MSB_INTEGER (0) at this
            # gateway so the converter doesn't interpret the low 24 bits.
            format_config = host_input[-1]
            if (format_config >> 16) & 0xff != 1:
                raise ValueError('Unexpected SOF integer sample type')
            host_input = (*host_input[:-1], format_config & ~(0xff << 16))
        host_payload = payload(host, host_input, host_output, host_ibs, host_obs, 2 * host_ibs)
        profiles.append((hda_format, host_payload, dai_payload))
    return profiles


def write_playback_header(path, profiles):
    with path.open('x') as output:
        output.write('// Generated from pinned SOF firmware and topology.\n')
        output.write('static constexpr ace::PlaybackProfile kAnalogProfiles[] = {\n')
        for hda_format, host, link in profiles:
            output.write(f'    {{0x{hda_format:04x}, {{\n')
            for idx, payload in enumerate((host, link)):
                for i in range(0, len(payload), 12):
                    output.write('        ' + ', '.join(f'0x{byte:02x}' for byte in payload[i:i + 12]) + ',\n')
                output.write('    }, {\n' if idx == 0 else '    }},\n')
        output.write('};\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('firmware', type=Path)
    parser.add_argument('topology', type=Path)
    parser.add_argument('nhlt', type=Path)
    parser.add_argument('--host-tag', type=int, required=True)
    parser.add_argument('--payload-dir', type=Path)
    args = parser.parse_args()
    try:
        firmware, topology = args.firmware.read_bytes(), args.topology.read_bytes()
        module, config = build(firmware, topology, args.nhlt.read_bytes(), args.host_tag)
        profiles = playback_profiles(firmware, topology)
        if args.payload_dir and any((args.payload_dir / f'{name}-copier.bin').exists() for name in config):
            raise ValueError('Payload directory already contains copier files')
        for name, gateway in config.items():
            payload = gateway.pop('payload')
            gateway['payload_bytes'] = len(payload)
            gateway['payload_sha256'] = sha256(payload).hexdigest()
            if args.payload_dir:
                with (args.payload_dir / f'{name}-copier.bin').open('xb') as output:
                    output.write(payload)
        if args.payload_dir:
            write_playback_header(args.payload_dir / 'AcePlaybackProfiles.inc', profiles)
    except (OSError, ValueError, struct.error, StopIteration) as error:
        parser.exit(1, f'{error}\n')
    print(json.dumps({
        'copier_module_id': module, 'host_tag_example': args.host_tag,
        'sample_rate_hz': 48000, 'channels': 2, 'hda_stream_format': 0x41,
        'trial_pipeline_pages': sum(g['pipeline_pages'] for g in config.values()), 'gateway': config,
        'analog_playback_profiles': [f'0x{fmt:04x}' for fmt, _, _ in profiles],
    }, indent=2))


if __name__ == '__main__':
    main()
