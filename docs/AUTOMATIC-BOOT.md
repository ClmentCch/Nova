# Automatic cold boot

Goal: press POWER and land in the NOVA menu without injecting a payload by hand.

## What actually starts a console

A Nintendo Switch executes BOOT0 from its internal eMMC/NAND. No SD-card file can
change that by itself, and NOVA does not modify BOOT0/BOOT1, the NAND or AutoRCM.
Automatic startup therefore depends on an **external loader** that runs before
BOOT0 and executes a payload file:

- **Modchip** (Spacecraft-NX/HWFLY-NX, Picofly, SX Core/Lite): the `sdloader`
  firmware reads `payload.bin` from the SD root and executes it instead of
  BOOT0. This is the untethered cold boot path for patched Erista and all
  Mariko consoles (V2, Lite, OLED).
- **SD-payload capable dongle / RCM loader**: some injectors read `payload.bin`
  from the SD root on power-on. For unpatched Erista this is still a tethered
  setup (the dongle is required), but the payload itself can be NOVA.
- **AutoRCM**: forces the console into RCM by corrupting the boot configuration
  in BOOT0. NOVA deliberately does not implement or use it. It is a boot
  partition modification and, on patched consoles, cannot be undone by software.

## Compatibility matrix

| Console | Cold boot without an external loader | Cold boot with `payload.bin` loader | Notes |
| --- | --- | --- | --- |
| Unpatched Erista (V1) | no | yes | dongle/modchip must read `payload.bin`; fusee-gelee consoles can also keep using an injector |
| Patched Erista | no | yes | modchip only |
| Mariko (V2 / Lite / OLED) | no | yes | modchip only |
| Any console, no loader | no | no | BOOT0/AutoRCM is not modified |

The installer can read the SoC type (Erista/Mariko) when `spl:` is accessible,
but **it cannot detect whether a modchip is installed or which file its loader
reads**. That is why the `COMPATIBILITY` row shows `UNKNOWN - DEPENDS ON
INJECTOR` until the boot marker proves otherwise.

## What the installer prepares

`AUTOMATIC BOOT` -> `ENABLE AUTOMATIC BOOT`:

1. Requires an installed, verified NOVA payload (`Nova/NOVA.bin`) and a valid
   hekate payload, because NOVA hands the actual boot to hekate.
2. Refuses payloads larger than 128 KiB (modchip `sdloader` limit). NOVA is
   about 84 KiB.
3. Backs up an existing `payload.bin` to
   `Nova/backup/autoboot/payload.bin` together with `meta.txt`
   (kind, size, SHA-256). If the slot was empty, the manifest records
   `kind=none` so rollback knows to clear it.
4. Writes NOVA to `payload.bin`, reads it back, checks the SHA-256 and validates
   the payload format.
5. Writes `Nova/state/autoboot.txt` with `state=pending` and the current NOVA
   boot counter.

No success is reported unless every file operation above is verified.

## Verification after reboot

NOVA writes `Nova/state/boot.txt` on every start:

```
[nova]
version=1.1.0
boots=7
```

The installer records `boots_at_configure` when enabling. On the next scan:

| Condition | Displayed state |
| --- | --- |
| `payload.bin` hash differs from the configured payload | `OVERWRITTEN` |
| marker counter has not advanced | `CONFIGURED - AWAITING COLD BOOT` |
| marker counter advanced | `ACTIVE - NOVA RAN AFTER CONFIG` |
| rollback performed | `DISABLED - PREVIOUS BOOT RESTORED` |

The marker proves that NOVA executed, not who started it. A manual chainload
also increments it, so the installer still asks for a power cycle and never
claims "automatic boot works" without that evidence. A console with an external
loader that reads `payload.bin` will show `ACTIVE` after the first cold boot.

## Rollback

`RESTORE PREVIOUS BOOT`:

- restores the backed-up `payload.bin` and verifies its SHA-256, or
- removes `payload.bin` when the slot was empty, then verifies it is gone,
- writes `state=disabled`.

`UNINSTALL NOVA` performs the same rollback before removing NOVA files, so a
console is never left with a NOVA cold boot slot after uninstalling.

If a cold boot fails and the console does not reach the NOVA menu:

- modchip firmware: hold **VOL+ and VOL-** during power-on to bypass
  `payload.bin` and boot OFW (supported by Spacecraft-NX/HWFLY-NX and Picofly),
  or remove/rename `payload.bin` on a PC;
- dongle: inject hekate as before, mount the SD card and remove `payload.bin`.

The previous payload is in `Nova/backup/autoboot/payload.bin`; copy it back to
`payload.bin` to undo the change without booting the installer.

## Why hekate autoboot is not written automatically

hekate's `autoboot` option selects a boot entry by position in
`hekate_ipl.ini`, and payload entries are supported. Editing that file would
change the user's global boot configuration and is fragile when entries are
added or removed. NOVA keeps `bootloader/ini/NOVA.ini` as its own file and never
edits `hekate_ipl.ini`. Users who already cold boot hekate and want
power-on -> NOVA can rename/point their loader at `payload.bin` = NOVA; the
`LAUNCH HEKATE` menu entry still reaches hekate at any time.

## Files

```
SD:/payload.bin                         external loader slot (NOVA when enabled)
SD:/Nova/backup/autoboot/payload.bin    previous payload
SD:/Nova/backup/autoboot/meta.txt       kind/size/SHA-256 of the previous payload
SD:/Nova/state/autoboot.txt             pending/active/disabled + configured hash
SD:/Nova/state/boot.txt                 NOVA boot counter
```

No BOOT0, BOOT1, eMMC/NAND, `atmosphere/` or `emummc/` access is involved.
