# Milo project instructions

## Firmware delivery

The user requires the latest functional flashable firmware to be available in
GitHub for restoring this board after working on other projects.

Whenever firmware changes are completed:
1. Build flexspi_nor_release and perform appropriate validation. A successful
   compilation alone is not hardware validation. Record actual evidence and
   remaining limitations; never call an untested candidate fully functional.
2. Commit source changes before publishing the matching artifacts.
3. Run scripts/publish-firmware.ps1 -ValidationNotes '<actual checks and limitations>' -Push.
4. Ensure releases/latest/Milo.elf, Milo.bin, manifest.json, and SHA256SUMS.txt
   are pushed. Keep the previously working release if validation fails.
5. Prefer the ELF for flashing; the current raw BIN base is 0x30000400.
   Do not mass-erase flash; preserve the Adblocker bank at 0x30800000.

Documentation-only updates do not require rebuilding firmware.
Never include home Wi-Fi credentials, generated portal key files, user photos,
or logs in commits. Published firmware includes its setup AP password/token;
these are shared public setup credentials, not private per-device secrets.
