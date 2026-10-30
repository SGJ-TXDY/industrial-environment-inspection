SHELL := /bin/sh
ARCH := arm
CROSS_COMPILE ?= arm-linux-gnueabihf-
KDIR ?= /home/linux/imx6ull/kernel/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek
DESTDIR ?= /home/linux/nfs/rootfs
TFTP_DIR ?= /home/linux/tftpboot
JOBS ?= 2
CC := $(CROSS_COMPILE)gcc
AR := $(CROSS_COMPILE)ar
HOSTCC ?= gcc
HOSTAR ?= ar
CFLAGS ?= -O2 -g -Wall -Wextra -std=gnu99
APP_SRC := app/main.c app/nmea.c app/graphics.c app/sensor_logic.c app/inspect.c app/can_bus.c \
           app/mailbox.c app/alarm_logic.c app/store.c app/services.c app/upload.c app/mqtt_format.c \
           app/ui_lvgl.c app/ui_display.c app/ui_tick.c $(wildcard app/fonts/*.c)
# SQLite 3.7.3 amalgamation from the course material, built for the target
# toolchain (the supplied prebuilt libsqlite3 is soft-float, not armhf).
SQLITE_DIR := third_party/sqlite
SQLITE_CFLAGS := -O2 -DSQLITE_THREADSAFE=1 -I$(SQLITE_DIR)
# LVGL v8.4 from the course lv_port_linux package; configured by app/lv_conf.h.
# Built once as a static library; use "make -j4" to speed up the first build.
LVGL_DIR := third_party/lvgl
LVGL_SRC := $(shell find $(LVGL_DIR)/src -name '*.c')
LVGL_CFLAGS := -O2 -DLV_CONF_INCLUDE_SIMPLE -Iapp -I$(LVGL_DIR)
LVGL_OBJ := $(patsubst %.c,build/obj/%.o,$(LVGL_SRC))
HOST_LVGL_OBJ := $(patsubst %.c,build/host-obj/%.o,$(LVGL_SRC))
# Eclipse Paho MQTT C 1.3.1 (course mqtt_lib), synchronous client without
# OpenSSL: the same sources as the course's libpaho-mqtt3c, linked statically.
PAHO_DIR := third_party/paho/src
PAHO_SRC := $(wildcard $(PAHO_DIR)/*.c)
PAHO_CFLAGS := -std=gnu99 -O2 -I$(PAHO_DIR)
PAHO_OBJ := $(patsubst %.c,build/obj/%.o,$(PAHO_SRC))
HOST_PAHO_OBJ := $(patsubst %.c,build/host-obj/%.o,$(PAHO_SRC))
APP_CFLAGS := -pthread -DLV_CONF_INCLUDE_SIMPLE -Iinclude -Iapp -I$(SQLITE_DIR) -I$(LVGL_DIR) -I$(PAHO_DIR)
export ARCH CROSS_COMPILE
.PHONY: all app prepare kernel modules dtbs install clean test
all: app modules
app: build/sensor-dashboard build/sensor-sqlite3
build/sqlite3.o: $(SQLITE_DIR)/sqlite3.c $(SQLITE_DIR)/sqlite3.h
	mkdir -p build
	$(CC) $(SQLITE_CFLAGS) -c $< -o $@
build/obj/$(PAHO_DIR)/%.o: $(PAHO_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(PAHO_CFLAGS) -c $< -o $@
build/obj/%.o: %.c app/lv_conf.h
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(LVGL_CFLAGS) -c $< -o $@
build/liblvgl.a: $(LVGL_OBJ)
	rm -f $@
	$(AR) rcs $@ $^
build/libpaho.a: $(PAHO_OBJ)
	rm -f $@
	$(AR) rcs $@ $^
build/sensor-dashboard: $(APP_SRC) $(wildcard app/*.h include/*.h) build/sqlite3.o build/liblvgl.a build/libpaho.a
	$(CC) $(CFLAGS) $(APP_CFLAGS) $(APP_SRC) build/sqlite3.o build/liblvgl.a build/libpaho.a -o $@ -lm -lrt -ldl
# Board-side shell to inspect the database: sensor-sqlite3 /var/lib/sensor-dashboard/sensor.db
build/sensor-sqlite3: $(SQLITE_DIR)/shell.c build/sqlite3.o
	$(CC) $(SQLITE_CFLAGS) $< build/sqlite3.o -o $@ -pthread -ldl
# Chinese UI fonts: re-run after changing text in app/ui_lvgl.c (needs Pillow
# and a Noto Sans CJK/SC font, e.g. apt install python3-pil fonts-noto-cjk).
fonts:
	python3 tools/gen_lv_font.py --font $(FONT) --index $(FONT_INDEX)
FONT ?= /usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc
FONT_INDEX ?= 2
prepare:
	sh scripts/prepare-kernel.sh "$(KDIR)"
kernel: prepare
	$(MAKE) -C "$(KDIR)" -j$(JOBS) zImage modules
modules: kernel
	$(MAKE) -C "$(KDIR)" M="$(CURDIR)/drivers" modules
dtbs: prepare
	sh scripts/build-dtb.sh "$(KDIR)" "$(CURDIR)"
install: all dtbs
	$(MAKE) -C "$(KDIR)" INSTALL_MOD_PATH="$(DESTDIR)" modules_install
	sh scripts/install.sh "$(KDIR)" "$(DESTDIR)" "$(TFTP_DIR)" "$(CURDIR)"
build/host-obj/$(PAHO_DIR)/%.o: $(PAHO_DIR)/%.c
	@mkdir -p $(dir $@)
	@$(HOSTCC) $(PAHO_CFLAGS) -c $< -o $@
build/host-obj/%.o: %.c app/lv_conf.h
	@mkdir -p $(dir $@)
	@$(HOSTCC) $(LVGL_CFLAGS) -c $< -o $@
build/host-liblvgl.a: $(HOST_LVGL_OBJ)
	rm -f $@
	$(HOSTAR) rcs $@ $^
build/host-libpaho.a: $(HOST_PAHO_OBJ)
	rm -f $@
	$(HOSTAR) rcs $@ $^
# Host tests; test_upload.py needs python3 and starts tests/fake_broker.py.
test: build/host-liblvgl.a build/host-libpaho.a
	mkdir -p build
	$(HOSTCC) -std=c99 -Wall -Wextra -Iinclude -Iapp tests/test_logic.c app/nmea.c app/sensor_logic.c app/inspect.c app/alarm_logic.c -o build/test-logic -lm
	./build/test-logic
	$(HOSTCC) $(SQLITE_CFLAGS) -c $(SQLITE_DIR)/sqlite3.c -o build/host-sqlite3.o
	$(HOSTCC) -std=gnu99 -Wall -Wextra $(APP_CFLAGS) tests/test_services.c app/mailbox.c app/alarm_logic.c app/store.c app/services.c app/upload.c app/mqtt_format.c build/host-sqlite3.o build/host-libpaho.a -o build/test-services -lm -ldl
	./build/test-services build
	$(HOSTCC) -std=gnu99 -Wall -Wextra $(APP_CFLAGS) tests/test_upload.c app/mailbox.c app/upload.c app/mqtt_format.c app/alarm_logic.c build/host-libpaho.a -o build/test-upload -lm
	python3 tests/test_upload.py build/test-upload build
	$(HOSTCC) -std=gnu99 -Wall -Wextra $(APP_CFLAGS) tests/test_ui.c app/ui_lvgl.c app/ui_tick.c app/graphics.c app/nmea.c app/sensor_logic.c app/inspect.c app/alarm_logic.c $(wildcard app/fonts/*.c) build/host-liblvgl.a -o build/test-ui -lm
	./build/test-ui build
.PHONY: fonts
clean:
	rm -rf build/obj build/host-obj
	rm -f build/sensor-dashboard build/sensor-sqlite3 build/sqlite3.o build/host-sqlite3.o build/liblvgl.a build/host-liblvgl.a \
	      build/libpaho.a build/host-libpaho.a build/test-logic build/test-services build/test-ui build/test-upload \
	      build/*.ppm build/*.log build/imx6ull-alientek-emmc.dtb
	$(MAKE) -C "$(KDIR)" M="$(CURDIR)/drivers" clean
