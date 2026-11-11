#!/bin/zsh
# Build a complete OhMyDSP ACE package without modifying the source tree.
set -eu
if (( $# < 4 || $# > 5 )); then
    print -u2 'Usage: scripts/build-microphone.sh MacKernelSDK sof-mtl.ri topology.tplg NHLT.bin [output-directory]'
    exit 2
fi
root=${0:A:h:h}
src="$root/src/OhMyDSPACE"
assets="$root/Audio/OhMyDSP-ACE"
sdk=${1:A}
firmware=${2:A}
topology=${3:A}
nhlt=${4:A}
out=${5:-"$root/build/OhMyDSP-ACE"}
out=${out:A}
mkdir -p "${out:h}"
[[ ! -e "$out" && ! -L "$out" ]] || { print -u2 "Output already exists: $out"; exit 1; }
stage=$(mktemp -d "${out:h}/.ohmydsp.XXXXXXXX")
trap 'rm -rf "$stage"' EXIT
package="$stage/package"
mkdir -p "$package/build" "$stage/src"
"$src/build.sh" "$sdk" "$package/build" "$firmware" "$topology" "$nhlt"
cp -R "$src/userspace" "$src/include" "$stage/src/"
make -C "$stage/src/userspace"
cp -R "$stage/src/userspace/build/OhMyDSP.driver" "$package/build/OhMyDSP-ACE.driver"
cp "$stage/src/userspace/build/ohmydspd" "$package/build/"
cp "$assets/Userspace/com.ohmydsp.ace.daemon.plist" "$package/build/"
cp "$assets"/{OpenCore-Kernel-Add.plist,install-userspace.sh,update-hal.sh,run-hardware-test.py} "$package/"
cp "$root/LICENSES/OhMyDSP.txt" "$package/LICENSE.OhMyDSP"
cp "$root/LICENSES/Intel-SOF.txt" "$package/LICENCE.Intel"
cp "$root/docs/microphone.md" "$package/README.md"
xcrun clang -arch x86_64 -O2 -Wall -Wextra -Werror -o "$package/build/ace-test" "$src/ace-test.c" -framework IOKit -framework CoreFoundation
xcrun clang -arch x86_64 -O2 -Wall -Wextra -Werror -o "$package/build/ace-speaker-tone" "$src/ace-speaker-tone.c" -framework CoreAudio -framework AudioToolbox -framework CoreFoundation
codesign --verify --deep --strict "$package/build/OhMyDSP-ACE.driver"
codesign --verify --strict "$package/build/ohmydspd"
plutil -lint "$package/build/com.ohmydsp.ace.daemon.plist" "$package/OpenCore-Kernel-Add.plist"
mv "$package" "$out"
print -r -- "$out"
