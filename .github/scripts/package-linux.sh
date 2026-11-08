#!/usr/bin/env bash
set -euo pipefail

build_dir=$1
qt_dir=$(realpath "$2")
version=$3
package=$4
stage=$(mktemp -d "$build_dir/deploy-linux.XXXXXX")
trap 'rm -rf -- "$stage"' EXIT
appdir="$stage/MarkdownReader.AppDir"
exe="$appdir/usr/bin/MarkdownReader"
libdir="$appdir/usr/lib"
plugins="$appdir/usr/plugins"
example=$(realpath examples/example.md)
unset LD_LIBRARY_PATH QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH

install -Dm755 "$build_dir/MarkdownReader" "$exe"
install -Dm755 "$qt_dir/plugins/platforms/libqxcb.so" "$plugins/platforms/libqxcb.so"
install -Dm755 "$qt_dir/plugins/platforms/libqoffscreen.so" "$plugins/platforms/libqoffscreen.so"
mkdir -p "$libdir" "$plugins/imageformats"
cp "$qt_dir/plugins/imageformats/"*.so "$plugins/imageformats/"

# Resolve the full Qt dependency closure, including dependencies of plugins.
# System libraries such as glibc, X11 and graphics drivers stay on the host.
inputs=("$build_dir/MarkdownReader" "$plugins"/platforms/*.so "$plugins"/imageformats/*.so)
LD_LIBRARY_PATH="$qt_dir/lib" ldd "${inputs[@]}" > "$stage/sdk-ldd.txt"
if grep 'not found' "$stage/sdk-ldd.txt"; then
    exit 1
fi
awk -v sdk="$qt_dir/lib/" '$2 == "=>" && index($3, sdk) == 1 { print $3 }' "$stage/sdk-ldd.txt" |
    sort -u | while IFS= read -r library; do
        install -m755 "$library" "$libdir/$(basename "$library")"
    done
for module in Core Gui Widgets XcbQpa; do
    test -f "$libdir/libQt6$module.so.6"
done

patchelf --set-rpath '$ORIGIN/../lib' "$exe"
for library in "$libdir/"*.so*; do
    patchelf --set-rpath '$ORIGIN' "$library"
done
for plugin in "$plugins"/*/*.so; do
    patchelf --set-rpath '$ORIGIN/../../lib' "$plugin"
done
printf '[Paths]\nPrefix=..\nPlugins=plugins\n' > "$appdir/usr/bin/qt.conf"

file "$exe"
readelf -h "$exe" | grep 'Advanced Micro Devices X86-64'
ldd "$exe" "$plugins"/*/*.so | tee "$stage/deployed-ldd.txt"
if grep -F 'not found' "$stage/deployed-ldd.txt" || grep -F "$qt_dir/" "$stage/deployed-ldd.txt"; then
    exit 1
fi

cat > "$appdir/AppRun" <<'SH'
#!/bin/sh
appdir=$(dirname "$(readlink -f "$0")")
exec "$appdir/usr/bin/MarkdownReader" "$@"
SH
chmod +x "$appdir/AppRun"
cat > "$appdir/markdown-reader.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=Markdown Reader
Exec=MarkdownReader %f
Icon=markdown-reader
Categories=Utility;Viewer;
MimeType=text/markdown;
Terminal=false
DESKTOP
cat > "$appdir/markdown-reader.svg" <<'SVG'
<svg xmlns="http://www.w3.org/2000/svg" width="128" height="128" viewBox="0 0 128 128">
  <rect x="10" y="10" width="108" height="108" rx="16" fill="#1b55a0"/>
  <path d="M32 91V37l32 32 32-32v54" fill="none" stroke="#fff" stroke-width="10" stroke-linejoin="round"/>
</svg>
SVG
desktop-file-validate "$appdir/markdown-reader.desktop"

fetch() {
    local url=$1 destination=$2 sha=$3
    if [[ -n "${APPIMAGE_TOOLS_DIR:-}" ]]; then
        cp "$APPIMAGE_TOOLS_DIR/${url##*/}" "$destination"
    else
        curl --fail --location --retry 3 "$url" -o "$destination"
    fi
    printf '%s  %s\n' "$sha" "$destination" | sha256sum --check
}
mkdir -p "$(dirname "$package")"
if [[ "$package" == *.tar.gz ]]; then
    tar -C "$stage" -czf "$package" MarkdownReader.AppDir
    launch=("$appdir/AppRun")
else
    fetch https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage \
        "$stage/appimagetool" ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
    fetch https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64 \
        "$stage/runtime" 2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d
    chmod +x "$stage/appimagetool"
    ARCH=x86_64 VERSION="$version" "$stage/appimagetool" --appimage-extract-and-run --runtime-file "$stage/runtime" "$appdir" "$package"
    launch=("$package" --appimage-extract-and-run)
fi
file "$package"

# A successful GUI smoke run is still in the event loop when timeout ends it.
status=0
timeout 10s xvfb-run -a env QT_QPA_PLATFORM=xcb "${launch[@]}" "$example" || status=$?
if [[ "$status" != 124 ]]; then
    echo "Packaged GUI smoke run ended unexpectedly (exit $status)" >&2
    exit 1
fi
