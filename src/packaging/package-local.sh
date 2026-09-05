#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repository_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)
stage_dir="$script_dir/data"
iscc="/c/Program Files (x86)/Inno Setup 6/ISCC.exe"
product_version=$(tr -d '\r\n' < "$repository_dir/VERSION")
setup_script=$(cygpath -w "$script_dir/setup.iss")

test -x "$repository_dir/dist/p2000t-cartridge-studio.exe"
test -x "$iscc"

cmake -E remove_directory "$stage_dir"
cmake -E make_directory "$stage_dir"
cmake -E copy "$repository_dir/dist/p2000t-cartridge-studio.exe" "$stage_dir/"
cmake -E copy_directory "$repository_dir/dist/firmware" "$stage_dir/firmware"
cmake -E copy_directory "$repository_dir/dist/tools" "$stage_dir/tools"
cmake -E copy "$repository_dir/src/assets/icon/p2000t-cartridge-studio.ico" "$stage_dir/"
cmake -E copy "$repository_dir/LICENSE" "$stage_dir/license.txt"
cmake -E copy "$repository_dir/VERSION" "$stage_dir/version.txt"

windeployqt6 --release --compiler-runtime --no-opengl-sw \
    --no-translations --include-plugins qmodernwindowsstyle \
    --dir "$stage_dir" "$stage_dir/p2000t-cartridge-studio.exe"

bash "$script_dir/deploy-mingw-dependencies.sh" "$stage_dir"

test -s "$stage_dir/Qt6Core.dll"
test -s "$stage_dir/libgcc_s_seh-1.dll"
test -s "$stage_dir/libstdc++-6.dll"
test -s "$stage_dir/libwinpthread-1.dll"
test -s "$stage_dir/styles/qmodernwindowsstyle.dll"

MSYS2_ARG_CONV_EXCL='*' \
    "$iscc" "/DAppVersion=$product_version" "$setup_script"

cmake -E copy \
    "$script_dir/installer/p2000t-cartridge-studio-windows-x64-setup.exe" \
    "$repository_dir/dist/p2000t-cartridge-studio-windows-x64-setup.exe"
