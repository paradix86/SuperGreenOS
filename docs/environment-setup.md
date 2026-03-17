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

### Python setting in this repo

This project must not force Python 2 on a modern host.

Use:

```text
CONFIG_PYTHON="python"
```

in:

- `sdkconfig`
- `sdkconfig.defaults`

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

Before building firmware, always regenerate template-derived files:

```bash
cd /home/alan/sources/SuperGreenOS
bash ./update_templates.sh config.controller.json
bash ./update_htmlapp.sh config.controller.json
```

If you skip this, generated files such as these may be missing:

- `main/component.mk`
- `main/init.c`
- `main/core/modules.h`
- `main/core/include_modules.h`
