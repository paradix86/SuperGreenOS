# Environment Setup

This project uses legacy ESP-IDF `3.3.1` with the old `make`-based build.

## Verified environment

- Host OS: modern Ubuntu Linux (native, or WSL2 Ubuntu 22.04 on Windows 11, verified 2026-09-06)
- ESP-IDF path: `$HOME/esp/esp-idf_release_3.3.1`
- ESP-IDF commit: `143d26aa49df524e10fb8e41a71d12e731b9b71d` (this is exactly tag `v3.3.1`)
- Toolchain: `xtensa-esp32-elf` 1.22.0-80-g6c4433a-5.2.0 (installed by `install.sh` into `~/.espressif`)
- Python: the IDF venv `~/.espressif/python_env/idf3.3_py3.10_env` (Python 3.10 works)

## Automated bootstrap (recommended)

`setup/setup_supergreenos_build_env.sh` installs everything above on Ubuntu/WSL:
apt packages, a native `ejs-cli`, the prebuilt `cue v0.0.8` binary (sha256-verified),
`mkspiffs` built from source with the generic config, ESP-IDF `v3.3.1` with shallow
submodules, and the IDF Python environment.

```bash
bash setup/setup_supergreenos_build_env.sh /path/to/SuperGreenOS
```

It is idempotent: re-running it skips the steps that are already done.
Then build with `scripts/build.sh` (see `build-and-ota.md`).

Notes learned the hard way:

- `go install cuelang.org/go/cmd/cue@v0.0.8` no longer compiles with a modern Go
  (2019 `xerrors` dependency). Use the prebuilt release binary; the script does.
- A recursive clone of esp-idf `master` downloads several GB of submodules for
  newer chips before the 3.3.1 checkout. Clone the `v3.3.1` tag instead.
- `install.sh` needs a `python` executable. On Ubuntu 22.04 install
  `python-is-python3`; in WSL the bare name otherwise resolves to a non-executable
  Windows alias (`/usr/bin/env: 'python': Permission denied`).
- `tools/kconfig/lxdialog/check-lxdialog.sh` must not be stubbed out: it also emits
  the ncurses compiler/linker flags. With `libncurses-dev` installed it just works.

## Recommended local tooling

- `python3` (+ `python-is-python3` on Ubuntu 22.04)
- `virtualenv`
- `node` / `npm` (node 20 verified)
- `ejs-cli` (`npm install -g ejs-cli`)
- `mkspiffs` 0.2.3, generic config (SPIFFS 0.3.7)
- `cue 0.0.8` (prebuilt: https://github.com/cue-lang/cue/releases/tag/v0.0.8; a Windows
  `cue.exe` can live in `%USERPROFILE%\esp\cue`, which the scripts add to `PATH`)

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

## Line endings

`.gitattributes` normalizes every text file to LF (`* text=auto eol=lf`). This matters
because the build runs from Linux/WSL on a checkout that may live on Windows: CRLF in
`Makefile`, `sdkconfig`, `*.sh`, `*.cue` or `*.template` breaks make, kconfig and bash.
If an older checkout still shows CRLF files, run `git checkout -- .` after pulling the
attributes (or convert the files in place).

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
   Keep it intact (see the bootstrap notes above); install `libncurses-dev` instead.

## Required project generation step

If you skip generation, files such as these may be missing or stale:

- `main/component.mk`
- `main/init.c`
- `main/core/modules.h`
- `main/core/include_modules.h`
- generated KV / keys metadata derived from `config.controller.json`
