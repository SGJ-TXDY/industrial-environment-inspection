#!/bin/sh
set -eu
k=$1
src=$2
name=imx6ull-alientek-emmc
old="$k/arch/arm/boot/dts/$name.dts"
if [ -f "$old" ] && ! cmp -s "$src/$name.dts" "$old"; then
    cp "$old" "$old.before-sensor-dashboard-$(date +%Y%m%d-%H%M%S)-$$"
fi
cp "$src/$name.dts" "$old"
make -C "$k" ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" "$name.dtb"
mkdir -p "$src/build"
cp "$k/arch/arm/boot/dts/$name.dtb" "$src/build/$name.dtb"
