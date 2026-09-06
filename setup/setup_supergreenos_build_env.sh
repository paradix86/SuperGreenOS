#!/usr/bin/env bash
set -euo pipefail

# SuperGreenOS build environment bootstrap for WSL Ubuntu
# Target repo docs:
# - ESP-IDF 3.3.1 @ 143d26aa49df524e10fb8e41a71d12e731b9b71d
# - cue 0.0.8
# - mkspiffs
# - ejs-cli
# Verified build order from repo docs is Linux-first.

REPO_PATH_DEFAULT="/mnt/c/Sources/SuperGreenOS"
REPO_PATH="${1:-$REPO_PATH_DEFAULT}"

IDF_DIR="${HOME}/esp/esp-idf_release_3.3.1"
IDF_COMMIT="143d26aa49df524e10fb8e41a71d12e731b9b71d"
IDF_TAG="v3.3.1"
CUE_VERSION="v0.0.8"
MKSPIFFS_DIR="${HOME}/esp/mkspiffs"

log() {
  printf '\n[%s] %s\n' "$(date '+%F %T')" "$*"
}

need_sudo() {
  if [[ "${EUID}" -ne 0 ]]; then
    sudo "$@"
  else
    "$@"
  fi
}

have_cmd() {
  command -v "$1" >/dev/null 2>&1
}

assert_ubuntu() {
  if [[ ! -f /etc/os-release ]]; then
    echo "Unsupported environment: /etc/os-release missing."
    exit 1
  fi
  . /etc/os-release
  if [[ "${ID:-}" != "ubuntu" ]]; then
    echo "Warning: this script was written for Ubuntu/WSL. Detected: ${PRETTY_NAME:-unknown}"
  fi
}

install_apt_packages() {
  log "Installing base apt packages"
  need_sudo apt-get update
  need_sudo apt-get install -y \
    bash \
    curl \
    git \
    make \
    gcc \
    g++ \
    unzip \
    tar \
    xz-utils \
    python3 \
    python3-pip \
    python3-venv \
    python3-setuptools \
    python3-wheel \
    python3-virtualenv \
    python-is-python3 \
    golang-go \
    flex \
    bison \
    gperf \
    libncurses-dev \
    libffi-dev \
    libssl-dev \
    libusb-1.0-0 \
    pkg-config \
    ccache \
    file
}

ensure_node() {
  # NodeSource's nodejs package bundles npm and conflicts with Ubuntu's npm package,
  # so only fall back to the distro packages when node/npm are really missing.
  if have_cmd node && have_cmd npm; then
    log "node $(node --version) / npm $(npm --version) already present"
    return
  fi
  log "Installing nodejs + npm from Ubuntu repositories"
  need_sudo apt-get install -y nodejs npm
}

