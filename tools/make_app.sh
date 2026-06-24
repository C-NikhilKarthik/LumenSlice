#!/usr/bin/env bash
# Package LumenSlice into a NOTARIZED, Gatekeeper-clean macOS .dmg (and .app).
#
# Pipeline:
#   build -> assemble .app -> sign (Developer ID + hardened runtime)
#         -> notarize .app -> staple -> build .dmg -> sign -> notarize -> staple.
#
# The result opens with a plain double-click on any Apple-Silicon Mac: no
# right-click -> Open, no `xattr` dance, and it passes `spctl` even offline.
#
# DCMTK is linked statically (see Package.swift), so the only thing we carry
# alongside the binary is DCMTK's data dictionary (required to parse
# Implicit-VR DICOM).
#
# Output: dist/LumenSlice.app  and  dist/LumenSlice.dmg   <-- send the .dmg
#
# ---------------------------------------------------------------------------
# ONE-TIME PREREQUISITES (see tools/NOTARIZE_SETUP.md for the click-by-click):
#   1. A PAID Apple Developer Program membership ($99/yr).
#   2. A "Developer ID Application" certificate installed in your login keychain.
#   3. A stored notarytool keychain profile (default name: lumenslice-notary):
#        xcrun notarytool store-credentials lumenslice-notary \
#          --apple-id <you@example.com> --team-id <TEAMID> \
#          --password <app-specific-password>
#
# Env overrides:
#   SIGN_IDENTITY   full "Developer ID Application: ..." string (else auto-detected)
#   NOTARY_PROFILE  notarytool keychain profile name (default: lumenslice-notary)
#   ADHOC=1         skip Developer ID + notarization; ad-hoc sign only.
#                   Builds a runnable .dmg but it is NOT Gatekeeper-clean off this
#                   Mac (recipient must right-click -> Open or strip quarantine).
#                   Use this only to test packaging without a Developer ID.
# ---------------------------------------------------------------------------
set -euo pipefail
cd "$(dirname "$0")/.."

APP_NAME="LumenSlice"
BUNDLE_ID="com.flamapp.lumenslice"
VERSION="0.1.0"
NOTARY_PROFILE="${NOTARY_PROFILE:-lumenslice-notary}"
ADHOC="${ADHOC:-0}"

# Always clean up temp artifacts, even if a later step aborts under `set -e`.
STAGING=""
NZIP=""
cleanup() {
    [ -n "$STAGING" ] && rm -rf "$STAGING"
    [ -n "$NZIP" ] && rm -f "$NZIP"
    return 0
}
trap cleanup EXIT

# Auto-detect the versioned DCMTK share dir (e.g. dcmtk-3.6.9, dcmtk-3.7.0) so we
# always bundle the real dicom.dic regardless of the installed DCMTK version.
DICT_DIR="$(ls -d "$(brew --prefix dcmtk)"/share/dcmtk-* 2>/dev/null | sort -V | tail -1)"
if [ -z "$DICT_DIR" ] || [ ! -d "$DICT_DIR" ]; then
    cat >&2 <<MSG

ERROR: Could not locate the DCMTK data dictionary directory.
Expected something like: $(brew --prefix dcmtk 2>/dev/null)/share/dcmtk-<version>

The DICOM data dictionary (dicom.dic) is required to parse Implicit-VR DICOM and
must be bundled into the .app. Install DCMTK via Homebrew and retry:

    brew install dcmtk

If DCMTK is installed in a non-standard location, set DICT_DIR to the directory
containing dicom.dic before running this script.

MSG
    exit 1
fi

# ---- resolve the signing identity ------------------------------------------
if [ "$ADHOC" = "1" ]; then
    SIGN_IDENTITY="-"
    echo "==> ADHOC=1: ad-hoc signing only (NOT Gatekeeper-clean off this Mac)."
else
    if [ -z "${SIGN_IDENTITY:-}" ]; then
        # Pull the human-readable name out of `security find-identity` output:
        #   1) ABCD...123 "Developer ID Application: Foo (TEAMID)"
        SIGN_IDENTITY="$(security find-identity -v -p codesigning \
            | grep "Developer ID Application" | head -1 \
            | sed -E 's/^[[:space:]]*[0-9]+\)[[:space:]]+[0-9A-Fa-f]+[[:space:]]+"(.*)"$/\1/')"
    fi
    if [ -z "$SIGN_IDENTITY" ]; then
        cat >&2 <<'MSG'

ERROR: No "Developer ID Application" certificate found in your keychain.

Notarized distribution requires one (a paid Apple Developer membership). To set
it up, follow tools/NOTARIZE_SETUP.md, then re-run this script. In short:

  - Create the cert: Xcode > Settings > Accounts > (your team) >
    Manage Certificates > "+" > "Developer ID Application".
  - Store notarytool credentials:
      xcrun notarytool store-credentials lumenslice-notary \
        --apple-id <you@example.com> --team-id <TEAMID> \
        --password <app-specific-password>

To build a non-notarized package for local testing instead, re-run with:
      ADHOC=1 tools/make_app.sh

