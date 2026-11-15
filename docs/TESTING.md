# NOVA testing status

Honest status of what has been verified in this branch.

## Tested (host, reproducible)

`make host-test` compiles the real core sources with the host compiler and runs
78 assertions:

| Area | Cases |
| --- | --- |
| Empty / unmounted SD | all targets unavailable, no crash with a NULL filesystem |
| Atmosphère | missing folder, missing `package3`, empty `package3`, legacy `fusee-secondary` |
| emuMMC raw | valid `raw_based` sector, sector parsed, target offered |
| emuMMC file | `file_based` marker, target offered |
| emuMMC disabled | `enabled=0` is not offered |
| emuMMC bad path | `path not found`, target not offered |
| emuMMC bad marker | `raw_based` with sector 0 and no `file_based` |
| emuMMC missing kipm | partial state, target not offered |
| hekate payload | valid magic accepted, renamed/invalid image rejected |
| hekate discovery | root and `bootloader/payloads` lookup |
| INI parser | CRLF, whitespace, comments, sections, `id=` lookup |
| Wildcards / versions | `hekate*.bin`, version extraction |
| boot_cfg patching | size checks, `ICTC` magic, `id` placement, clearing |
| NOVA.ini generation | both ids, stock/emummc keys, overflow handling |

## Verified structurally

* `NOVA.bin` builds with devkitARM 15.2.0: 93,656 bytes.
* ELF machine `EM_ARM`, entry `0x40010000`, no relocations, required symbols
  present, `.bss` inside the stack budget (`tools/verify_payload.py`).
* The complete UI is rendered on the host from the shipping drawing code
  (`docs/nova-menu.png`), which caught and fixed a rounded-corner bug.
* AutoRCM detection, the BOOT0 read and the payload-slot fallback compile with
  the payload; they are not executable in the host harness because they need
  the Tegra eMMC controller.

## Not tested

**NOVA Boot has not run on a Nintendo Switch.** The following are therefore
unverified:

* display bring-up on real panels (Erista/Mariko/Aula);
* SDMMC initialisation on real cards, exFAT and FAT32;
* Joy-Con/Sio input mapping and the stick deadzone;
* chainloading hekate from NOVA (the trampoline is compiled but never executed);
* hekate's boot-from-ID path with NOVA's `NOVA.ini`;
* battery/charging, docked output and sleep behaviour.

These rely on hekate's own BDK implementations, which are proven in hekate
itself, and on hekate's documented boot_cfg mechanism. That is a strong basis,
not a substitute for hardware testing.

## Hardware checklist (when a console is available)

1. Back up the SD card.
2. Copy the release archive to the SD root, run Nova Installer.
3. Inject `NOVA.bin` from RCM (unpatched console) or the modchip payload slot.
4. Verify: menu renders; SD row `READY`; hekate version shown; emuMMC row matches
   the actual `emummc.ini`.
5. `LAUNCH HEKATE` → hekate menu appears.
6. `LAUNCH STOCK` → OFW boots (no CFW kips).
7. `LAUNCH ATMOSPHERE` → emuMMC boots; confirm the right emuMMC is mounted.
8. Remove `atmosphere/package3`: emuMMC option must disappear and stock must be
   reported unavailable.
9. Uninstall with Nova Installer and confirm the console still boots normally.

Report results with the console model, SD filesystem and hekate version.
