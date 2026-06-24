# Notarizing LumenSlice (one-time setup)

`tools/make_app.sh` builds a **notarized, Gatekeeper-clean** `dist/LumenSlice.dmg`
that opens with a plain double-click on any Apple-Silicon Mac - no "app cannot be
opened" warning, no right-click -> Open, no `xattr` dance.

That requires two things this Mac does **not** have yet:

1. A **Developer ID Application** certificate (needs a *paid* Apple Developer
   Program membership - $99/yr; the free "Apple Development" cert already on this
   machine does **not** work for notarized distribution).
2. Stored `notarytool` credentials.

Do the three steps below once. After that, `tools/make_app.sh` runs the whole
sign -> notarize -> staple -> DMG pipeline by itself.

---

## 1. Get a "Developer ID Application" certificate

You must be the **Account Holder** or **Admin** of a paid team.

**Easiest (Xcode):**
Xcode -> Settings -> Accounts -> select your team -> **Manage Certificates...**
-> click **+** -> **Developer ID Application**. It installs into your login
keychain automatically.

**Or via the portal:** https://developer.apple.com/account/resources/certificates
-> **+** -> *Developer ID Application* -> upload a CSR
(Keychain Access -> Certificate Assistant -> Request a Certificate from a
Certificate Authority) -> download the `.cer` -> double-click to install.

Verify it landed:

```bash
security find-identity -v -p codesigning | grep "Developer ID Application"
```

You should see one line. The make script auto-detects it (or set
`SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)"`).

## 2. Create an app-specific password

notarytool cannot use your normal Apple password. Make a dedicated one:

https://appleid.apple.com -> **Sign-In & Security** -> **App-Specific Passwords**
-> **+** -> name it `lumenslice-notary`. Copy the `xxxx-xxxx-xxxx-xxxx` value.

## 3. Store the notarytool credentials in the keychain

Find your **Team ID** at https://developer.apple.com/account (Membership details),
or read it from the cert name in step 1 - it is the value in parentheses.

```bash
xcrun notarytool store-credentials lumenslice-notary \
  --apple-id "you@example.com" \
  --team-id "YOURTEAMID" \
  --password "xxxx-xxxx-xxxx-xxxx"
```

`lumenslice-notary` is the profile name the script expects (override with the
`NOTARY_PROFILE` env var if you pick a different one).

---

## Build the notarized DMG

```bash
tools/make_app.sh
```

It builds, signs with hardened runtime, notarizes the app and the DMG (each
upload waits ~1-3 min), staples both, and verifies. Ship `dist/LumenSlice.dmg`.

Confirm it is clean:

```bash
spctl -a -t exec -vv dist/LumenSlice.app    # -> accepted, source=Notarized Developer ID
xcrun stapler validate dist/LumenSlice.dmg  # -> The validate action worked!
```

## Fallback without a Developer ID

To test packaging without notarization, ad-hoc sign instead:

```bash
ADHOC=1 tools/make_app.sh
```

That DMG runs, but it is **not** Gatekeeper-clean off this Mac - the recipient
must right-click -> Open once, or run
`xattr -dr com.apple.quarantine /Applications/LumenSlice.app`.

## Troubleshooting notarization rejections

```bash
xcrun notarytool history --keychain-profile lumenslice-notary
xcrun notarytool log <submission-id> --keychain-profile lumenslice-notary
```

Common causes: missing hardened runtime (`--options runtime`), missing secure
timestamp (`--timestamp`), or an unsigned nested binary. The make script already
sets all of these.

## Note on architecture

The bundle is **arm64** (Apple Silicon) only. An Intel Mac cannot run it
regardless of notarization. To support Intel too you would build a universal
binary (`swift build --arch arm64 --arch x86_64`) and `lipo` them together.
