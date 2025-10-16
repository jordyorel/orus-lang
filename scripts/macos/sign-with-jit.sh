#!/usr/bin/env bash
set -euo pipefail

if [[ "${OSTYPE:-}" != darwin* ]]; then
    echo "This helper only runs on macOS." >&2
    exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ENTITLEMENTS_FILE="$ROOT_DIR/scripts/macos/orus_jit.entitlements"

if [[ ! -f "$ENTITLEMENTS_FILE" ]]; then
    echo "Missing entitlements file: $ENTITLEMENTS_FILE" >&2
    exit 1
fi

declare -a TARGETS=()

append_target() {
    local candidate="$1"
    if [[ -f "$candidate" ]]; then
        TARGETS+=("$candidate")
    fi
}

collect_executables() {
    local dir="$1"
    if [[ -d "$dir" ]]; then
        while IFS= read -r path; do
            append_target "$path"
        done < <(find "$dir" -type f -perm -111)
    fi
}

if [[ $# -eq 0 ]]; then
    DEFAULT_DIR="$ROOT_DIR/build/bin"
    if [[ ! -d "$DEFAULT_DIR" ]]; then
        echo "No targets specified and $DEFAULT_DIR does not exist. Build the project or pass binaries to sign." >&2
        exit 1
    fi
    collect_executables "$DEFAULT_DIR"
else
    for arg in "$@"; do
        if [[ -d "$arg" ]]; then
            collect_executables "$arg"
        else
            append_target "$arg"
        fi
    done
fi

if [[ ${#TARGETS[@]} -eq 0 ]]; then
    echo "No binaries found to sign." >&2
    exit 1
fi

echo "Signing ${#TARGETS[@]} target(s) with JIT entitlement..."
for target in "${TARGETS[@]}"; do
    echo "  • $target"
    codesign --force --sign - --options runtime --entitlements "$ENTITLEMENTS_FILE" "$target"
done

echo "All targets signed. macOS will now permit MAP_JIT allocations for these binaries."
