#!/usr/bin/env bash
set -euo pipefail

# Uses installed libcurl and SDL3 >= 3.4. No runtime, model or dependency download.
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cyui_root=$(realpath -- "${1:-$project_root/../CyImgui_Interface}")
build_root=$(realpath -m -- "${2:-$project_root/build-chat-linux}")
portable_root=$(realpath -m -- "${3:-$project_root/out/ChatLab-linux-$(uname -m)}")
if [[ $(uname -s) != Linux ]]; then
    echo 'Run this script on Linux.' >&2
    exit 2
fi
if [[ -z ${DISPLAY:-} && -z ${WAYLAND_DISPLAY:-} ]]; then
    echo 'Run from a graphical session (DISPLAY or WAYLAND_DISPLAY is required for the window tests).' >&2
    exit 2
fi
cmake -S "$project_root" -B "$build_root" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCYMCP_BUILD_CHAT_APP=ON -DCYMCP_BUILD_TESTS=ON -DCYMCP_CYUI_SOURCE_DIR="$cyui_root"
cmake --build "$build_root" --parallel "${CYMCP_BUILD_JOBS:-4}"
ctest --test-dir "$build_root" --output-on-failure
cmake --install "$build_root" --prefix "$build_root/install"
mkdir -p -- "$portable_root/configs" "$portable_root/licenses"
install -m 755 "$build_root/install/bin/cymcp_chat_app" "$portable_root/cymcp_chat_app"
cp -- "$project_root/LICENSE" "$project_root/THIRD_PARTY_NOTICES.md" \
    "$project_root/examples/chat_app/README.txt" "$portable_root/"
cp -R -- "$build_root/install/bin/Documentation" "$portable_root/"
cp -R -- "$build_root/install/share/CyMCPSDK/licenses/." "$portable_root/licenses/"
cp -- "$project_root"/examples/chat-*.json "$portable_root/configs/"
archive="$portable_root.tar.gz"
tar -czf "$archive" -C "$(dirname -- "$portable_root")" "$(basename -- "$portable_root")"
printf 'Application: %s\nLocal archive: %s\n' "$portable_root/cymcp_chat_app" "$archive"
