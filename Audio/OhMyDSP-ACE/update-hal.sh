#!/bin/sh
# Update an existing OhMyDSP ACE installation and reload Core Audio.
set -eu
[ "$(/usr/bin/id -u)" = 0 ] || { echo 'Run as administrator'; exit 1; }
base=$(cd "$(/usr/bin/dirname "$0")" && pwd -P)
source_bundle="$base/build/OhMyDSP-ACE.driver"
[ -d "$source_bundle" ] || source_bundle="$base/Userspace/OhMyDSP-ACE.driver"
runtime='/Library/Application Support/OhMyDSP-ACE'
hal='/Library/Audio/Plug-Ins/HAL/OhMyDSP-ACE.driver'
plist='Contents/Info.plist'
for path in "$runtime" "$hal"; do
    [ -d "$path" ] && [ ! -L "$path" ] && [ "$(/usr/bin/stat -f %u "$path")" = 0 ]
done
for bundle in "$hal" "$source_bundle"; do
    [ "$(/usr/bin/plutil -extract CFBundleIdentifier raw "$bundle/$plist")" = com.ohmydsp.ace.driver ]
done
old_version=$(/usr/bin/plutil -extract CFBundleVersion raw "$hal/$plist")
case "$old_version" in 0.3.3|0.3.4) ;; *) exit 1 ;; esac
[ "$(/usr/bin/plutil -extract CFBundleVersion raw "$source_bundle/$plist")" = 0.3.4 ]
if [ "$old_version" = 0.3.4 ] && /usr/bin/diff -qr "$hal" "$source_bundle" >/dev/null; then
    echo 'This HAL build is already installed.'
    exit 0
fi
stage=$(/usr/bin/mktemp -d /Library/.ohmydsp-hal.XXXXXXXX)
old="$runtime/HAL-$old_version-$(/bin/date +%Y%m%d-%H%M%S).driver"
[ ! -e "$old" ] && [ ! -L "$old" ]
swapped=0
finish() {
    if [ "$swapped" = 0 ] && [ -d "$old" ]; then
        if [ -d "$hal" ]; then /bin/mv "$hal" "$stage/failed"; fi
        /bin/mv "$old" "$hal"
    fi
    /bin/rm -rf "$stage"
}
trap finish EXIT
trap 'exit 1' HUP INT TERM
/usr/bin/ditto --norsrc "$source_bundle" "$stage/driver"
/usr/sbin/chown -R root:wheel "$stage/driver"
/bin/chmod -R go-w "$stage/driver"
/usr/bin/codesign --verify --deep --strict "$stage/driver"
/bin/mv "$hal" "$old"
/bin/mv "$stage/driver" "$hal"
/usr/bin/codesign --verify --deep --strict "$hal"
swapped=1
printf 'Installed HAL 0.3.4; previous bundle: %s\n' "$old"
/usr/bin/killall -TERM coreaudiod
