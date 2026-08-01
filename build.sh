#!/usr/bin/env bash
# ============================================================================
#  AluChop — build wrapper
#  ENCT151 Object-Oriented Programming coursework
#  Developed by Shashank Bhattarai (ACE082BCT078)
#
#  Wraps the EXACT configure + build commands verified in TOOLCHAIN.md.
#  Homebrew splits Qt 6 into separate formulae, so the addon modules
#  (Charts / Svg / Tools) need QT_ADDITIONAL_PACKAGES_PREFIX_PATH — note it is
#  PACKAGES, plural. The singular spelling is wrong and silently does nothing.
#
#  Usage:
#    ./build.sh          configure (if needed) + build
#    ./build.sh clean    delete the build directory
#    ./build.sh run      configure + build, then launch the app
#    ./build.sh rebuild  clean, then configure + build
#    ./build.sh help     show this message
# ============================================================================

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
BINARY="${BUILD_DIR}/AluChop"

# --- Verified toolchain values (TOOLCHAIN.md) -------------------------------
QT_BASE_PREFIX='/opt/homebrew/opt/qtbase'
QT_ADDON_PREFIXES='/opt/homebrew/opt/qtcharts;/opt/homebrew/opt/qtsvg;/opt/homebrew/opt/qttools'
GENERATOR='Ninja'

# --- Pretty output ----------------------------------------------------------
if [[ -t 1 ]]; then
    C_BOLD=$'\033[1m'; C_GREEN=$'\033[32m'; C_RED=$'\033[31m'
    C_YELLOW=$'\033[33m'; C_OFF=$'\033[0m'
else
    C_BOLD=''; C_GREEN=''; C_RED=''; C_YELLOW=''; C_OFF=''
fi

info()  { printf '%s==>%s %s\n' "${C_BOLD}${C_GREEN}" "${C_OFF}" "$*"; }
warn()  { printf '%s==>%s %s\n' "${C_BOLD}${C_YELLOW}" "${C_OFF}" "$*" >&2; }
die()   { printf '%serror:%s %s\n' "${C_BOLD}${C_RED}" "${C_OFF}" "$*" >&2; exit 1; }

usage() {
    # Print the banner comment block (line 2 up to the first non-comment line).
    awk 'NR > 1 { if ($0 !~ /^#/) exit; sub(/^# ?/, ""); print }' "${BASH_SOURCE[0]}"
}

# --- Preflight --------------------------------------------------------------
preflight() {
    command -v cmake >/dev/null 2>&1 || die "cmake not found on PATH."
    command -v ninja >/dev/null 2>&1 || die "ninja not found on PATH (brew install ninja)."
    [[ -d "${QT_BASE_PREFIX}" ]] || die "Qt base prefix missing: ${QT_BASE_PREFIX} (brew install qtbase)."

    local missing=()
    local p
    IFS=';' read -r -a _addons <<< "${QT_ADDON_PREFIXES}"
    for p in "${_addons[@]}"; do
        [[ -d "${p}" ]] || missing+=("${p}")
    done
    if (( ${#missing[@]} > 0 )); then
        die "Missing Qt addon formula prefix(es): ${missing[*]} (brew install qtcharts qtsvg qttools)."
    fi
}

# --- Actions ----------------------------------------------------------------
do_configure() {
    info "Configuring (${GENERATOR}) -> ${BUILD_DIR}"
    cmake -B "${BUILD_DIR}" -S "${PROJECT_ROOT}" -G "${GENERATOR}" \
        -DCMAKE_PREFIX_PATH="${QT_BASE_PREFIX}" \
        -DQT_ADDITIONAL_PACKAGES_PREFIX_PATH="${QT_ADDON_PREFIXES}" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
}

do_build() {
    [[ -f "${BUILD_DIR}/CMakeCache.txt" ]] || do_configure
    info "Building"
    cmake --build "${BUILD_DIR}"
    info "Built ${BINARY}"
}

do_clean() {
    if [[ -d "${BUILD_DIR}" ]]; then
        info "Removing ${BUILD_DIR}"
        rm -rf "${BUILD_DIR}"
    else
        warn "Nothing to clean — ${BUILD_DIR} does not exist."
    fi
}

do_run() {
    do_build
    [[ -x "${BINARY}" ]] || die "Binary not found or not executable: ${BINARY}"
    info "Launching AluChop"
    # Run from the project root so assets/, reports/ and exports/ resolve.
    cd "${PROJECT_ROOT}"
    exec "${BINARY}"
}

# --- Dispatch ---------------------------------------------------------------
case "${1:-build}" in
    ""|build)          preflight; do_build ;;
    clean)             do_clean ;;
    rebuild)           preflight; do_clean; do_build ;;
    configure)         preflight; do_configure ;;
    run)               preflight; do_run ;;
    -h|--help|help)    usage ;;
    *)                 die "Unknown command '${1}'. Try: ./build.sh help" ;;
esac
