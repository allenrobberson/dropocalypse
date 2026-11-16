#!/usr/bin/env bash
# CI helper: imports the signing certificates from GitHub secrets into a temporary keychain,
# writes the App Store Connect API key, and downloads the Mac App Store provisioning profile.
# Exports SIGNING_READY=1 and the identity names to $GITHUB_ENV when everything is present.
set -euo pipefail

if [ -z "${MAC_DIST_P12:-}" ] || [ -z "${DEVELOPER_ID_P12:-}" ] || [ -z "${P12_PASSWORD:-}" ] || [ -z "${ASC_KEY_P8:-}" ]; then
  echo "Signing secrets not set; building unsigned (ad-hoc)."
  exit 0
fi

KEYCHAIN="$RUNNER_TEMP/signing.keychain-db"
KC_PASS="$(openssl rand -hex 16)"
security create-keychain -p "$KC_PASS" "$KEYCHAIN"
security set-keychain-settings -lut 21600 "$KEYCHAIN"
security unlock-keychain -p "$KC_PASS" "$KEYCHAIN"
for v in MAC_DIST_P12 MAC_INSTALLER_P12 DEVELOPER_ID_P12; do
  [ -n "${!v:-}" ] || continue
  echo "${!v}" | base64 --decode > "$RUNNER_TEMP/$v.p12"
  security import "$RUNNER_TEMP/$v.p12" -k "$KEYCHAIN" -P "$P12_PASSWORD" -T /usr/bin/codesign -T /usr/bin/productbuild -T /usr/bin/security
  rm -f "$RUNNER_TEMP/$v.p12"
done
security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$KC_PASS" "$KEYCHAIN" >/dev/null
security list-keychains -d user -s "$KEYCHAIN" $(security list-keychains -d user | tr -d '"')
security find-identity -v "$KEYCHAIN"

ident() { security find-identity -v -p "$2" "$KEYCHAIN" | grep -o "\"$1[^\"]*\"" | head -1 | tr -d '"'; }
DEVID="$(ident 'Developer ID Application' codesigning)"
DIST="$(ident 'Apple Distribution' codesigning)"
INST="$(security find-identity -v "$KEYCHAIN" | grep -o '"3rd Party Mac Developer Installer[^"]*"' | head -1 | tr -d '"')"

# App Store Connect API key (used by notarytool, altool and the profile download).
KEY_DIR="$HOME/.appstoreconnect/private_keys"; mkdir -p "$KEY_DIR"
KEY_FILE="$KEY_DIR/AuthKey_${ASC_KEY_ID}.p8"
# Rebuild a clean PEM from the pasted text: editors like TextEdit can turn the "-----" lines
# into long dashes or add stray spaces, so keep only the base64 body.
ASC_KEY_P8="$ASC_KEY_P8" python3 - "$KEY_FILE" <<'PY'
import os, re, sys, textwrap
raw = os.environ["ASC_KEY_P8"]
body = re.sub(r"(?i)[^A-Za-z0-9+/=]*(BEGIN|END)\s+(EC\s+)?PRIVATE\s+KEY[^A-Za-z0-9+/=]*", "\n", raw)
body = "".join(re.findall(r"[A-Za-z0-9+/=]+", body))
open(sys.argv[1], "w").write("-----BEGIN PRIVATE KEY-----\n" + "\n".join(textwrap.wrap(body, 64)) + "\n-----END PRIVATE KEY-----\n")
print("API key: %d base64 characters" % len(body))
PY
chmod 600 "$KEY_FILE"

# Provisioning profile for the App Store edition, fetched by name from the App Store Connect API.
PROFILE="$RUNNER_TEMP/appstore.provisionprofile"
if [ -n "${MAC_PROVISION_PROFILE:-}" ]; then
  echo "$MAC_PROVISION_PROFILE" | base64 --decode > "$PROFILE"
else
  python3 -m pip install --quiet --break-system-packages pyjwt cryptography 2>/dev/null || python3 -m pip install --quiet pyjwt cryptography
  python3 - "$KEY_FILE" "$PROFILE" <<'PY' || echo "Couldn't download the provisioning profile; the App Store build will be skipped."
import base64, json, os, sys, time, urllib.parse, urllib.request
import jwt
key = open(sys.argv[1]).read()
now = int(time.time())
token = jwt.encode({"iss": os.environ["ASC_ISSUER_ID"], "iat": now, "exp": now + 900, "aud": "appstoreconnect-v1"},
                   key, algorithm="ES256", headers={"kid": os.environ["ASC_KEY_ID"], "typ": "JWT"})
q = urllib.parse.urlencode({"filter[name]": "Dropocalypse Mac App Store", "filter[profileState]": "ACTIVE"})
req = urllib.request.Request("https://api.appstoreconnect.apple.com/v1/profiles?" + q,
                             headers={"Authorization": "Bearer " + token})
data = json.load(urllib.request.urlopen(req))["data"]
if not data:
    sys.exit("profile not found")
open(sys.argv[2], "wb").write(base64.b64decode(data[0]["attributes"]["profileContent"]))
print("Downloaded profile", data[0]["attributes"]["name"])
PY
fi

{
  echo "SIGNING_READY=1"
  echo "DEVID_IDENTITY=$DEVID"
  echo "DIST_IDENTITY=$DIST"
  echo "INSTALLER_IDENTITY=$INST"
  echo "NOTARY_KEY=$KEY_FILE"
  [ -s "$PROFILE" ] && echo "PROVISION_PROFILE=$PROFILE"
} >> "$GITHUB_ENV"
echo "Signing ready: ${DEVID:-none} / ${DIST:-none} / ${INST:-none}"
