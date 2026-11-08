#!/usr/bin/env bash
set -euo pipefail

build_dir=$1
qt_dir=$2
arch=$3
package=$4
stage=$(mktemp -d "$build_dir/deploy-macos.XXXXXX")
trap 'rm -rf -- "$stage"' EXIT
app="$stage/MarkdownReader.app"
ditto "$build_dir/MarkdownReader.app" "$app"
"$qt_dir/bin/macdeployqt" "$app" -always-overwrite -codesign=-

exe="$app/Contents/MacOS/MarkdownReader"
test -x "$exe"
file "$exe"
test "$(lipo -archs "$exe")" = "$arch"
for module in Core Gui Widgets; do
    test -f "$app/Contents/Frameworks/Qt$module.framework/Versions/A/Qt$module"
done
test -f "$app/Contents/PlugIns/platforms/libqcocoa.dylib"

while IFS= read -r -d '' binary; do
    if [[ "$(file -b "$binary")" != *Mach-O* ]]; then
        continue
    fi
    lipo "$binary" -verify_arch "$arch"
    deps=$(otool -L "$binary")
    load_commands=$(otool -l "$binary")
    printf '%s\n' "$deps"
    if printf '%s\n' "$deps" | awk '/^[[:space:]]+\// && $1 !~ /^\/(System\/Library|usr\/lib)\// { print; bad=1 } END { exit !bad }' ||
        printf '%s\n' "$load_commands" | awk '$1=="cmd" { rpath=($2=="LC_RPATH") } rpath && $1=="path" && $2 ~ /^\// && $2 !~ /^\/(System\/Library|usr\/lib)\// { print; bad=1 } END { exit !bad }'; then
        echo "Unexpected absolute dependency or runtime path in $binary" >&2
        exit 1
    fi
done < <(find "$app/Contents" -type f -print0)

codesign --verify --deep --strict "$app"
mkdir -p "$(dirname "$package")"
hdiutil create -ov -fs HFS+ -srcfolder "$stage" -format UDZO -volname "Markdown Reader" "$package"
hdiutil verify "$package"
