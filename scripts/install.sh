#!/usr/bin/env sh
set -eu

PROJECT_OWNER="jordyorel"
PROJECT_REPO="orus-lang"
LATEST_URL_BASE="https://github.com/${PROJECT_OWNER}/${PROJECT_REPO}/releases/latest/download"

usage() {
    cat <<'USAGE'
Usage: install.sh [--prefix DIR] [--bin-dir DIR]

Downloads the latest Orus release archive for your platform, extracts it so
that the interpreter binary and std/ directory stay together, and maintains a
symlink in the chosen bin directory.

Options:
  --prefix DIR   Installation root where the archive is extracted. Defaults to
                 /usr/local/opt/orus when run as root/sudo, otherwise
                 ~/.local/opt/orus.
  --bin-dir DIR  Directory where the orus binary symlink should live. Defaults
                 to PREFIX/bin, except when PREFIX is one of the defaults where
                 a more convenient system/user bin directory is chosen.
  -h, --help     Show this help message and exit.
USAGE
}

command_exists() {
    command -v "$1" >/dev/null 2>&1
}

resolve_os() {
    case "$(uname -s)" in
        Linux)   printf 'linux' ;;
        Darwin)  printf 'macos' ;;
        *)
            printf "Unsupported operating system: %s\n" "$(uname -s)" >&2
            exit 1
            ;;
    esac
}

resolve_arch() {
    case "$(uname -m)" in
        x86_64|amd64) printf 'x86_64' ;;
        arm64|aarch64) printf 'arm64' ;;
        *)
            printf "Unsupported architecture: %s\n" "$(uname -m)" >&2
            exit 1
            ;;
    esac
}

PREFIX_OVERRIDE=""
BIN_OVERRIDE=""

while [ "$#" -gt 0 ]; do
    case "$1" in
        --prefix)
            if [ "$#" -lt 2 ]; then
                printf 'Error: --prefix requires a value.\n' >&2
                exit 1
            fi
            PREFIX_OVERRIDE="$2"
            shift 2
            ;;
        --bin-dir)
            if [ "$#" -lt 2 ]; then
                printf 'Error: --bin-dir requires a value.\n' >&2
                exit 1
            fi
            BIN_OVERRIDE="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            printf 'Unknown option: %s\n\n' "$1" >&2
            usage
            exit 1
            ;;
    esac
done

if ! command_exists curl; then
    printf 'Error: curl is required to download releases.\n' >&2
    exit 1
fi

if ! command_exists tar; then
    printf 'Error: tar is required to extract releases.\n' >&2
    exit 1
fi

if [ -n "$PREFIX_OVERRIDE" ]; then
    INSTALL_PREFIX="$PREFIX_OVERRIDE"
else
    if [ "${SUDO_USER-}" != "" ] || [ "$(id -u)" -eq 0 ]; then
        INSTALL_PREFIX="/usr/local/opt/orus"
    else
        INSTALL_PREFIX="${HOME}/.local/opt/orus"
    fi
fi

case "$INSTALL_PREFIX" in
    /*) ;;
    *)
        INSTALL_PREFIX="$(cd "$(dirname "$INSTALL_PREFIX")" 2>/dev/null && pwd)/$(basename "$INSTALL_PREFIX")"
        ;;
esac

if [ -n "$BIN_OVERRIDE" ]; then
    BIN_DIR="$BIN_OVERRIDE"
else
    case "$INSTALL_PREFIX" in
        /usr/local/opt/orus) BIN_DIR="/usr/local/bin" ;;
        "${HOME}/.local/opt/orus") BIN_DIR="${HOME}/.local/bin" ;;
        *) BIN_DIR="${INSTALL_PREFIX}/bin" ;;
    esac
fi

OS_NAME=$(resolve_os)
ARCH_NAME=$(resolve_arch)
ARCHIVE_NAME="orus-${OS_NAME}-${ARCH_NAME}.tar.gz"
DOWNLOAD_URL="${LATEST_URL_BASE}/${ARCHIVE_NAME}"

printf 'Resolving latest release for %s/%s...\n' "$OS_NAME" "$ARCH_NAME"
RESOLVED_URL=$(curl -fsSLI -o /dev/null -w '%{url_effective}' "$DOWNLOAD_URL") || {
    printf 'Failed to resolve release URL.\n' >&2
    exit 1
}

RELEASE_TAG=$(printf '%s\n' "$RESOLVED_URL" | sed -n 's#.*/download/\([^/]*\)/.*#\1#p')
if [ -z "$RELEASE_TAG" ]; then
    RELEASE_TAG="latest"
fi

STAGE_DIR="${INSTALL_PREFIX}/${RELEASE_TAG}"

TMPDIR=${TMPDIR:-/tmp}
WORKDIR=$(mktemp -d "${TMPDIR%/}/orus-install.XXXXXX")
trap 'rm -rf "$WORKDIR"' EXIT INT HUP TERM
ARCHIVE_PATH="${WORKDIR}/${ARCHIVE_NAME}"

printf 'Downloading %s...\n' "$RESOLVED_URL"
if ! curl -fsSL "$RESOLVED_URL" -o "$ARCHIVE_PATH"; then
    printf 'Error: failed to download archive.\n' >&2
    exit 1
fi

printf 'Extracting to %s...\n' "$STAGE_DIR"
mkdir -p "$INSTALL_PREFIX"
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"

if ! tar -xzf "$ARCHIVE_PATH" -C "$STAGE_DIR"; then
    printf 'Error: extraction failed.\n' >&2
    exit 1
fi

BINARY_PATH="${STAGE_DIR}/orus"
STD_PATH="${STAGE_DIR}/std"

if [ ! -x "$BINARY_PATH" ]; then
    printf 'Warning: expected executable not found at %s.\n' "$BINARY_PATH" >&2
fi

if [ ! -d "$STD_PATH" ]; then
    printf 'Warning: expected std directory not found at %s.\n' "$STD_PATH" >&2
fi

PREFIX_BIN_DIR="${INSTALL_PREFIX}/bin"
mkdir -p "$PREFIX_BIN_DIR"
ln -sfn "$BINARY_PATH" "${PREFIX_BIN_DIR}/orus"

if [ "$BIN_DIR" != "$PREFIX_BIN_DIR" ]; then
    mkdir -p "$BIN_DIR"
    ln -sfn "$BINARY_PATH" "${BIN_DIR}/orus"
fi

printf '\nInstalled Orus %s to %s\n' "$RELEASE_TAG" "$STAGE_DIR"
printf 'Symlinked binary to %s\n' "${PREFIX_BIN_DIR}/orus"
if [ "$BIN_DIR" != "$PREFIX_BIN_DIR" ]; then
    printf 'Additional symlink created at %s\n' "${BIN_DIR}/orus"
fi

if [ "$BIN_DIR" = "${HOME}/.local/bin" ]; then
    case ":${PATH}:" in
        *":${HOME}/.local/bin:"*) ;;
        *)
            printf '\nHint: add %s to your PATH.\n' "${HOME}/.local/bin"
            printf 'For example: export PATH="%s:$PATH"\n' "${HOME}/.local/bin"
            ;;
    esac
fi

printf '\nThe standard library remains at %s beside the interpreter.\n' "$STD_PATH"
printf 'Update ORUSPATH if you relocate the std/ directory.\n'
