# Environment Setup

This project uses legacy ESP-IDF `3.3.1` with the old `make`-based build.

## Verified environment

- Host OS: modern Ubuntu Linux
- ESP-IDF path: `$HOME/esp/esp-idf_release_3.3.1`
- ESP-IDF commit: `143d26aa49df524e10fb8e41a71d12e731b9b71d`

## Recommended local tooling

- `python3`
- `virtualenv`
- `node`
- `npm`
- `ejs-cli`
- `mkspiffs`
- `cue 0.0.8`

## ESP-IDF checkout

```bash
mkdir -p $HOME/esp
cd $HOME/esp
git clone --recursive https://github.com/espressif/esp-idf.git esp-idf_release_3.3.1
cd esp-idf_release_3.3.1
git checkout 143d26aa49df524e10fb8e41a71d12e731b9b71d
git submodule sync --recursive
git submodule update --init --recursive --force
```

## Important notes for modern Linux

## Upstream reference repo

This project already contains the generator architecture from `SuperGreenOSBoilerplate`.

Practical rule:

- use `SuperGreenOSBoilerplate` as a comparison/reference repo
- do not vendor the whole boilerplate into this project

Useful comparison targets:

- `config_gen/config/`
- `templates/`
- `main/core/`
- root helper scripts

### Python setting in this repo

This project must not force Python 2 on a modern host.

Use:

```text
CONFIG_PYTHON="python"
```

in:

- `sdkconfig`
- `sdkconfig.defaults`

### Config generation is part of the build

This project now expects `config.controller.json` to be regenerated from CUE before template generation.

Canonical order:

```bash
cd /home/alan/sources/SuperGreenOS
./update_config.sh config_gen/config/SuperGreenOS/Controllers/Controller/v2.1 config.controller.json
bash ./update_templates.sh config.controller.json
bash ./update_htmlapp.sh config.controller.json
```

Important local fix:

- `update_config.sh` must not delete `CONFIG_VERSION` from `sdkconfig`

### Legacy ESP-IDF compatibility issues

These are local environment fixes that may be needed in the ESP-IDF checkout:

1. `tools/idf_tools.py`
   Remove `--no-site-packages` if virtualenv creation fails.
2. `setuptools`
   Pin below `81` if `pkg_resources` is missing:

```bash
pip install "setuptools<81"
```

3. `tools/kconfig/lxdialog/check-lxdialog.sh`
   Some modern systems hit a false negative for `ncurses`.

## Required project generation step

If you skip generation, files such as these may be missing or stale:

- `main/component.mk`
- `main/init.c`
- `main/core/modules.h`
- `main/core/include_modules.h`
- generated KV / keys metadata derived from `config.controller.json`
