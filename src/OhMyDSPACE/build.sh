#!/bin/zsh
set -eu
if [[ $# != 5 ]]; then
    print -u2 'Usage: ./build.sh /path/to/MacKernelSDK /path/to/output-directory /path/to/sof-mtl.ri /path/to/topology.tplg /path/to/NHLT.bin'
    exit 2
fi
src=${0:A:h}
sdk=${1:A}
out=${2:A}
firmware=${3:A}
topology=${4:A}
nhlt=${5:A}
name=OhMyDSPACE
expected=4f3fb3fc57d2a2c90216d2a6195e24be54921c099395c9d2d019a14bbdc09250
actual=$(shasum -a 256 < "$firmware")
[[ "${actual%% *}" == "$expected" ]] || { print -u2 'Unexpected SOF firmware SHA-256'; exit 1; }
mkdir -p "$out"
[[ ! -e "$out/$name.kext" ]] || { print -u2 'Output bundle already exists'; exit 1; }
stage=$(mktemp -d "$out/.build.XXXXXXXX")
trap 'rm -rf "$stage"' EXIT
contents="$stage/$name.kext/Contents"
mkdir -p "$contents/MacOS"
PYTHONDONTWRITEBYTECODE=1 python3 "$src/build_capture_plan.py" "$firmware" "$topology" "$nhlt" \
    --host-tag 1 --payload-dir "$stage" > "$stage/IPC4-capture-parameters.json"
flags=(-target x86_64-apple-macos10.12 -mkernel -fno-stack-protector -mno-red-zone
       -nostdinc -isystem "$sdk/Headers" -I "$stage" -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -O2)
xcrun clang++ "${flags[@]}" -fapple-kext -std=gnu++14 -fno-rtti -fno-exceptions \
    -Wall -Wextra -Werror -Wno-inconsistent-missing-override \
    -c "$src/$name.cpp" -o "$stage/probe.o"
xcrun clang++ "${flags[@]}" -fapple-kext -std=gnu++14 -fno-rtti -fno-exceptions \
    -Wall -Wextra -Werror -Wno-inconsistent-missing-override \
    -c "$src/UserClient.cpp" -o "$stage/firmware-trial.o"
cp "$firmware" "$stage/sof-mtl.ri"
(
    cd "$stage"
    xcrun clang -target x86_64-apple-macos10.12 -c -x assembler -o firmware-data.o - <<'ASM'
.section __TEXT,__const
.p2align 4
.private_extern _thinkbookSofFirmware
.private_extern _thinkbookSofFirmwareEnd
_thinkbookSofFirmware:
.incbin "sof-mtl.ri"
_thinkbookSofFirmwareEnd:
.p2align 2
.private_extern _thinkbookDmicConfig
.private_extern _thinkbookDmicConfigEnd
_thinkbookDmicConfig:
.incbin "dmic-copier.bin"
_thinkbookDmicConfigEnd:
.p2align 2
.private_extern _thinkbookHostConfig
.private_extern _thinkbookHostConfigEnd
_thinkbookHostConfig:
.incbin "host-copier.bin"
_thinkbookHostConfigEnd:
ASM
)
xcrun clang "${flags[@]}" -c "$src/module.c" -o "$stage/module.o"
xcrun ld -kext -arch x86_64 -macos_version_min 10.12 -undefined dynamic_lookup \
    -no_fixup_chains -o "$contents/MacOS/$name" "$stage/probe.o" "$stage/firmware-trial.o" \
    "$stage/firmware-data.o" "$stage/module.o" \
    "$sdk/Library/x86_64/libkmod.a"
cp "$src/Info.plist" "$contents/Info.plist"
codesign --force --sign - --timestamp=none "$stage/$name.kext"
codesign --verify --deep --strict "$stage/$name.kext"
mv "$stage/$name.kext" "$out/"
mv "$stage/IPC4-capture-parameters.json" "$out/"
