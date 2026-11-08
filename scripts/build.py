#!/usr/bin/env python3
"""Build the notebook kexts with Apple's command-line tools and MacKernelSDK."""

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src'
HID_SOURCE = SOURCE / 'VoodooI2CHID/VoodooI2C Satellites/VoodooI2CHID/VoodooI2CHID'
TARGETS = {
    'ThinkBookISHProbe': [SOURCE / 'ThinkBookISHProbe/ThinkBookISHProbe.cpp'],
    'ISHLightSensor': [SOURCE / 'ISHLightSensor' / name for name in ('ISHLightSensor.cpp', 'AmbientLightValue.cpp')],
    'NativeALSDisplay': [SOURCE / 'NativeALSDisplay/NativeALSDisplay.cpp'],
    'GoodixForceTouch': [SOURCE / 'GoodixForceTouch/GoodixForceTouchEngine.cpp'],
    'VoodooInput': sorted((SOURCE / 'VoodooInput').rglob('*.cpp')),
    'VoodooI2CHID': sorted(HID_SOURCE.rglob('*.cpp')),
}


def run(*args):
    subprocess.run(args, check=True)


def build(name, args):
    target = SOURCE / name
    bundle = args.output / (name + '.kext')
    if bundle.exists():
        raise FileExistsError(f'Choose an empty output location: {bundle}')
    if name == 'ThinkBookI2C':
        (bundle / 'Contents').mkdir(parents=True)
        shutil.copy2(target / 'Info.plist', bundle / 'Contents/Info.plist')
        return

    common = [
        '-target', 'x86_64-apple-macos10.12', '-mkernel', '-fno-common',
        '-fno-stack-protector', '-mno-red-zone', '-nostdinc', '-isystem', str(args.sdk / 'Headers'),
        '-DKERNEL', '-DKERNEL_PRIVATE', '-DDRIVER_PRIVATE', '-O2',
    ]
    cpp = [
        '-fapple-kext', '-std=gnu++14', '-fno-rtti', '-fno-exceptions',
        '-Wno-inconsistent-missing-override', '-Wno-macro-redefined',
    ]
    if name == 'ISHLightSensor':
        if not args.lilu or not args.virtualsmc:
            raise ValueError('ISHLightSensor needs --lilu and --virtualsmc source directories')
        common += ['-I', str(args.lilu / 'Lilu'), '-I', str(args.virtualsmc), '-DPRODUCT_NAME=ISHLightSensor', '-DMODULE_VERSION=1.0.0']
    if name in {'GoodixForceTouch', 'VoodooI2CHID'}:
        common += ['-I', str(SOURCE / 'VoodooInput'), '-I', str(SOURCE / 'VoodooI2CHID/Multitouch Support')]
    if name == 'VoodooInput':
        common += ['-I', str(target), '-I', str(target / 'VoodooInputMultitouch')]

    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='thinkbook-kext-', dir=args.output) as temp:
        work = Path(temp)
        objects = []
        for i, path in enumerate([*TARGETS[name], target / 'module.c']):
            obj = work / f'{i}.o'
            compiler, options = ('clang++', cpp) if path.suffix == '.cpp' else ('clang', [])
            run('xcrun', compiler, *common, *options, '-c', str(path), '-o', str(obj))
            objects.append(str(obj))
        staged = work / bundle.name
        binary = staged / 'Contents/MacOS' / name
        binary.parent.mkdir(parents=True)
        run('xcrun', 'ld', '-kext', '-arch', 'x86_64', '-macos_version_min', '10.12',
            '-undefined', 'dynamic_lookup', '-no_fixup_chains', '-o', str(binary),
            *objects, str(args.sdk / 'Library/x86_64/libkmod.a'))
        binary.chmod(0o755)
        shutil.copy2(target / 'Info.plist', staged / 'Contents/Info.plist')
        run('codesign', '--force', '--sign', '-', '--timestamp=none', str(staged))
        run('codesign', '--verify', '--deep', '--strict', str(staged))
        staged.rename(bundle)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True, help='MacKernelSDK source directory')
    parser.add_argument('--lilu', type=Path, help='Lilu repository root')
    parser.add_argument('--virtualsmc', type=Path, help='VirtualSMC repository root')
    parser.add_argument('--output', type=Path, default=ROOT / 'build')
    parser.add_argument('targets', nargs='*', help='Kext names without .kext, defaults to all')
    args = parser.parse_args()
    names = [*TARGETS, 'ThinkBookI2C']
    for name in args.targets or names:
        if name not in names:
            parser.error(f'Unknown target: {name}')
        build(name, args)


if __name__ == '__main__':
    main()
