# NOVA Boot architecture

## Why a payload, not an NRO

An `.nro` is a homebrew executable for Horizon OS: it needs a running kernel,
services and the homebrew loader. A boot interface runs *before* all of that.

NOVA Boot is therefore a **Tegra X1 payload**:

* raw ARM32 code loaded at `0x40010000` in IRAM by RCM, a modchip, or hekate's
  own chainloader;
* no filesystem, no services, no libnx;
* display, SD and input driven directly through hekate's BDK.

`NOVA.bin` is produced from `payload/start.S`, `payload/main.c` and the BDK by
`payload/Makefile`, then checked by `tools/verify_payload.py` (ELF machine
`EM_ARM`, entry `0x40010000`, no relocations, size ≤ `0x30000`).

## Boot flow

```
RCM / modchip / hekate payloads menu
        │  inject NOVA.bin
        ▼
NOVA payload @ 0x40010000
  hw_init → heap → display → sd_mount → Joy-Con
        │
        ├─ detect: atmosphere/, package3, emummc.ini, emummc.kipm, hekate*.bin
        ├─ ensure bootloader/ini/NOVA.ini exists (NOVA owned file)
        └─ menu
              │ A
              ▼
        read hekate payload into RAM
        patch boot_cfg @ payload+0x94 with id nvstk / nvemu (or clear it)
        sd_end() → hw_deinit(false)
        copy down to 0x40010000 from a heap trampoline, jump
              │
              ▼
        hekate reads boot_cfg, finds the id in bootloader/ini/NOVA.ini,
        runs the documented HOS boot entry (stock or emummcforce)
              │
              ▼
        Horizon OS
```

`B` on the main screen does the same with no boot id, which opens hekate's own
menu — a safe "back".

## hekate integration details

hekate documents a payload boot storage (`README.md`, *Payload storage*):

| Offset | Meaning |
| --- | --- |
| `0x94` | `boot_cfg`: bit0 force autoboot, bit2 boot from ID |
| `0x95` | autoboot entry (`0` = force menu) |
| `0x96` | autoboot list |
| `0x98` | `id[8]` when booting from ID |
| `0x118` | `ipl_ver_meta` magic `"ICTC"` (validated by NOVA) |

hekate's loader copies this block into the decompressed bootloader
(`loader/loader.c`: `memcpy(IPL_LOAD_ADDR + 0x94, &b_cfg, sizeof(boot_cfg_t))`),
and `ipl_main` honours it in `_auto_launch()`.

NOVA validates `"ICTC"` before patching anything, so a renamed NRO or an
unrelated `.bin` can never be treated as hekate.

NOVA's entries live in `bootloader/ini/NOVA.ini`:

```ini
[NOVA Stock (SysNAND)]
id=nvstk
pkg3=atmosphere/package3
stock=1
emummc_force_disable=1

[NOVA Atmosphère (emuMMC)]
id=nvemu
pkg3=atmosphere/package3
kip1=atmosphere/kips/*
emummcforce=1
```

These are exactly the key combinations documented by hekate for stock and for
enforced emuMMC CFW.

## Detection rules

The emuMMC option is only offered when **all** of these are true:

1. `atmosphere/` exists and `atmosphere/package3` is a non-empty file
   (legacy `fusee-secondary.bin` layouts are reported as partial, not ready);
2. `emummc/emummc.ini` exists, `[emummc] enabled=1` and `path` is set;
3. the configured path exists;
4. the emuMMC is real: `<path>/raw_based` contains a non-zero start sector, or
   `<path>/file_based` exists (the two markers hekate itself checks);
5. the emuMMC's `Nintendo` content folder exists (`nintendo_path` or
   `<path>/Nintendo`);
6. `bootloader/sys/emummc.kipm` exists (hekate needs it to boot emuMMC);
7. a valid hekate payload is present.

`LAUNCH STOCK` needs hekate plus `atmosphere/package3`; it stays visible but is
marked unavailable (with the reason) when that is not satisfied.

The detection code is platform independent (`payload/core/nova_detect.c`) and is
compiled unchanged for the host test suite, which builds real directory trees to
exercise every rule.

## Memory and size

| Region | Use |
| --- | --- |
| `0x40010000` | NOVA code and rodata (IRAM) |
| after the image | NOVA `.bss` (must end before `0x4003E000`) |
| `0x4003FF00` | SYS stack (installed by the exception setup) |
| `0x82000000` | heap (image staging, background buffer) |

The payload is 85,672 bytes, inside hekate's `0x30000` chainload limit and the
IRAM stack budget enforced by `payload/link.ld` and the verifier.

## Interface

* Pure black background with a blue/purple glow, star field and animated accent
  line; all drawn by `payload/ui/ui.c` into the Tegra framebuffer.
* 720×1280 portrait framebuffer (`display_init_window_a_pitch()`), which the
  console presents on the internal panel and scales to HDMI when docked.
* Large focusable cards, animated focus rail, pulsing gradient borders.
* Controls: D-pad or left stick to move, `A` to select, `B` for hekate's menu.
  Volume buttons work as a fallback when Joy-Con are detached.
* Status panel: SD, hekate version, Atmosphère, emuMMC mode, boot entries.

The drawing code is the same code rendered into the screenshots in this
repository (`tools/ui_preview.c`).
