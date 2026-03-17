![SuperGreenLab](assets/sgl.png?raw=true "SuperGreenLab")

# Table of Contents

   * [SuperGreenOS](#supergreenos)
      * [Who is this document for](#who-is-this-document-for)
      * [Features](#features)
   * [Quickstart](#quickstart)
      * [Workspace setup](#workspace-setup)
         * [Esp-idf setup](#esp-idf-setup)
         * [Clone repo, build and run](#clone-repo-build-and-run)
         * [Connect to wifi](#connect-to-wifi)
      * [Basic concept](#basic-concept)
      * [Key / value](#key--value)
      * [Available keys](#available-keys)
         * [Core keys](#core-keys)
         * [Controller keys](#controller-keys)
         * [Box keys](#box-keys)
         * [Led keys](#led-keys)

![WeedAppPic](assets/weedapppic.png?raw=true "WeedAppPic")

# SuperGreenOS

SuperGreenOS provides most features used when growing cannabis, all in one package, and controllable from your smartphone, pc, mac, linux, toaster, plumbus, whatnot...

It is the official firmware for the [SuperGreenController](https://github.com/supergreenlab/SuperGreenController).

## Documentation

For the practical, maintained documentation set, start here:

- `docs/README.md`
- `docs/environment-setup.md`
- `docs/build-and-ota.md`
- `docs/ui-customization.md`
- `docs/backend-roadmap.md`

## Who is this document for

This document is for developpers that want to start playing with there controller's internal stuffs, or just setup their own hardware.
This repository is based on [SuperGreenOSBoilerplate](https://github.com/supergreenlab/SuperGreenOSBoilerplate), please read the doc here first.

## Features

Here's what it can (or will) do:

- Lights on and off schedules
- Up to 6 separate led channels (you can put multiple leds behind one channel)
- Up to 3 separate timers, for full-cycle setups (veg + flo)
- Monitoring a wide range of sensors
- Data sent to a MQTT server
- Produce alerts based on sensor values
- Allows remote control
- Manual ventilation control
- Automatic ventilation control based on temperature and humidity
- `Stretch` mode, allows to choose how much you want your plant to stretch or thicken
- `Sunglass` mode, so you don't burn your eyes when you work on your plants
- More to come..

This is the firmware that runs the [SuperGreenController](https://github.com/supergreenlab/SuperGreenController).

# Workspace setup

If you haven't already done it, you'll to setup esp-idf's toolchain and sdk.

They have a very good quickstart [here](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html).

## Clone repo, build and run

Now you should be able to clone and build the firmware:

```

git clone https://github.com/supergreenlab/SuperGreenOS.git
cd SuperGreenOS
./update_templates.sh config.controller.json
./update_htmlapp.sh config.controller.json
make -j4

```

The plug your controller or any esp32 based board and run the commands:

```

make -j4 flash monitor
./write_spiffs.sh

```

The first command flashes the firmware, the second writes the embedded admin interface on the tiny file system (~20KB available).

# How to use

Once the firmware is flashed you can access the controller's wifi network, once connected go to http://192.168.4.1/fs/app.html,
this will display the html embedded admin interface, which allows you to easily modify any of the controller's parameter.

![Admin](assets/admin.png?raw=true "Admin")


# Up-2-date dev environment setup 06/2020

## Python 2.7

### macos
```bash
brew install python@2
```

## ESP-IDF


```bash
mkdir -p $HOME/esp && cd $HOME/esp
git clone --recursive https://github.com/espressif/esp-idf.git esp-idf_release_3.3.1
cd esp-idf_release_3.3.1
git checkout 143d26aa49df524e10fb8e41a71d12e731b9b71d
```

Install Docs reference:
https://docs.espressif.com/projects/esp-idf/en/v3.3.2/get-started/index.html

```bash
python2.7 -m pip install --user -r $IDF_PATH/requirements.txt
```

Practically, a virtualenv is created in ~/.espressif where packages are installed and will be activated with the following addition to shell (.bashrc / .zshrc)

```bash
export IDF_PATH=$HOME/esp/esp-idf_release_3.3.1
source $IDF_PATH/export.sh
```

## ejs-cli
```bash
npm -g install ejs-cli
```

## mkspiffs

Please pay attention to *Build configuration name: generic* and version.

https://github.com/igrr/mkspiffs/releases

```bash
mkspiffs ver. 0.2.3
Build configuration name: generic
SPIFFS ver. 0.3.7-5-gf5e26c4
Extra build flags: (none)
SPIFFS configuration:
  SPIFFS_OBJ_NAME_LEN: 32
  SPIFFS_OBJ_META_LEN: 0
  SPIFFS_USE_MAGIC: 1
  SPIFFS_USE_MAGIC_LENGTH: 1
  SPIFFS_ALIGNED_OBJECT_INDEX_TABLES: 0
```

## cue

https://github.com/cuelang/cue/releases

```bash
cue version 0.0.8 darwin/amd64
```

# Modern Linux Notes

This repository was successfully built and deployed via OTA from a modern Ubuntu machine in March 2026, but only after a few compatibility fixes for legacy ESP-IDF `3.3.1`.

## Important

Before every firmware build, regenerate config, templates, and UI in this order:

```bash
cd /home/alan/sources/SuperGreenOS
./update_config.sh config_gen/config/SuperGreenOS/Controllers/Controller/v2.1 config.controller.json
bash ./update_templates.sh config.controller.json
bash ./update_htmlapp.sh config.controller.json
```

If you skip this, the build may fail with missing generated files such as:

- `main/component.mk`
- `main/init.c`
- `main/core/modules.h`
- `main/core/include_modules.h`

`cue 0.0.8` was used successfully for this generation flow.

## Python setting used by this repo

On modern Linux, this repo must not force Python 2.

Use:

```text
CONFIG_PYTHON="python"
```

in:

- `sdkconfig`
- `sdkconfig.defaults`

## Verified maintenance OTA build

The following worked on Ubuntu after ESP-IDF setup:

```bash
export IDF_PATH=$HOME/esp/esp-idf_release_3.3.1
. $IDF_PATH/export.sh
bash ./scripts/build_maintenance_ota.sh
```

Successful output creates:

```text
releases/SuperGreenMaintenance/last_timestamp
releases/SuperGreenMaintenance/<timestamp>/firmware.bin
```

Verified timestamps include:

- `1773751114`
- `1773757485`

## Verified OTA over Wi-Fi

Example verified setup:

- controller: `192.168.1.104`
- build host: `192.168.1.151`
- server port: `8091`

Serve OTA files:

```bash
cd /home/alan/sources/SuperGreenOS/releases
python3 -m http.server 8091
```

Controller OTA settings:

- `server_ip = 192.168.1.151`
- `server_hostname = 192.168.1.151`
- `server_port = 8091`
- `basedir = /SuperGreenMaintenance`

Then trigger OTA by setting:

- `start = 1`

The controller was observed requesting:

- `/SuperGreenMaintenance/last_timestamp`
- `/SuperGreenMaintenance/<timestamp>/firmware.bin`

This flow was later reused to deploy the first working `sensor_health` backend to a live controller.

## Maintenance build caveat

`scripts/build_maintenance_ota.sh` enables a one-time SPIFFS format. This means the controller may boot the new firmware successfully but lose `/fs/app.html` until the web UI is uploaded again.

If `/fs/app.html` returns `This URI does not exist` after maintenance OTA, restore the UI with:

```bash
cd /home/alan/sources/SuperGreenOS
bash ./update_htmlapp.sh config.controller.json
bash ./upload_htmlapp.sh 192.168.1.104 ./spiffs_fs
```

## SPIFFS size note

The legacy SPIFFS partition is only `32 KB`, so UI size is a real deployment constraint.

Important practical discoveries:

- duplicated CSS can make `app.html` too large to upload
- `upload_htmlapp.sh` now prefers `zopfli` when available and falls back to `gzip -9 -n`
- the current web UI includes a very small `sensor_health` debug bridge because richer JS quickly burns the SPIFFS budget

## Sensor health backend

The repo now includes a first real backend module:

- `main/sensor_health/`

It is generated/configured through:

- `config_gen/config/SuperGreenOS/Controllers/sensor_health.cue`

Verified live controller state included:

- `SENSOR_HEALTH_STATUS = 3`
- `SENSOR_HEALTH_LAST_ALERT = box_0_co2_stuck`
- `SENSOR_HEALTH_STUCK_SAMPLES = 5`

This was verified to restore the admin UI after OTA.

## See also

- `README_TEST.md` for safe UI-only testing
- `Agents.md` for a repo-specific operational log of the setup and OTA findings