MSG
        exit 1
    fi
    echo "==> Signing identity: $SIGN_IDENTITY"
fi

echo "==> Building release..."
swift build -c release --product "$APP_NAME"
BIN="$(swift build -c release --show-bin-path)/$APP_NAME"

APP="dist/$APP_NAME.app"
DMG="dist/$APP_NAME.dmg"
echo "==> Assembling ${APP}..."
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/$APP_NAME"

# Bundle the DICOM data dictionary the app points DCMDICTPATH at.
for d in dicom.dic private.dic acrnema.dic; do
    [ -f "$DICT_DIR/$d" ] && cp "$DICT_DIR/$d" "$APP/Contents/Resources/"
done

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>$APP_NAME</string>
  <key>CFBundleIdentifier</key><string>$BUNDLE_ID</string>
  <key>CFBundleName</key><string>$APP_NAME</string>
  <key>CFBundleDisplayName</key><string>$APP_NAME</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>LSMinimumSystemVersion</key><string>13.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSPrincipalClass</key><string>NSApplication</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.medical</string>
</dict></plist>
PLIST

# ---- sign the .app ---------------------------------------------------------
if [ "$ADHOC" = "1" ]; then
    echo "==> Ad-hoc code-signing..."
    codesign --force --deep --sign - "$APP"
else
    echo "==> Signing .app with Developer ID + hardened runtime..."
    # Sign inner-to-outer (no deprecated --deep). Hardened runtime + a secure
    # timestamp are both required for notarization.
    codesign --force --options runtime --timestamp \
        --sign "$SIGN_IDENTITY" "$APP/Contents/MacOS/$APP_NAME"
    codesign --force --options runtime --timestamp \
        --sign "$SIGN_IDENTITY" "$APP"
fi
codesign --verify --strict --verbose=2 "$APP" 2>&1 | sed 's/^/    /'

# ---- notarize + staple the .app --------------------------------------------
if [ "$ADHOC" != "1" ]; then
    echo "==> Notarizing .app (this uploads to Apple and waits; ~1-3 min)..."
    NZIP="dist/$APP_NAME-notarize.zip"  # cleaned up by the EXIT trap on any failure
    ditto -c -k --keepParent "$APP" "$NZIP"
    if ! xcrun notarytool submit "$NZIP" \
            --keychain-profile "$NOTARY_PROFILE" --wait; then
        echo "ERROR: notarization failed. Inspect the log with:" >&2
        echo "  xcrun notarytool history --keychain-profile $NOTARY_PROFILE" >&2
        echo "  xcrun notarytool log <submission-id> --keychain-profile $NOTARY_PROFILE" >&2
        exit 1  # EXIT trap removes $NZIP
    fi
    rm -f "$NZIP"; NZIP=""
    echo "==> Stapling the notarization ticket to the .app..."
    xcrun stapler staple "$APP"
fi

# ---- build the .dmg (drag-to-Applications layout) --------------------------
echo "==> Building ${DMG}..."
STAGING="$(mktemp -d)"
cp -R "$APP" "$STAGING/"
ln -s /Applications "$STAGING/Applications"
rm -f "$DMG"
hdiutil create -volname "$APP_NAME" -srcfolder "$STAGING" \
    -ov -format UDZO "$DMG" >/dev/null
rm -rf "$STAGING"; STAGING=""

# ---- sign + notarize + staple the .dmg -------------------------------------
if [ "$ADHOC" != "1" ]; then
    echo "==> Signing the .dmg..."
    codesign --force --timestamp --sign "$SIGN_IDENTITY" "$DMG"
    echo "==> Notarizing the .dmg (~1-3 min)..."
    if ! xcrun notarytool submit "$DMG" \
            --keychain-profile "$NOTARY_PROFILE" --wait; then
        echo "ERROR: DMG notarization failed (see notarytool log above)." >&2
        exit 1
    fi
    echo "==> Stapling the notarization ticket to the .dmg..."
    xcrun stapler staple "$DMG"
fi

# ---- verify ----------------------------------------------------------------
echo "==> Verifying with Gatekeeper..."
if [ "$ADHOC" != "1" ]; then
    # Expect: "accepted ... source=Notarized Developer ID"
    spctl -a -t exec -vv "$APP" 2>&1 | sed 's/^/    /' || true
    xcrun stapler validate "$DMG" 2>&1 | sed 's/^/    /' || true
else
    spctl -a -t exec -vv "$APP" 2>&1 | sed 's/^/    /' || true
fi

echo ""
echo "Done:"
echo "  $APP"
echo "  $DMG   ($(du -h "$DMG" | cut -f1))   <-- send this"
echo ""
if [ "$ADHOC" != "1" ]; then
    echo "This .dmg is notarized + stapled: the recipient just double-clicks it,"
    echo "drags LumenSlice to Applications, and opens it. No Gatekeeper warning."
else
    echo "ADHOC build: recipient must right-click -> Open on first launch, or run:"
    echo "  xattr -dr com.apple.quarantine /Applications/LumenSlice.app"
fi
echo ""
echo "Note: this bundle is arm64 (Apple Silicon). An Intel Mac can't run it."
