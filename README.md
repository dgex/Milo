# Milo: an RT1170 screen companion

Milo is a first working firmware prototype for the **MIMXRT1170-EVKB**, the
**Murata Type 2EL / IW612** M.2 module, and the **RK055MHD091A0-CTG** 5.5-inch
720 × 1280 MIPI display. These parts are identified in the sibling Adblocker
project, so a screen serial number is not needed for this version.

The screen shows a pair of animated eyes that blink, wink, glance around, and
offer changing little prompts. A tap
opens a clock and today's weather; a second tap or 18 seconds of inactivity
returns to setup until connected, then to the eyes. Two full-screen 4-bit indexed-color framebuffers live in
the EVKB's on-chip OCRAM; LCDIFv2 uses a 16-color palette.
The display uses LCDIFv2 and the panel's HX8394 driver. Touch uses GT911 on
LPI2C5. Wi-Fi provisioning reuses the Adblocker project's proven Murata setup
flow, adapted for Milo.

This is an **animated companion foundation**, not a local language model or
voice assistant yet. The board has an onboard P2 microphone and WM8962 audio codec, but this
firmware does not yet capture audio, play spoken replies, or run a conversation service. The clock is network supplied and advances
locally between updates; there is no battery-backed RTC in this implementation.

## Build

Requires the MCUXpresso SDK 26.06.00-LTS and tools used by the sibling
Adblocker project. From PowerShell in this directory:

```powershell
./scripts/build.ps1 -Configuration flexspi_nor_debug
./scripts/build.ps1 -Configuration flexspi_nor_release
```

The address-aware image is
`build/flexspi_nor_debug/rt1170_companion_cm7.elf`. Use the ELF for debugger
programming. The raw `.bin` is generated too but should not be treated as an
image beginning at the flash base. The linker keeps the application in the
first 8 MiB of QSPI NOR so it does not overlap the Adblocker project's
blocklist bank at `0x30800000`. Programming this ELF still replaces the
current application. No flashing is done by the build script.

## First board run

1. With power off, keep the onboard microSD slot empty: it shares USDHC1 with
   the Murata M.2 Wi-Fi module. Connect the panel to J48. The SDK example
   specifies 5 V on J43 with J38 on pins 1–2.
2. Program the ELF using a suitable EVKB debugger configuration, then fully
   power cycle. A warm reset has previously caused IW612 SDIO initialization
   failures on this board.
3. Open the debug serial port at **115200 8N1**. Scan the Wi-Fi QR code on
   Milo's display to join `Milo-Setup` on a phone, then browse to
   `http://192.168.4.1/`. On the setup page, attach a photo of your home
   Wi-Fi QR code to fill the network details on your phone, or enter them
   manually. The QR photo stays on the phone. The setup
   password is also printed on serial. Credentials remain in RAM and must be
   entered again after a reboot. Milo's eyes appear after Wi-Fi connects and weather data arrives.
4. Keep the board's USB connection attached to this computer and start the
   clock/weather helper with approved automatic IP location:

   ```powershell
   & "$env:USERPROFILE/.mcuxpressotools/.mcux-venv-3.12/Scripts/python.exe" bridge/usb_sync.py --auto-location
   ```

5. Tap the screen for clock and today's forecast. The helper detects approximate
   city and time zone using ipwho.is, fetches Open-Meteo forecasts, and sends
   updates over USB every 15 seconds. Do not open another serial console while
   the helper is using COM9. Stop the helper before flashing.

USB delivery works even if the Murata module fails to initialize. In that case
Milo keeps the setup QR visible when the dashboard closes. Wi-Fi must connect
before the eyes appear. The helper and computer must remain running; this is
not yet a standalone internet weather implementation on the board.

## Verification and next milestone

The debug and release firmware build with GCC 14.2.1 against the installed SDK, and the
bridge protocol has a local test:

```powershell
& "$env:USERPROFILE/.mcuxpressotools/.mcux-venv-3.12/Scripts/python.exe" -m unittest discover -s tests -v
```

The release ELF was programmed to the connected EVKB on 2026-09-24 using
LinkServer 25.6.131; every ELF flash section verified successfully. A 2 MiB
readback of the previous application is saved at `build/pre-milo-2mib.bin`.
COM9 reported `Touch: GT911 ready` and
`Display: RK055MHD091 full-screen OCRAM companion ready`. The screen was
changed from external SDRAM scanout to a full-panel OCRAM framebuffer after
an on-board SDRAM readback check failed in Milo. NXP's standalone SDRAM test
passed, so that integration issue remains isolated from the display path.
The Murata module timed out during firmware download after debugger resets;
a full physical power cycle and visual/touch confirmation are still needed.
The next hardware pass should confirm QR readability, touch events, Wi-Fi
setup, and the weather request. The next product milestone is
adding an audio path and a real conversation service, with explicit privacy
and power behavior appropriate to the hardware.

## Hardware and data sources

- Local source: `../AD_Blocker/docs/hardware/storage-and-display.md`
- NXP panel: https://www.nxp.com/design/design-center/development-boards-and-designs/RK055HDMIPI4MA0
- Open-Meteo forecast API: https://open-meteo.com/en/docs

## Refresh and USB synchronization update

The renderer draws into an inactive packed framebuffer and waits for the LCD
frame-completion callback before reusing the previous frame. Both frames fit
in OCRAM; external SDRAM is not used. The palette has 16 colors.

`bridge/usb_sync.py` sends clock and forecast snapshots over COM9 every 15
seconds. This also works after Wi-Fi initialization fails. The helper must
remain running and USB connected. Missing weather no longer suppresses time.
Automatic approximate city/time-zone lookup is opt-in with `--auto-location`;
this sends a request to ipwho.is, and Open-Meteo receives the returned coordinates.
Without a configured or approved automatic location, only the clock is sent.
The Wi-Fi firmware-download timeout remains unresolved.

The dashboard centers its labels within the rounded cards and labels the city as an IP location estimate. A local sentence generator changes the two-line joke bubble every 30 seconds on both the face and dashboard; it does not call a language model.
