#!/bin/sh
set -eu
# Isolated fake kernel/rootfs. Never touches the real NFS/TFTP folders.
base=$(pwd)
mkdir -p "$base/build"
tmp=$(mktemp -d "$base/build/install-test.XXXXXX")
mkdir -p "$tmp/bin" "$tmp/kernel/arch/arm/boot" "$tmp/src/build" "$tmp/src/drivers" "$tmp/src/scripts" "$tmp/root/etc/init.d" "$tmp/tftp"
printf '#!/bin/sh\necho 4.1.15-test\n' > "$tmp/bin/make"
printf '#!/bin/sh\nexit 0\n' > "$tmp/bin/depmod"
chmod +x "$tmp/bin/make" "$tmp/bin/depmod"
export PATH="$tmp/bin:$PATH" CROSS_COMPILE=arm-linux-gnueabihf-
printf 'kernel\n' > "$tmp/kernel/arch/arm/boot/zImage"
printf 'application\n' > "$tmp/src/build/sensor-dashboard"
printf 'sqlite shell\n' > "$tmp/src/build/sensor-sqlite3"
printf 'dtb\n' > "$tmp/src/build/imx6ull-alientek-emmc.dtb"
for n in env_lm75 env_adxl345 env_gt9147 env_alarm; do printf 'module\n' > "$tmp/src/drivers/$n.ko"; done
cp scripts/S99sensor-dashboard scripts/sensor-dashboard.conf "$tmp/src/scripts/"
printf '#!/bin/sh\necho boot\nexit 0\n' > "$tmp/root/etc/init.d/rcS"
printf 'old-kernel\n' > "$tmp/tftp/zImage"
sh scripts/install.sh "$tmp/kernel" "$tmp/root" "$tmp/tftp" "$tmp/src"
sh scripts/install.sh "$tmp/kernel" "$tmp/root" "$tmp/tftp" "$tmp/src"
[ "$(grep -c 'S99sensor-dashboard start' "$tmp/root/etc/init.d/rcS")" = 1 ]
grep -q 'GPS_DEVICE=/dev/ttymxc3' "$tmp/root/etc/default/sensor-dashboard"
[ "$(grep -c '^CAN_BITRATE=500000$' "$tmp/root/etc/default/sensor-dashboard")" = 1 ]
# Older config without CAN/alarm/database keys: append once, keep the user's GPS edit.
printf 'GPS_BAUD=115200\n' > "$tmp/root/etc/default/sensor-dashboard"
sh scripts/install.sh "$tmp/kernel" "$tmp/root" "$tmp/tftp" "$tmp/src"
sh scripts/install.sh "$tmp/kernel" "$tmp/root" "$tmp/tftp" "$tmp/src"
grep -q '^GPS_BAUD=115200$' "$tmp/root/etc/default/sensor-dashboard"
[ "$(grep -c '^CAN_DEVICE=can0$' "$tmp/root/etc/default/sensor-dashboard")" = 1 ]
[ "$(grep -c '^ALARM_TEMP_HIGH=' "$tmp/root/etc/default/sensor-dashboard")" = 1 ]
[ "$(grep -c '^DB_PATH=' "$tmp/root/etc/default/sensor-dashboard")" = 1 ]
[ "$(grep -c '^GPS_BAUD=' "$tmp/root/etc/default/sensor-dashboard")" = 1 ]
[ -x "$tmp/root/usr/bin/sensor-sqlite3" ] && [ -d "$tmp/root/var/lib/sensor-dashboard" ]
[ -f "$tmp/root/lib/modules/4.1.15-test/extra/env_alarm.ko" ]
[ -f "$tmp/root/lib/modules/4.1.15-test/extra/env_gt9147.ko" ]
[ -f "$tmp/tftp/imx6ull-alientek-emmc.dtb" ]
# Existing S?? launcher must remain untouched.
printf '#!/bin/sh\nfor i in /etc/init.d/S??*; do "$i" start; done\n' > "$tmp/root/etc/init.d/rcS"
cp "$tmp/root/etc/init.d/rcS" "$tmp/original"
sh scripts/install.sh "$tmp/kernel" "$tmp/root" "$tmp/tftp" "$tmp/src"
cmp "$tmp/original" "$tmp/root/etc/init.d/rcS"
echo 'PASS: install destinations, repeated installation, config key merge, alarm module, SQLite shell, rcS hook and existing S?? launcher'
