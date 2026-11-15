# NOVA Installer

Homebrew installer for **NOVA**, a custom boot system for Nintendo Switch.

This branch (`Nova-installer`) contains only the installer application, built
as `Nova-Installer.nro` and launched from Homebrew Menu under Atmosphère.
The boot payload itself lives in the `Nova-boot` branch and is embedded
verbatim into this NRO at build time.

```
Nova-boot       -> NOVA.bin, the real Tegra boot payload
Nova-installer  -> Nova-Installer.nro, this application
```

## Features

- **Dashboard** – installation status, installed version, SD free space,
  embedded payload size and SHA-256, installed file verification, hekate
  availability, Atmosphère and emuMMC readiness, backup status.
- **TRANSFER FILES** – writes the embedded payload to `Nova/NOVA.bin` and
  `bootloader/payloads/NOVA.bin`, writes the version file, the NOVA boot
  entries and the configuration. Shows stage, percentage, transferred and
  total bytes, and verifies the result with SHA-256.
- **INSTALL NOVA** – compatibility checks (payload format and version,
  hekate present, free space), one-time backup of NOVA managed files, file
  transfer, then full verification of both payload copies and both boot ids.
- **UNINSTALL NOVA** – restores the backed up configuration, removes only
  NOVA owned files, then verifies the removal. `atmosphere/`, `emummc/`,
  `Nintendo/` and the user's `bootloader/hekate_ipl.ini` are never touched.
- **BACKUP & RESTORE** – create, verify and restore a backup with a
  SHA-256 manifest. Backups live in `Nova/backup/latest/`.
- Controller driven console UI: D-pad, A confirm, B back, + exit.

## SD card layout

| Path | Owner | Purpose |
| --- | --- | --- |
| `Nova/NOVA.bin` | NOVA | canonical payload copy |
| `Nova/version.txt` | NOVA | installed version |
| `Nova/config/nova.ini` | NOVA | boot configuration (theme is built in) |
| `Nova/themes/README.txt` | NOVA | theme notes |
| `Nova/backup/latest/` | NOVA | backup files plus `manifest.txt` |
| `bootloader/payloads/NOVA.bin` | NOVA | payload copy loadable from hekate |
| `bootloader/ini/NOVA.ini` | NOVA | NOVA owned hekate entries (`nvstk`, `nvemu`) |
| `switch/Nova-Installer.nro` | user | this application |
| `hekate_ctcaer_x.x.x.bin` | hekate | official payload, supplied by the release |

## Safety

The installer never writes `BOOT0`, `BOOT1`, the NAND, `atmosphere/` or
`emummc/`. It never edits `bootloader/hekate_ipl.ini`. Everything it does is
on the SD card and can be undone with **UNINSTALL NOVA** or **RESTORE
BACKUP**. At boot time NOVA does not patch any file either: it sets hekate's
documented `boot_cfg` byte in its own in-memory payload copy to select the
`nvstk` or `nvemu` entry.

## Build

Requires devkitPro with devkitA64 and libnx:

```sh
export DEVKITPRO=/opt/devkitpro
export PATH=$DEVKITPRO/tools/bin:$DEVKITPRO/devkitA64/bin:$PATH
make -j$(nproc)
```

Output: `Nova-Installer.nro` (plus `.nacp`). `data/NOVA.bin` is the payload
built in the `Nova-boot` branch and is embedded with `.incbin`.

Host tests for the checksum code:

```sh
make -C tests/host
```

Verify the NRO structure, NACP asset and embedded payload:

```sh
python3 tools/verify_nro.py Nova-Installer.nro
```

## Release archive

```sh
python3 tools/release.py \
    --installer Nova-Installer.nro \
    --payload data/NOVA.bin \
    --hekate release/hekate_ctcaer_6.5.5.bin \
    --out dist/NOVA-v1.0.0.zip
```

The archive contains `Nova/`, `switch/Nova-Installer.nro` and the official
`hekate_ctcaer_6.5.5.bin`. Extract it to the SD card root.

## Testing status

| Item | Status |
| --- | --- |
| Installer compiles (devkitA64 + libnx) | yes |
| NRO format, NACP and icon assets verified | yes |
| Embedded payload byte-identical to Nova-boot `dist/NOVA.bin` | yes |
| SHA-256 against FIPS test vectors | yes (5 checks) |
| Installer engine: scan, transfer, install, uninstall, backup, restore | yes (43 host checks) |
| Console UI rendered on a Switch | not run here |
| Install on a physical SD card | not run here |
| Launch from hbmenu on console | not run here |
| EmuMMC, Atmosphère, hekate boot paths | not run here (see Nova-boot) |

No emulator or hardware run was possible in this environment, so the file
operations are verified by construction and checksum logic, not by execution
against a mounted `sdmc:` device. See [docs/TESTING.md](docs/TESTING.md).

## Licensing

NOVA is licensed under GPL-2.0-or-later. The release archive redistributes
the official hekate payload (`hekate_ctcaer_6.5.5.bin`, GPL-2.0), built and
published by the hekate project. See `NOTICE` and `LICENSE`.
