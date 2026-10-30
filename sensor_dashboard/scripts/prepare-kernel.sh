#!/bin/sh
set -eu
k=$1
[ -f "$k/Makefile" ] || { echo "Kernel source not found: $k" >&2; exit 1; }
command -v "${CROSS_COMPILE}gcc" >/dev/null || { echo "Missing ${CROSS_COMPILE}gcc" >&2; exit 1; }
[ -f "$k/.config" ] || { echo "Kernel .config is missing. Configure your board kernel first (use the course's working config)." >&2; exit 1; }
[ -x "$k/scripts/config" ] || { echo "Missing kernel scripts/config" >&2; exit 1; }
cp "$k/.config" "$k/.config.sensor-dashboard.tmp"
for symbol in MODULES FILE_LOCKING OF GPIOLIB GPIO_MXC DEVTMPFS DEVTMPFS_MOUNT SPI SPI_MASTER SPI_IMX I2C I2C_IMX INPUT INPUT_EVDEV INPUT_TOUCHSCREEN FB FB_MXS BACKLIGHT_CLASS_DEVICE BACKLIGHT_PWM PWM PWM_IMX TTY SERIAL_IMX SERIAL_IMX_CONSOLE NET CAN CAN_RAW CAN_DEV CAN_CALC_BITTIMING CAN_FLEXCAN; do
    "$k/scripts/config" --file "$k/.config" --enable "$symbol"
done
if ! cmp -s "$k/.config.sensor-dashboard.tmp" "$k/.config"; then
    cp "$k/.config.sensor-dashboard.tmp" "$k/.config.before-sensor-dashboard-$(date +%Y%m%d-%H%M%S)-$$"
fi
rm -f "$k/.config.sensor-dashboard.tmp"
make -C "$k" ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" olddefconfig
for symbol in MODULES FILE_LOCKING OF GPIOLIB GPIO_MXC DEVTMPFS SPI_IMX I2C_IMX INPUT_EVDEV FB_MXS BACKLIGHT_PWM PWM_IMX SERIAL_IMX CAN CAN_RAW CAN_DEV CAN_CALC_BITTIMING CAN_FLEXCAN; do
    grep -q "^CONFIG_${symbol}=y$" "$k/.config" || { echo "Required CONFIG_${symbol}=y did not resolve; inspect kernel configuration." >&2; exit 1; }
done
