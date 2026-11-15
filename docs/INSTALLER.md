# NOVA Installer - technical notes

## Data flow

```
Nova-Installer.nro
  |
  |-- .incbin data/NOVA.bin             embedded payload (built in Nova-boot)
  |
  |-- TRANSFER FILES
  |     write Nova/NOVA.bin              (chunked, progress callback)
  |     write bootloader/payloads/NOVA.bin
  |     write Nova/version.txt
  |     write bootloader/ini/NOVA.ini    (ids nvstk / nvemu)
  |     write Nova/config/nova.ini
  |     write Nova/themes/README.txt
  |     SHA-256 of both payload copies, version file and boot ids
  |
  |-- INSTALL NOVA
  |     compatibility: payload valid, version marker, hekate found, free space
  |     backup (only when no backup exists yet)
  |     TRANSFER FILES
  |     nova_verify_installation()
  |
  |-- UNINSTALL NOVA
  |     restore the previous cold boot payload (payload.bin) when configured
  |     restore backup when it contains files
  |     remove NOVA owned files and empty NOVA folders
  |     verify removal; the backup folder is kept
  |
  |-- AUTOMATIC BOOT
  |     back up payload.bin, write NOVA, verify hash and format
  |     record state=pending plus the current NOVA boot counter
  |     promote to ACTIVE only when the counter advances after a reboot
  |     restore previous payload (or clear the slot) on rollback
```

## Compatibility checks

The embedded payload is accepted only when:

- its size is between 1 KiB and hekate's 192 KiB payload limit;
- it is not an NRO (`NRO0`), ELF (`\x7fELF`) or NSO (`NSO0`) image;
- the first word is an ARM32 data-processing/branch instruction;
- it contains the current `NOVA_VERSION_STRING` version marker, so installer
  and payload versions cannot drift apart silently.

Automatic boot adds one more check: the payload must be at most 128 KiB, the
limit enforced by modchip `sdloader` firmware.

`INSTALL NOVA` additionally requires a valid hekate payload on the SD card
(root `hekate*.bin` or `bootloader/payloads/hekate*.bin`) and at least
three payload copies of free space.

## Rendering

libnx keeps the console contents in memory and only submits a frame to the
display from `consoleUpdate()`. Every screen built by `source/main.c` therefore
ends with `present()`, which is that call. Skipping it leaves a black screen
that looks exactly like a failed launch: the applet is running, reads the SD
card and answers the pad, but nothing is ever drawn.

Rendering is also decoupled from reading the SD card. The dashboard is scanned
once at startup and after every action, not on each frame, so navigating the
menus never repeats checksum work on the payload copies.

## Progress and checksums

File writes are chunked at 16 KiB and report `(stage, done, total)` to the
UI, which draws a percentage bar plus transferred and total bytes. After a
transfer both payload copies are hashed and compared with the in-memory
payload; `Nova/version.txt` and `bootloader/ini/NOVA.ini` are checked too.
Any mismatch is reported as a failure and leaves the previous files intact
because all writes are new or replaced atomically per file.

## Backup format

`Nova/backup/latest/manifest.txt`:

```
NOVA-BACKUP 1
<count>
<sha256> <size> <relative path>
...
```

Files are stored under `Nova/backup/latest/files/<relative path>`. The
backup set is:

- `bootloader/ini/NOVA.ini`
- `bootloader/payloads/NOVA.bin`
- `Nova/NOVA.bin`
- `Nova/version.txt`
- `Nova/config/nova.ini`

`VERIFY BACKUP` re-hashes every stored file and compares size and SHA-256.
`RESTORE BACKUP` verifies first, restores each file, then re-hashes the
restored copy. `UNINSTALL NOVA` uses the same restore path before removing
NOVA files.

## Error handling

Every operation returns a negative code and fills a human readable message
that the UI shows on a result screen. Typical errors:

- SD card not mounted (`statvfs` on `sdmc:/` failed);
- folder creation failure;
- short write or flush failure;
- checksum mismatch after transfer or restore;
- missing hekate payload;
- insufficient free space;
- backup manifest missing, truncated or referencing missing files.

No operation proceeds past a failed verification.

## What the installer does not do

- No BOOT0/BOOT1 or NAND access.
- No modification of `atmosphere/`, `emummc/` or `Nintendo/`.
- No editing of `bootloader/hekate_ipl.ini`; NOVA entries live in
  `bootloader/ini/NOVA.ini`, a hekate directory intended for extra entries.
- No autoboot configuration changes; the user selects NOVA once and NOVA
  then picks the target entry with hekate's `boot_cfg` byte.
