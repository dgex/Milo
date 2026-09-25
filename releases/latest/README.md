# Restore the latest Milo firmware

Download **Milo.elf** (recommended) or **Milo.bin** from this directory.
Use GitHub's Download raw file button. The manifest records the source commit,
validation notes, and SHA-256 checksums. These are the latest published,
validated artifacts; experimental builds remain in the ignored build folder.

## Flash using NXP LinkServer

For a connected MIMXRT1170-EVKB with a single debug probe:

```powershell
& 'C:/NXP/LinkServer_25.6.131/LinkServer.exe' flash MIMXRT1176xxxxx:MIMXRT1170-EVKB load Milo.elf
```

Adjust the installed LinkServer path if needed. Stop the USB bridge/serial
monitor first. Restore the board's normal QSPI boot configuration if another
project changed it, then fully power-cycle after flashing.

The ELF contains its programming addresses. The raw BIN must be programmed at
**0x30000400**, NOT 0x30000000. Avoid whole-chip erase: the Adblocker blocklist
bank starts at 0x30800000. Restoring Milo replaces the current application.

## Current prototype limitations

- Display/touch and USB clock/weather synchronization initialized successfully
  during the last hardware run; the latest text layout still needs visual confirmation.
- The Murata Wi-Fi module reported an initialization timeout after flashing.
- Clock/weather currently require the computer bridge and USB connection.
- Microphone capture and spoken conversation are not implemented.
- The compiled setup AP password/token are shared by everyone downloading this
  build. No home-network password is compiled into the image.

## Publishing subsequent firmware

Commit source changes, build release using `scripts/build.ps1 -Configuration
flexspi_nor_release`, and validate on the board. From the repository root:

```powershell
./scripts/publish-firmware.ps1 -ValidationNotes 'Describe actual hardware checks and remaining limitations' -Push
```

The publisher checks source cleanliness, build freshness, and agreement between
ELF and BIN, then copies, hashes, commits, and pushes the artifacts. It does not
flash the board or claim that compilation proves hardware functionality.
