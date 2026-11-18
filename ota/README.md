# OTA and release workflow

A push to `main` (or a manual run of the GitHub Actions workflow) builds the ESP32 firmware, increments the patch version from the latest `vX.Y.Z` release tag, and publishes a GitHub Release containing:

- `udm-prig-client.ino.bin` — firmware image for OTA
- `version.txt` — version compiled into that firmware

The ESP32 downloads both assets from:

`https://github.com/nad22/UDM-Packet-Radio-Internet-Gateway/releases/latest/download/`

Automatic updates only install a strictly newer numeric `major.minor.patch`
version. An older version must be flashed manually. Local builds use the
fallback version in `include/firmware_version.h`; a build made from unreleased
changes should be identified as a test build, not a published release.

The release workflow requires GitHub Actions to be enabled and the repository setting **Settings → Actions → General → Workflow permissions** to allow a token to create releases (the workflow requests `contents: write`).

For local builds, `include/firmware_version.h` supplies the fallback version. The build hook copies the generated image into this directory; only the automated release workflow publishes OTA assets.
