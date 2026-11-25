# tiny-kamiokande-client

English | [日本語](README.ja.md)

An MCU client for Tiny Kamiokande.
It fetches data that the Tiny Kamiokande server has extracted from the event display images published by the
[Super-Kamiokande Realtime Monitor](https://www-sk.icrr.u-tokyo.ac.jp/realtimemonitor/), maps it onto a 3D model of the detector,
and shows it together with the particle tracks estimated by the server.

The display is not guaranteed to be scientifically accurate (both the tracks and the orientation are estimated from the images and public information).

## Supported devices

- M5Stack CoreS3 (ESP32-S3)
- M5Stack Tab5 (ESP32-P4 + ESP32-C6)
- Host build (PC; renders frames from saved responses to PNG for checking)

## Prebuilt binaries

Prebuilt firmware can be downloaded from [Releases](https://github.com/shapoco/tiny-kamiokande-client/releases).
Extract the zip and flash with `upload.sh` in the directory for your device ([esptool](https://github.com/espressif/esptool) is required).

```sh
m5cores3/upload.sh /dev/ttyACM0   # CoreS3
m5tab5/upload.sh /dev/ttyACM0     # Tab5
```

The Tab5 needs its C6 updated after flashing (see below).

## Requirements for building from source

- [ESP-IDF](https://github.com/espressif/esp-idf) 5.5 (environment variable `IDF_ROOT`, default `~/esp/5.5/esp-idf`)
- A clone of [ShapoGFX](https://github.com/shapoco/shapo-gfx) (environment variable `SHAPOGFX_PATH`, default `~/shapo-gfx`)

## Build and flash

```sh
impl/m5cores3/run.sh /dev/ttyACM0   # CoreS3 (adjust the port to your environment)
impl/m5tab5/run.sh /dev/ttyACM0     # Tab5
impl/m5cores3/build.sh -DTKAPP_DEMO=1   # Demo mode: shows the embedded samples without networking
```

On the Tab5, HTTPS does not work with the factory firmware of the ESP32-C6, so the C6 must be updated first.
The Tab5 firmware embeds the C6 update image; run the `c6ota` command on the USB serial console to update it (about 11 seconds; see [SPEC.md](SPEC.md) 5.6).

## WiFi settings

Tap the WiFi icon at the top right of the screen to open the settings screen. You can also configure it with the `wifi` command on the USB serial console.
The settings are stored in the device's NVS.

## Documentation

See [SPEC.md](SPEC.md) (Japanese) for the specification, the server API and implementation details.
