# Verification record

Executed locally with Windows MinGW GCC 16.1.0 and Git sh:

- `test_logic.c`: PASS; compiled with `-std=c99 -Wall -Wextra -Werror`.
- `preview.c` + UI/graphics/font/parser/attitude: PASS; same strict flags.
- Both valid-data and error-data screens rendered at 16, 24 and 32 bpp with padded rows and y-offset. 32bpp PNGs visually inspected.
- DTS preprocessing and dtc compile: PASS against local reference imx6ull.dtsi/imx6ul-pinfunc.h. The host dtc build used `-Dlstat=stat -fcommon` for Windows compatibility only; no target source affected.
- POSIX shell syntax: PASS.
- Isolated install test: PASS for ordinary rcS, repeated installation, and existing S?? launcher. No actual NFS/TFTP installation performed.

Not executed: Linux application/ARM module build against the user's KDIR, full zImage build, board boot, hardware measurements, physical touch tests.

CAN addition, 2026-10-30, Windows MinGW GCC 16.1.0 and Git sh:

- `test_logic.c` with `app/inspect.c`: PASS, `-std=c99 -Wall -Wextra -Werror`.
- `preview.c` rendering normal, defect and CAN-link-down screens: PASS; 32/16/24bpp PNGs visually inspected.
- `tests/test-install.sh` including CAN config append for older configs: PASS. Shell syntax: PASS.

Not executed: `app/can_bus.c` and `app/main.c` compile (need Linux headers), DTS recompile after removing flexcan1 xceiver-supply, FlexCAN on hardware, STM32 interoperability.

Thread mailbox, alarm and SQLite addition, 2026-10-30, Windows MinGW GCC 16.1.0:

- `test_logic.c` with `app/alarm_logic.c`: PASS, `-std=c99 -Wall -Wextra -Werror`.
- `test_services.c` (mailbox, store, alarm/db threads, SQLite 3.7.3 amalgamation, alarm device replaced by a file): PASS 7 of 7 runs, `-Wall -Wextra -Werror`. Windows-only shim for mkdir/O_CLOEXEC/setenv passed with `-include`; not part of the project.
- `preview.c` normal, defect, temperature alarm and fault screens: PASS, visually inspected.
- `tests/test-install.sh` with env_alarm.ko, sensor-sqlite3 and config key merge: PASS. Shell syntax: PASS.

Not executed: `drivers/env_alarm.c` build, DTS compile with the env-alarm node and SNVS pin group, ARM build of the SQLite amalgamation and shell, LED/buzzer on hardware, SQLite on the NFS root filesystem.

LVGL interface, 2026-10-30, Windows MinGW GCC 16.1.0:

- All 195 LVGL v8.4.1 sources with app/lv_conf.h: compiled, no warnings at default level.
- `test_ui.c` + `app/ui_lvgl.c` + generated fonts, `-Wall -Wextra -Werror`: PASS. Normal, defect, temperature alarm and fault screens at 32 bpp; normal at 16/24 bpp with padded rows and y offset; PNGs visually inspected. Every character on screen resolves to a glyph. Virtual pointer clicks on both buttons return the right action; a card click returns none.
- Windows-only shim (localtime_r, mkdir, O_CLOEXEC, setenv) passed with `-include`; not part of the project.
- `test_logic`, `test_services` unaffected.

Not executed: `app/ui_display.c` and `app/main.c` compile (Linux headers), ARM build of LVGL, refresh rate on the i.MX6ULL, real GT9147 coordinates through LVGL.

MQTT upload, 2026-10-30, Windows MinGW GCC 16.1.0 + Python 3.12:

- Paho MQTT C 1.3.1 sources (without MQTTAsync/SSL): compiled with -std=gnu99 (GCC 16 defaults to C23, where paho's bool typedef fails). Windows-only: htonll/ntohll mapped to __builtin_bswap64 for WebSocket.c, linked with ws2_32/rpcrt4/crypt32.
- `tests/test_upload.py` + `test_upload.c` against `tests/fake_broker.py`, `-Wall -Wextra -Werror`: PASS 3 of 3 runs. Broker opened 3 s late: 4 records queued with backlog 3 (oldest sensor dropped), resent in order after connect; JSON fields and null confidence; retained online status; pause publishes offline and reconnects on resume; reconnect request; offline status at shutdown; CONNACK 4 for a wrong password.
- `test_services.c` with upload linked: PASS (MQTT idle as unconfigured without MQTT_HOST).
- `test_ui.c`: upload screen connected / waiting-to-reconnect / unconfigured rendered and inspected; pause, reconnect and back buttons; main screen with three buttons.

Not executed: real MQTT broker or cloud platform, ARM build of Paho, TLS (not built).

Session-aware result dedup (protocol version 2), 2026-10-30:

- `test_logic.c`: PASS. seq 1 followed by an STM32 reboot that sends seq 1 again under a new session is accepted (restart, not duplicate); same-session repeat still dropped; new session first seen at seq 3 adds 2 missed; repeated session number falls back to the sequence rule; v2 session 0 rejected; v1 frames still accepted.
- `test_services.c`: PASS, including adding the session column to a database created by the previous version.
- `test_upload.py`: PASS, JSON carries "session" (null for v1 records). `test_ui.c`: PASS with the regenerated fonts (304 glyphs).

Not done: the STM32N647 firmware in F:/15-STM32N647_AI still sends version 1 (session byte 0) and keeps its own copy of the old inspect.c in tests/; it needs the change described in docs/CAN_PROTOCOL.md.

LM75BDP replaces DHT11, 2026-10-30:

- `test_logic.c`: LM75 decode against datasheet values (+127.875, +125, +25.125, +0.125, 0, -0.125, -25, -54.875, -55 C; D4..D0 ignored): PASS.
- `test_services.c`: temperature-only sensor rows; a DHT11-era database keeps its table as sensor_dht11 and gets a new sensor table and index: PASS.
- `test_upload.py`: sensor JSON is {"ts","temperature"} with 3 decimals: PASS. `test_ui.c`: temperature card normal/alarm/error, fonts regenerated (305 glyphs): PASS.
- tests/test-install.sh with env_lm75.ko: PASS.

Not executed: `drivers/env_lm75.c` build, DTS compile with lm75@48 under &i2c2, LM75BDP hardware.
