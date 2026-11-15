# Testing report - Nova-installer

Environment: Linux container, devkitA64 15.2.0 and libnx from the official
`devkitpro/devkita64` image, GNU Make, Python 3.

## Performed

| Check | Command | Result |
| --- | --- | --- |
| Compile and link the NRO | `make -j4` | pass, `Nova-Installer.nro` 410,727 bytes |
| NRO layout (`NRO0` at 0x10, segments inside code size) | `tools/verify_nro.py` | pass |
| Asset section (`ASET`, icon 5,167 B, NACP 16,384 B) | `tools/verify_nro.py` | pass |
| Embedded payload byte-identical to `data/NOVA.bin` | `tools/verify_nro.py` | pass |
| SHA-256 FIPS vectors and 1,000,000-byte stream | `make -C tests/host` | 5 checks, 0 failures |
| Installer engine on a real filesystem tree | `make -C tests/host` | 43 checks, 0 failures |
| Automatic boot backup, marker, overwrite and rollback | `make -C tests/host` | 44 checks, 0 failures |
| Release archive contents and inputs | `tools/release.py` | pass |

The payload embedded in the NRO is the `dist/NOVA.bin` file built and
verified in the `Nova-boot` branch (86,224 bytes, NOVA 1.1.0, SHA-256
`54ff802174789ad86b532902f733e025dbe713bab841f8c3d8a549109bd239a3`).

### Installer engine tests

`tests/host/test_installer.c` compiles the real engine, the libnx path
adapter and the shared detection core for the host. Because `sdmc:/` is a
relative path on Linux, the tests create `tests/host/sdmc:/` and exercise:

- SD scan, payload validation, hekate and Atmosphère detection;
- transfer of the real 86,224-byte payload with progress callbacks;
- verification of both payload copies, the version file and both boot ids;
- backup creation and manifest verification;
- detection of a corrupted installed payload;
- restore from backup and re-verification;
- uninstall, including preservation of `atmosphere/` and the hekate payload;
- refusal to install when hekate is missing, with an explicit error.

This is the same C code that runs in the NRO; only the underlying POSIX
filesystem differs from the console's `sdmc:` device.

The automatic boot suite adds: previous `payload.bin` backup and checksum,
`configured -> active` promotion from a simulated NOVA boot marker,
detection of an overwritten `payload.bin`, exact rollback to a hekate
payload, rollback to an empty slot, and refusal of a payload above the
128 KiB injector limit.

## Not performed

- No execution on Nintendo Switch hardware or emulator. The console UI was
  not exercised at runtime.
- No real cold boot through a modchip or dongle; no `splGetConfig` hardware
  query on hardware.
- No Y2K/hbmenu launch test, no HOME button test.
- No physical SD card install, uninstall, backup or restore run.
- No test of the hekate boot path, emuMMC boot or stock boot from NOVA.

These require a console with Atmosphère and hekate. The corresponding code
paths are compiled and their format assumptions are verified, but runtime
behaviour must be treated as untested until run on hardware.

## How to test on hardware

1. Copy the release contents to the SD root.
2. Launch `Nova-Installer` from Homebrew Menu.
3. Confirm the dashboard shows the SD card size, hekate path, Atmosphère and
   emuMMC status.
4. Run `TRANSFER FILES` and compare the shown progress with the file sizes.
5. Run `INSTALL NOVA`, then `BACKUP & RESTORE` -> `VERIFY BACKUP`.
6. Reboot into hekate and select the NOVA entry, or chainload
   `bootloader/payloads/NOVA.bin`; test all three menu entries.
7. Return to the installer and run `UNINSTALL NOVA`; verify `Nova/` no
   longer contains `NOVA.bin` and `bootloader/ini/NOVA.ini` is restored or
   removed, while `atmosphere/`, `emummc/` and `Nintendo/` are unchanged.