install_ejs_cli() {
  local found
  found="$(command -v ejs-cli 2>/dev/null || true)"
  if [[ -n "${found}" && "${found}" != /mnt/* ]]; then
    log "ejs-cli already present: ${found}"
    return
  fi
  if [[ -n "${found}" ]]; then
    log "Ignoring Windows ejs-cli at ${found}; installing a native Linux copy"
  fi
  log "Installing ejs-cli globally with npm"
  need_sudo npm install -g ejs-cli
}

install_cue() {
  export GOPATH="${HOME}/go"
  export PATH="${PATH}:${GOPATH}/bin:/usr/local/bin"

  if have_cmd cue && cue version 2>/dev/null | grep -q "${CUE_VERSION#v}"; then
    log "cue already present: $(cue version | head -1)"
    return
  fi

  # The 2019 dependency tree of cue v0.0.8 (golang.org/x/xerrors pre-1.13 API)
  # no longer compiles with a modern Go toolchain, so prefer the prebuilt
  # release binary (sha256 from the release's checksums.txt) and keep
  # `go install` only as a fallback.
  local tarball="cue_${CUE_VERSION#v}_Linux_x86_64.tar.gz"
  local url="https://github.com/cue-lang/cue/releases/download/${CUE_VERSION}/${tarball}"
  local sha256="34ac356ebd0d6cd44811b491dc796c897b528b6b973f03db742cba88fe682227"
  local tmp
  tmp="$(mktemp -d)"
  log "Installing cue ${CUE_VERSION} from ${url}"
  if curl -fsSL -o "${tmp}/${tarball}" "${url}"      && echo "${sha256}  ${tmp}/${tarball}" | sha256sum -c --quiet -      && tar -xzf "${tmp}/${tarball}" -C "${tmp}" cue; then
    need_sudo install -m 755 "${tmp}/cue" /usr/local/bin/cue
    rm -f "${tmp}/${tarball}" "${tmp}/cue"; rmdir "${tmp}"
  else
    rm -f "${tmp}/${tarball}" "${tmp}/cue"; rmdir "${tmp}" 2>/dev/null || true
    log "Prebuilt download/verification failed, falling back to go install"
    go install "cuelang.org/go/cmd/cue@${CUE_VERSION}"
  fi

  cue version | head -1
}

install_mkspiffs() {
  if have_cmd mkspiffs; then
    log "mkspiffs already present: $(command -v mkspiffs)"
    return
  fi

  mkdir -p "${HOME}/esp"

  if [[ ! -d "${MKSPIFFS_DIR}" ]]; then
    log "Cloning mkspiffs"
    git clone --recursive https://github.com/igrr/mkspiffs.git "${MKSPIFFS_DIR}"
  fi
  # spiffs/ is a git submodule; without it main.cpp cannot find spiffs.h
  git -C "${MKSPIFFS_DIR}" submodule update --init --recursive

  log "Building mkspiffs (generic config: OBJ_NAME_LEN=32, META_LEN=0, as documented in README.md)"
  make -C "${MKSPIFFS_DIR}" clean >/dev/null 2>&1 || true
  make -C "${MKSPIFFS_DIR}"
  need_sudo cp "${MKSPIFFS_DIR}/mkspiffs" /usr/local/bin/mkspiffs
  need_sudo chmod 755 /usr/local/bin/mkspiffs
}

install_idf() {
  mkdir -p "${HOME}/esp"

  if [[ ! -d "${IDF_DIR}/.git" ]]; then
    # Clone the release tag directly: a recursive clone of master would first
    # download the submodules of every newer chip (several GB) only to discard
    # them at checkout. --shallow-submodules keeps the ~20 v3.3 submodules small.
    log "Cloning ESP-IDF ${IDF_TAG} (shallow submodules)"
    git clone --branch "${IDF_TAG}" --recursive --shallow-submodules       https://github.com/espressif/esp-idf.git "${IDF_DIR}"
  fi

  log "Checking out ESP-IDF commit ${IDF_COMMIT}"
  if ! git -C "${IDF_DIR}" cat-file -e "${IDF_COMMIT}^{commit}" 2>/dev/null; then
    git -C "${IDF_DIR}" fetch origin "${IDF_COMMIT}"
  fi
  git -C "${IDF_DIR}" checkout --quiet "${IDF_COMMIT}"
  git -C "${IDF_DIR}" submodule sync --recursive
  # --depth 1: GitHub allows fetching the exact pinned submodule commits shallowly
  git -C "${IDF_DIR}" submodule update --init --recursive --force --depth 1
}

apply_idf_compat_fixes() {
  log "Applying documented compatibility fixes if needed"

  local idf_tools="${IDF_DIR}/tools/idf_tools.py"
  local check_lxdialog="${IDF_DIR}/tools/kconfig/lxdialog/check-lxdialog.sh"

  if grep -q -- '--no-site-packages' "${idf_tools}" 2>/dev/null; then
    log "Removing --no-site-packages from idf_tools.py"
    sed -i "s/'--no-site-packages',//g" "${idf_tools}"
    sed -i 's/"--no-site-packages",//g' "${idf_tools}" || true
  fi

  # check-lxdialog.sh must stay intact: besides the ncurses sanity check it
  # prints the -DCURSES_LOC/-lncursesw flags that mconf needs. With
  # libncurses-dev installed the check passes; an earlier version of this
  # script replaced it with a bare "exit 0", which breaks the mconf build.
  if [[ -f "${check_lxdialog}" ]] && ! grep -q 'ccflags()' "${check_lxdialog}"; then
    log "Restoring stubbed check-lxdialog.sh from git"
    git -C "${IDF_DIR}" checkout -- tools/kconfig/lxdialog/check-lxdialog.sh
  fi
}

install_idf_python_env() {
  log "Installing ESP-IDF Python environment"
  (
    cd "${IDF_DIR}"
    ./install.sh
  )

  log "Sourcing export.sh"
  set +u
  # shellcheck disable=SC1090
  source "${IDF_DIR}/export.sh"
  set -u

  log "Pinning setuptools below 81 if needed"
  python -m pip install "setuptools<81"
}

ensure_shell_init_hint() {
  cat <<EOF

Add this to your ~/.bashrc if you want the ESP-IDF env ready by default:

export IDF_PATH="${IDF_DIR}"
source "\$IDF_PATH/export.sh"

EOF
}

verify_repo_tools() {
  log "Verifying toolchain and repo prerequisites"

  export GOPATH="${HOME}/go"
  export PATH="${PATH}:${GOPATH}/bin:/usr/local/bin"

  set +u
  # shellcheck disable=SC1090
  source "${IDF_DIR}/export.sh"
  set -u

  echo "REPO_PATH=${REPO_PATH}"
  echo "IDF_PATH=${IDF_PATH:-<unset>}"
  echo "python3=$(command -v python3 || true)"
  echo "virtualenv=$(command -v virtualenv || true)"
  echo "node=$(command -v node || true)"
  echo "npm=$(command -v npm || true)"
  echo "ejs-cli=$(command -v ejs-cli || true)"
  echo "mkspiffs=$(command -v mkspiffs || true)"
  echo "cue=$(command -v cue || true)"
  echo "git=$(command -v git || true)"
  echo "make=$(command -v make || true)"
  echo "bash=$(command -v bash || true)"
  echo "curl=$(command -v curl || true)"

  echo
  echo "cue version:"
  cue version || true

  echo
  echo "mkspiffs version:"
  mkspiffs 2>/dev/null | head -20 || true

  echo
  echo "ESP-IDF git commit:"
  git -C "${IDF_DIR}" rev-parse HEAD

  if [[ -d "${REPO_PATH}" ]]; then
    log "Repo found. Running repo-specific preflight checks"
    [[ -f "${REPO_PATH}/update_config.sh" ]] || { echo "Missing update_config.sh in repo"; exit 1; }
    [[ -f "${REPO_PATH}/update_templates.sh" ]] || { echo "Missing update_templates.sh in repo"; exit 1; }
    [[ -f "${REPO_PATH}/update_htmlapp.sh" ]] || { echo "Missing update_htmlapp.sh in repo"; exit 1; }
    [[ -f "${REPO_PATH}/config.controller.json" ]] || { echo "Missing config.controller.json in repo"; exit 1; }
    [[ -f "${REPO_PATH}/sdkconfig" ]] || { echo "Missing sdkconfig in repo"; exit 1; }

    echo
    echo "Repo script line ending check:"
    file "${REPO_PATH}/update_config.sh" || true
    file "${REPO_PATH}/update_templates.sh" || true
    file "${REPO_PATH}/update_htmlapp.sh" || true
  else
    echo "Repo path not found: ${REPO_PATH}"
  fi
}

print_next_steps() {
  cat <<EOF

Environment bootstrap complete.

Documented build order for this repo is:
1. ./update_config.sh config_gen/config/SuperGreenOS/Controllers/Controller/v3 config.controller.json
2. bash ./update_templates.sh config.controller.json
3. bash ./update_htmlapp.sh config.controller.json
4. source ~/esp/esp-idf_release_3.3.1/export.sh
5. make defconfig
6. make -j4

Suggested next commands:

cd "${REPO_PATH}"
./update_config.sh config_gen/config/SuperGreenOS/Controllers/Controller/v3 config.controller.json
bash ./update_templates.sh config.controller.json
bash ./update_htmlapp.sh config.controller.json
source "${IDF_DIR}/export.sh"
make defconfig
make -j4

EOF
}

main() {
  assert_ubuntu
  install_apt_packages
  ensure_node
  install_ejs_cli
  install_cue
  install_mkspiffs
  install_idf
  apply_idf_compat_fixes
  install_idf_python_env
  verify_repo_tools
  ensure_shell_init_hint
  print_next_steps
}

main "$@"