#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Run an adaptor test with a private SoftHSM store; no host token configuration.
set -euo pipefail

if [ "$#" -eq 0 ]; then
    echo "Usage: XDP_CERTIFICATE_REFERENCE_ADAPTER=/path/to/certificate-reference-adapter $0 TEST [ARGS...]" >&2
    exit 2
fi
skip() { echo "SKIP: $*" >&2; exit 77; }
for tool in softhsm2-util pkcs11-tool certtool; do
    command -v "$tool" >/dev/null || skip "$tool is unavailable"
done
if [ "${CERTIFICATE_TEST_REQUIRE_REFERENCE:-1}" = 1 ]; then
    if [ -z "${XDP_CERTIFICATE_REFERENCE_ADAPTER:-}" ]; then
        skip "set XDP_CERTIFICATE_REFERENCE_ADAPTER to the certificate repo's build/tests/certificate-reference-adapter"
    fi
    if [ ! -x "$XDP_CERTIFICATE_REFERENCE_ADAPTER" ]; then
        echo "Reference adapter is not executable: $XDP_CERTIFICATE_REFERENCE_ADAPTER" >&2
        exit 1
    fi
fi
if [ "${CERTIFICATE_TEST_ALWAYS_AUTH:-0}" = 1 ]; then
    command -v openssl >/dev/null || skip "openssl is unavailable"
fi
if [ -z "${SOFTHSM_MODULE:-}" ]; then
    for module in /usr/lib64/pkcs11/libsofthsm2.so /usr/lib64/softhsm/libsofthsm2.so \
        /usr/lib/softhsm/libsofthsm2.so /usr/lib/x86_64-linux-gnu/softhsm/libsofthsm2.so; do
        if [ -f "$module" ]; then
            SOFTHSM_MODULE="$module"
            break
        fi
    done
fi
[ -f "${SOFTHSM_MODULE:-}" ] || skip "libsofthsm2.so is unavailable; set SOFTHSM_MODULE"
export SOFTHSM_MODULE

umask 077
SOFTHSM_FIXTURE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/kde-certificate-softhsm.XXXXXXXX")
export SOFTHSM_FIXTURE_DIR
trap 'rm -rf -- "$SOFTHSM_FIXTURE_DIR"' EXIT
mkdir "$SOFTHSM_FIXTURE_DIR/tokens"
export SOFTHSM2_CONF="$SOFTHSM_FIXTURE_DIR/softhsm2.conf"
cat >"$SOFTHSM2_CONF" <<EOF
directories.tokendir = $SOFTHSM_FIXTURE_DIR/tokens
objectstore.backend = file
log.level = ERROR
slots.removable = false
EOF
printf '%s' "$SOFTHSM_MODULE" >"$SOFTHSM_FIXTURE_DIR/module-path"
# A control character makes the wire label differ from the sanitized UI label.
label=$'Portal\tTest Token'
softhsm2-util --module "$SOFTHSM_MODULE" --init-token --slot 0 --label "$label" \
    --so-pin 3737 --pin 123456 >/dev/null
cat >"$SOFTHSM_FIXTURE_DIR/client.tmpl" <<EOF
organization = "Example Org"
cn = "Portal Test User (RSA)"
serial = 011
expiration_days = 3650
signing_key
tls_www_client
EOF
certtool --generate-privkey --key-type=rsa --bits=2048 --pkcs8 --no-text \
    --outfile "$SOFTHSM_FIXTURE_DIR/client.p8" 2>/dev/null
certtool --generate-self-signed --load-privkey "$SOFTHSM_FIXTURE_DIR/client.p8" \
    --template "$SOFTHSM_FIXTURE_DIR/client.tmpl" --outder \
    --outfile "$SOFTHSM_FIXTURE_DIR/portal-test-rsa.der" 2>/dev/null
# Test-only PINs, matching the certificate repository's fixture approach.
if [ "${CERTIFICATE_TEST_ALWAYS_AUTH:-0}" = 1 ]; then
    # Import the same certificate's key with per-operation authentication.
    openssl rsa -in "$SOFTHSM_FIXTURE_DIR/client.p8" -outform DER -traditional \
        -out "$SOFTHSM_FIXTURE_DIR/client-key.der" 2>/dev/null
    pkcs11-tool --module "$SOFTHSM_MODULE" --token-label "$label" --login --pin 123456 \
        --write-object "$SOFTHSM_FIXTURE_DIR/client-key.der" --type privkey --id 01 \
        --label portal-test-rsa --usage-sign --private --sensitive --always-auth >/dev/null
    rm -f "$SOFTHSM_FIXTURE_DIR/client-key.der"
else
    softhsm2-util --module "$SOFTHSM_MODULE" --import "$SOFTHSM_FIXTURE_DIR/client.p8" \
        --token "$label" --label portal-test-rsa --id 01 --pin 123456 >/dev/null
fi
pkcs11-tool --module "$SOFTHSM_MODULE" --token-label "$label" --login --pin 123456 \
    --write-object "$SOFTHSM_FIXTURE_DIR/portal-test-rsa.der" --type cert --id 01 --label portal-test-rsa >/dev/null
rm -f "$SOFTHSM_FIXTURE_DIR/client.p8" "$SOFTHSM_FIXTURE_DIR/client.tmpl"
"$@"
