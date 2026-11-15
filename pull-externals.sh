#!/bin/bash
# macOS counterpart of pull-externals.bat: fetches the third-party sources the macOS build needs
# into External/ (git-ignored). Safe to re-run; existing checkouts are left alone.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p External
cd External

# clone <dir> <url> [branch-or-tag] [commit]
clone() {
  local dir=$1 url=$2 ref=${3:-} commit=${4:-}
  if [ -d "$dir/.git" ]; then
    echo "==> $dir (already present)"
  else
    echo "==> $dir"
    if [ -n "$ref" ]; then
      git clone --quiet --depth 1 --branch "$ref" "$url" "$dir"
    else
      git clone --quiet "$url" "$dir"
    fi
  fi
  if [ -n "$commit" ]; then
    git -C "$dir" -c advice.detachedHead=false checkout --quiet "$commit"
  fi
}

# Versions mirror pull-externals.bat.
clone bcdec     https://github.com/iOrange/bcdec        "" 80859ed3b7afb1c527a2a99d70c61457bea72d0c
clone FastLZ    https://github.com/ariya/FastLZ         0.5.0
clone glm       https://github.com/g-truc/glm           1.0.3
clone imgui     https://github.com/Norbyte/imgui        "" 0c44880d5f783e83d0e086a9dfc5d8caf055051a
clone lua       https://github.com/Norbyte/lua-dos
clone lz4       https://github.com/lz4/lz4              v1.10.0
clone rapidjson https://github.com/tencent/rapidjson    "" 24b5e7a8b27f42fa16b96fc70aade9106cf7102f
clone stb       https://github.com/nothings/stb         "" 7023e273f1513b68b8d4086077c6faca555d50df
clone tinycrypt https://github.com/intel/tinycrypt
clone ZipLib    https://github.com/Norbyte/ZipLib

clone optick    https://github.com/Norbyte/optick
clone SDL       https://github.com/libsdl-org/SDL       release-2.30.6
clone protobuf  https://github.com/protocolbuffers/protobuf v36.1

# NoesisGUI SDK headers (the game's UI library); same archive the Windows build uses
noesis=NoesisGUI-NativeSDK-win-3.1.7-Indie
if [ ! -d "$noesis/Include" ]; then
  echo "==> $noesis"
  mkdir -p _dl
  curl -sSL --fail -o _dl/noesis.zip "http://bg3se-updates.norbyte.dev/Stuff/$noesis.zip"
  unzip -q -o _dl/noesis.zip -d .
  # The SDK declares an override that doesn't override anything (GetCopy is the virtual).
  # MSVC never diagnoses it in an uninstantiated template; clang does.
  sed -i '' 's/void Get(const void\* ptr, void\* dest) const override;/void GetCopy(const void* ptr, void* dest) const override;/' \
    "$noesis/Include/NsCore/TypePropertyImpl.h"
  sed -i '' 's/void TypePropertyFunction<C,T>::Get(const void\* ptr, void\* dest) const/void TypePropertyFunction<C,T>::GetCopy(const void* ptr, void* dest) const/' \
    "$noesis/Include/NsCore/TypePropertyImpl.inl"
fi

# protobuf (lite runtime + protoc), built statically for both architectures
if [ ! -f protobuf-build/libprotobuf-lite.a ] || [ ! -x protobuf-build/protoc ]; then
  command -v cmake >/dev/null || { echo "error: cmake is required (brew install cmake)" >&2; exit 1; }
  echo "==> building protobuf (this takes a few minutes)"
  cmake -S protobuf -B protobuf-build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=23 \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_CXX_VISIBILITY_PRESET=hidden -DCMAKE_VISIBILITY_INLINES_HIDDEN=ON \
    -Dprotobuf_BUILD_TESTS=OFF -Dprotobuf_BUILD_SHARED_LIBS=OFF -Dprotobuf_FORCE_FETCH_DEPENDENCIES=ON > _dl-protobuf-configure.log 2>&1
  cmake --build protobuf-build --target libprotobuf-lite protoc -j > _dl-protobuf-build.log 2>&1
fi

echo "Externals are ready in $(pwd)"
