#!/usr/bin/env python3
"""Exercise the live ACE ring and play a short acoustic reference during capture."""
import json
from pathlib import Path
import plistlib
import shlex
import struct
import subprocess
import time

KIT = Path(__file__).resolve().parent


def state():
    nodes = plistlib.loads(subprocess.check_output(['/usr/sbin/ioreg', '-a', '-r', '-c', 'OhMyDSPACE']))
    return next((node for node in nodes if node.get('DriverVersion') == '0.3.3'), {})


def run():
    before = state()
    if not before.get('StartupPassive') or before.get('ACEBlockedUntilReboot'):
        raise SystemExit('Need an unblocked OhMyDSP ACE 0.3.3 service')
    if before.get('ACEState') == 'capturing':
        raise SystemExit('Another client is already capturing')
    if before.get('ACEFirmwareResident'):
        raise SystemExit('Wait for the previous capture to release the DSP')
    player = str(KIT / 'build/ace-speaker-tone')
    rate_command = [player, '--nominal-rate']
    original = subprocess.check_output(rate_command, text=True, timeout=5).strip()
    if original == '48000':
        return capture(player)
    changed = False
    try:
        subprocess.run([*rate_command, '48000'], timeout=5, check=True)
        changed = True
        for _ in range(20):
            if subprocess.check_output(rate_command, text=True, timeout=5).strip() == '48000':
                break
            time.sleep(0.15)
        else:
            raise RuntimeError('Built-in output did not switch to 48 kHz')
        print('Built-in output set to 48 kHz for the analog playback trial.')
        return capture(player)
    finally:
        if changed:
            for _ in range(20):
                if subprocess.run([*rate_command, original], timeout=5, capture_output=True).returncode == 0:
                    print(f'Built-in output sample rate restored to {original} Hz.')
                    break
                time.sleep(0.25)
            else:
                print(f'Could not restore the built-in output sample rate to {original} Hz.')


def capture(player):
    subprocess.run([player, '0.4'], timeout=5, check=True)
    time.sleep(2.5)
    command = shlex.join(['/usr/bin/caffeinate', '-i', str(KIT / 'build/ace-test'), '6'])
    script = 'do shell script ' + json.dumps(command) + ' with administrator privileges'
    task = subprocess.Popen(['/usr/bin/osascript', '-e', script], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    deadline = time.monotonic() + 90
    played = False
    try:
        while task.poll() is None and time.monotonic() < deadline:
            if not played and state().get('ACEState') == 'capturing':
                played = True
                time.sleep(0.5)
                subprocess.run([player, '3'], timeout=7, check=True)
            time.sleep(0.15)
        if task.poll() is None:
            raise RuntimeError('Administrator prompt or capture timed out')
        print(task.communicate()[0].strip())
    finally:
        if task.poll() is None:
            task.terminate()
            task.communicate(timeout=5)
    after = state()
    end = time.monotonic() + 2
    while after.get('ACEFirmwareResident') and not after.get('ACEBlockedUntilReboot') and time.monotonic() < end:
        time.sleep(0.15)
        after = state()
    for name in ('ACEState', 'ACEFirmwareResult', 'ACEDmiL0Result', 'ACED0ixResult', 'ACEInputLease',
                 'ACECaptureResult', 'ACECapturePositionChanges', 'ACEPlaybackPositionChanges',
                 'ACESpeakerState', 'ACESpeakerIPC', 'ACESpeakerRequest', 'ACESpeakerReply', 'ACESpeakerRunPolls',
                 'ACESpeakerFailedRequest', 'ACESpeakerFailedReply',
                 'ACESpeakerPositionChanges', 'ACESpeakerRestored', 'ACESpeakerHostControl', 'ACESpeakerHostBytes',
                 'ACESpeakerHostReinitialized', 'ACESpeakerHostResetUnconfirmed', 'ACESpeakerHostProgress', 'ACESpeakerLinkProgress',
                 'ACESpeakerFormat', 'ACESpeakerCodecMask', 'ACESpeakerPPBefore', 'ACEInputDmaSafe', 'ACEDspRestored', 'ACED3Result'):
        print(f'{name}: {after.get(name, "unavailable")}')
    labels = ('sdCtl', 'lpib', 'dpib', 'pplcCtl', 'linkPos', 'mlCtl', 'mlSid', 'spbCtl', 'spib')
    for name in ('ACESpeakerFirstTrace', 'ACESpeakerLatestTrace'):
        raw = after.get(name)
        if isinstance(raw, bytes) and len(raw) == 4 * len(labels):
            values = struct.unpack('<' + 'I' * len(labels), raw)
            print(name + ': ' + json.dumps(dict(zip(labels, [f'0x{value:08x}' for value in values]))))
    print('A 660 Hz reference was played during capture.' if played else 'No reference was played.')
    return task.returncode or (0 if played and after.get('ACESpeakerHostProgress', 0) >= 4
                               and after.get('ACESpeakerLinkProgress', 0) >= 4 else 1)


if __name__ == '__main__':
    raise SystemExit(run())
