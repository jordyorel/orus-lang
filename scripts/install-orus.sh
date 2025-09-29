#!/usr/bin/env bash
# Install the Orus binary from the GitHub release assets. Intended for use with
# `curl -fsSL https://github.com/jordyorel/orus-lang/releases/latest/download/install-orus.sh | bash`
set -euo pipefail

REPO="jordyorel/orus-lang"

usage() {
    cat <<'USAGE'
Install the Orus CLI from a GitHub release.

Options:
  --version <tag>   Install a specific git tag (e.g. v0.6.0). Defaults to latest release.
  --dest <path>     Installation directory (default: ~/.local/bin).
  --sudo            Elevate privilege for installation if --dest is not writable.
  --dry-run         Print the resolved download URL and destination, then exit.
  -h, --help        Show this message.

Environment variables:
  ORUS_VERSION  Same as --version
  INSTALL_DIR   Same as --dest
USAGE
}

ORUS_VERSION="${ORUS_VERSION:-latest}"
INSTALL_DIR="${INSTALL_DIR:-$HOME/.local/bin}"
USE_SUDO=0
DRY_RUN=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --version)
            shift
            [[ $# -gt 0 ]] || { echo "--version requires an argument" >&2; exit 1; }
            ORUS_VERSION="$1"
            ;;
        --dest|--prefix)
            shift
            [[ $# -gt 0 ]] || { echo "--dest requires an argument" >&2; exit 1; }
            INSTALL_DIR="$1"
            ;;
        --sudo)
            USE_SUDO=1
            ;;
        --dry-run)
            DRY_RUN=1
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
    shift
done

uname_s=$(uname -s)
case "$uname_s" in
    Darwin)
        OS="macos"
        ;;
    Linux)
        OS="linux"
        ;;
    *)
        echo "Unsupported operating system: $uname_s" >&2
        exit 1
        ;;
esac

uname_m=$(uname -m)
case "$uname_m" in
    arm64|aarch64)
        ARCH="arm64"
        ;;
    x86_64|amd64)
        ARCH="x86_64"
        ;;
    *)
        echo "Unsupported architecture: $uname_m" >&2
        exit 1
        ;;
esac

if [[ "$ORUS_VERSION" == "latest" ]]; then
    TAG_SEGMENT="latest"
else
    TAG_SEGMENT="$ORUS_VERSION"
    [[ "$TAG_SEGMENT" == v* ]] || TAG_SEGMENT="v${TAG_SEGMENT}"
fi

ASSET_NAME="orus-${OS}-${ARCH}.tar.gz"
if [[ "$ORUS_VERSION" == "latest" ]]; then
    DOWNLOAD_URL="https://github.com/${REPO}/releases/latest/download/${ASSET_NAME}"
else
    DOWNLOAD_URL="https://github.com/${REPO}/releases/download/${TAG_SEGMENT}/${ASSET_NAME}"
fi

if [[ $DRY_RUN -eq 1 ]]; then
    echo "Would download: ${DOWNLOAD_URL}"
    echo "Would install to: ${INSTALL_DIR}"
    exit 0
fi

command -v curl >/dev/null 2>&1 || { echo "curl is required" >&2; exit 1; }
command -v tar >/dev/null 2>&1 || { echo "tar is required" >&2; exit 1; }
command -v install >/dev/null 2>&1 || { echo "install(1) is required" >&2; exit 1; }

TMPDIR=$(mktemp -d 2>/dev/null || mktemp -d -t orus)
cleanup() {
    rm -rf "$TMPDIR"
}
trap cleanup EXIT

ARCHIVE_PATH="${TMPDIR}/${ASSET_NAME}"

printf "Downloading Orus (%s/%s) from %s...\n" "$OS" "$ARCH" "$DOWNLOAD_URL"
curl -fsSL "${DOWNLOAD_URL}" -o "$ARCHIVE_PATH"

printf "Extracting archive...\n"
tar -xzf "$ARCHIVE_PATH" -C "$TMPDIR"

BINARY_PATH="${TMPDIR}/orus"
[[ -f "$BINARY_PATH" ]] || { echo "Archive does not contain expected 'orus' binary" >&2; exit 1; }

mkdir -p "$INSTALL_DIR"

if [[ ! -w "$INSTALL_DIR" ]]; then
    if [[ $USE_SUDO -eq 1 ]]; then
        SUDO=sudo
    else
        echo "Installation directory '$INSTALL_DIR' is not writable. Re-run with --sudo or choose a different --dest." >&2
        exit 1
    fi
else
    SUDO=""
fi

${SUDO} install -m 0755 "$BINARY_PATH" "$INSTALL_DIR/orus"

printf "Orus installed to %s\n" "$INSTALL_DIR/orus"

if [[ ":$PATH:" != *":${INSTALL_DIR}:"* ]]; then
    echo "Warning: $INSTALL_DIR is not on your PATH. Add it or move the binary to a PATH directory." >&2
fi
