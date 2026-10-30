#!/bin/sh
set -eu
k=$1
root=$2
tftp=$3
src=$4
case "$root" in ''|/) echo 'Refusing an empty or / rootfs destination' >&2; exit 1;; esac
release=$(make -s -C "$k" ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" kernelrelease)
[ -n "$release" ]
for file in "$src/build/sensor-dashboard" "$src/build/sensor-sqlite3" "$src/build/imx6ull-alientek-emmc.dtb" "$k/arch/arm/boot/zImage" "$src/drivers/env_lm75.ko" "$src/drivers/env_adxl345.ko" "$src/drivers/env_gt9147.ko" "$src/drivers/env_alarm.ko"; do
    [ -s "$file" ] || { echo "Missing $file" >&2; exit 1; }
done
mkdir -p "$root/usr/bin" "$root/etc/init.d" "$root/etc/default" "$root/lib/modules/$release/extra" "$root/var/run" "$tftp"
install -m 755 "$src/build/sensor-dashboard" "$root/usr/bin/sensor-dashboard"
for name in env_lm75 env_adxl345 env_gt9147 env_alarm; do
    install -m 644 "$src/drivers/$name.ko" "$root/lib/modules/$release/extra/$name.ko"
done
install -m 755 "$src/scripts/S99sensor-dashboard" "$root/etc/init.d/S99sensor-dashboard"
[ -f "$root/etc/default/sensor-dashboard" ] || install -m 644 "$src/scripts/sensor-dashboard.conf" "$root/etc/default/sensor-dashboard"
# Keep user edits; only append settings an older config does not have yet.
conf="$root/etc/default/sensor-dashboard"
grep -E '^[A-Z_]+=' "$src/scripts/sensor-dashboard.conf" | while IFS= read -r line; do
    grep -q "^${line%%=*}=" "$conf" || printf '%s\n' "$line" >> "$conf"
done
mkdir -p "$root/var/lib/sensor-dashboard"
install -m 755 "$src/build/sensor-sqlite3" "$root/usr/bin/sensor-sqlite3"
for name in zImage imx6ull-alientek-emmc.dtb; do
    [ ! -f "$tftp/$name" ] || cp "$tftp/$name" "$tftp/$name.bak-$(date +%Y%m%d-%H%M%S)-$$"
done
install -m 644 "$k/arch/arm/boot/zImage" "$tftp/zImage"
install -m 644 "$src/build/imx6ull-alientek-emmc.dtb" "$tftp/imx6ull-alientek-emmc.dtb"
# BusyBox rcS normally executes S??*. Do not duplicate that execution path.
if [ -f "$root/etc/init.d/rcS" ]; then
    if ! grep -Eq 'S\?\?|S\*|S\[|S99sensor-dashboard' "$root/etc/init.d/rcS"; then
        cp "$root/etc/init.d/rcS" "$root/etc/init.d/rcS.bak-$(date +%Y%m%d-%H%M%S)-$$"
        temp="$root/etc/init.d/rcS.sensor-tmp"
        # Insert before a final exit if the existing script has one.
        awk 'BEGIN {added=0} /^[ \t]*exit([ \t]|$)/ && !added {print "/etc/init.d/S99sensor-dashboard start"; added=1} {print} END {if(!added) print "/etc/init.d/S99sensor-dashboard start"}' "$root/etc/init.d/rcS" > "$temp"
        cat "$temp" > "$root/etc/init.d/rcS"
        rm -f "$temp"
    fi
else
    printf '#!/bin/sh\n/etc/init.d/S99sensor-dashboard start\n' > "$root/etc/init.d/rcS"
    chmod 755 "$root/etc/init.d/rcS"
    echo 'Created rcS; confirm /etc/inittab invokes it on this rootfs.'
fi
if command -v depmod >/dev/null 2>&1; then depmod -b "$root" "$release" || echo 'depmod failed; startup uses explicit insmod paths.'; fi
echo "Installed kernel $release, DTB, modules and application. Reboot the board using TFTP + NFS."
