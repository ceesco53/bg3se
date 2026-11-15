#!/bin/bash
# Builds, signs, notarizes and (optionally) publishes the macOS Script Extender.
#
# One-time setup:
#   - a Developer ID Application certificate in your Keychain
#   - notarization credentials:  xcrun notarytool store-credentials <profile> --apple-id ... --team-id ...
#     (the profile name defaults to bg3se-notary; set BG3SE_NOTARY_PROFILE to use another,
#     either in the environment or in the git-ignored file macOS/Local.env)
# For each release, raise the version in macOS/VERSION first.
#
#   macOS/scripts/release.sh               build, sign, notarize, publish a GitHub release
#   macOS/scripts/release.sh --no-publish  stop after notarization; the archive stays in build/release
set -euo pipefail
cd "$(dirname "$0")/../.."

publish=1
[ "${1:-}" = "--no-publish" ] && publish=0

# Machine-specific settings stay out of the repository
[ -f macOS/Local.env ] && . macOS/Local.env
notary_profile=${BG3SE_NOTARY_PROFILE:-bg3se-notary}
version=$(tr -d '[:space:]' < macOS/VERSION)
tag="macos-v$version"
out=build/release
name="BG3ScriptExtender-macOS-$version"

fail() { echo "error: $*" >&2; exit 1; }
command -v cmake >/dev/null || fail "cmake is required (brew install cmake)"
identity=$(security find-identity -v -p codesigning | awk -F'"' '/Developer ID Application/ {print $2; exit}')
[ -n "$identity" ] || fail "no Developer ID Application certificate in the Keychain"
if [ $publish -eq 1 ]; then
  command -v gh >/dev/null || fail "the GitHub CLI is required (brew install gh)"
  gh release view "$tag" >/dev/null 2>&1 && fail "release $tag already exists; raise the version in macOS/VERSION"
  [ -z "$(git status --porcelain)" ] || fail "commit your changes first, so the release matches the repository"
fi

echo "==> Building Script Extender $version"
cmake -S macOS -B "$out/cmake" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$out/cmake" -j --target BG3ScriptExtender SEPlatformTests SELoadTest | tail -1
"$out/cmake/SEPlatformTests" | tail -1
"$out/cmake/SELoadTest" "$out/cmake/libBG3ScriptExtender.dylib" >/dev/null || fail "the built library failed its load test"

# Don't ship a build whose symbol table no longer matches the installed game
macOS/scripts/check-symbols.py || fail "the symbol mappings do not match the installed game"

echo "==> Signing as $identity"
stage="$out/$name"
rm -rf "$stage" "$out/$name.zip"
mkdir -p "$stage"
cp "$out/cmake/libBG3ScriptExtender.dylib" "$stage/"
cp macOS/scripts/bg3se-launch.sh "$stage/"
cp macOS/README.md "$stage/README.md"
cp LICENSE "$stage/LICENSE"
strip -x "$stage/libBG3ScriptExtender.dylib"
codesign --force --timestamp --options runtime --sign "$identity" "$stage/libBG3ScriptExtender.dylib"
codesign --verify --strict --verbose=2 "$stage/libBG3ScriptExtender.dylib"

echo "==> Notarizing"
ditto -c -k --keepParent "$stage" "$out/$name.zip"
xcrun notarytool submit "$out/$name.zip" --keychain-profile "$notary_profile" --wait
# A bare dylib can't carry a stapled ticket; Gatekeeper looks the notarization up online instead.

if [ $publish -eq 0 ]; then
  echo "Done: $out/$name.zip (signed and notarized, not published)"
  exit 0
fi

echo "==> Publishing $tag"
repository=$(gh repo view --json nameWithOwner --jq .nameWithOwner)
gh release create "$tag" "$out/$name.zip" --repo "$repository" --title "Script Extender for macOS $version" --generate-notes --prerelease
echo "Done: https://github.com/$repository/releases/tag/$tag"
