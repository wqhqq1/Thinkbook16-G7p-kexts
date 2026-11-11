#!/bin/sh
# Install only this port's daemon and HAL plug-in. Next boot activates both.
set -eu
[ "$(/usr/bin/id -u)" = 0 ] || { echo 'Run as administrator'; exit 1; }
base=$(cd "$(/usr/bin/dirname "$0")" && pwd -P)
artifacts="$base/build"
[ -d "$artifacts" ] || artifacts="$base/Userspace"
runtime='/Library/Application Support/OhMyDSP-ACE'
hal='/Library/Audio/Plug-Ins/HAL/OhMyDSP-ACE.driver'
plist='/Library/LaunchDaemons/com.ohmydsp.ace.daemon.plist'
case ${1:-install} in
    install) ;;
    *) echo 'Usage: install-userspace.sh [install]'; exit 2 ;;
esac
for path in "$runtime" "$hal" "$plist"; do
    [ ! -e "$path" ] && [ ! -L "$path" ] || { echo "Already present: $path"; exit 1; }
done
/usr/bin/codesign --verify --deep --strict "$artifacts/OhMyDSP-ACE.driver"
/usr/bin/codesign --verify --strict "$artifacts/ohmydspd"
/usr/bin/plutil -lint "$artifacts/com.ohmydsp.ace.daemon.plist"
stage=$(/usr/bin/mktemp -d /Library/.ohmydsp-ace.XXXXXXXX)
commit=0; put_runtime=0; put_hal=0; put_plist=0
finish() {
    if [ "$commit" = 0 ]; then
        [ "$put_plist" = 0 ] || /bin/rm -f "$plist"
        [ "$put_hal" = 0 ] || /bin/rm -rf "$hal"
        [ "$put_runtime" = 0 ] || /bin/rm -rf "$runtime"
    fi
    /bin/rm -rf "$stage"
}
trap finish EXIT
trap 'exit 1' HUP INT TERM
/usr/bin/install -d -o root -g wheel -m 755 "$stage/runtime" "${hal%/*}"
/usr/bin/install -o root -g wheel -m 755 "$artifacts/ohmydspd" "$stage/runtime/ohmydspd"
/usr/bin/install -o root -g wheel -m 644 "$artifacts/com.ohmydsp.ace.daemon.plist" "$stage/daemon.plist"
/usr/bin/ditto --norsrc "$artifacts/OhMyDSP-ACE.driver" "$stage/driver"
/usr/sbin/chown -R root:wheel "$stage/driver"
/bin/chmod -R go-w "$stage/driver"
/usr/bin/codesign --verify --deep --strict "$stage/driver"
/bin/mv "$stage/runtime" "$runtime"; put_runtime=1
/bin/mv "$stage/driver" "$hal"; put_hal=1
/bin/mv "$stage/daemon.plist" "$plist"; put_plist=1
/usr/bin/cmp "$artifacts/ohmydspd" "$runtime/ohmydspd"
/usr/bin/codesign --verify --deep --strict "$hal"
/usr/bin/plutil -lint "$plist"
commit=1
echo 'OhMyDSP ACE daemon and HAL plug-in installed. No audio service was restarted.'
