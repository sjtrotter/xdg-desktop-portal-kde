Certificate adaptor acquisition parity uses an isolated SoftHSM token and the
certificate repository's real GDBus reference adapter with a headless chooser.
Build that repository with its default service enabled, and install its updated
SDK in the KDE build container before building KDE with BUILD_CERTIFICATE_PORTAL=ON.
The helper is a test executable, not an installed SDK API.

Inside the container (softhsm, opensc and gnutls-utils installed):

```sh
export XDP_CERTIFICATE_REFERENCE_ADAPTER=/path/to/certificate/build/tests/certificate-reference-adapter
# Optional when the module is outside the usual system locations:
export SOFTHSM_MODULE=/path/to/libsofthsm2.so
ctest --test-dir build -R '^certificateacquisitiontest$' --output-on-failure
```

The CTest wrapper runs `certificate-softhsm-fixture.sh` through bash. To invoke it
directly without CTest:

```sh
bash autotests/certificate-softhsm-fixture.sh /path/to/certificateadaptortest successfulAcquisitionAndGrantedMalformedSign
```

The script creates a fresh private directory and SoftHSM configuration, initializes
a token (test user PIN 123456, SO PIN 3737), generates a 2048-bit RSA key and a
self-signed client-auth certificate with certtool, and imports the PKCS#8 key and
DER certificate under the same CKA_ID (01), using softhsm2-util and pkcs11-tool.
The token label contains a tab to distinguish original wire metadata from display
sanitization. It exports SOFTHSM_FIXTURE_DIR, SOFTHSM2_CONF and SOFTHSM_MODULE to the
test and removes the entire directory on exit. No OpenSSL or display is needed.
Missing tools, module or reference-helper setting cause CTest skip (exit 77).

The test starts its own private D-Bus daemon, exports the KDE adaptor with a real
SDK context and a fake UI that chooses the first RSA candidate, and launches the
reference adapter on the same bus and token store. Both acquisition dictionaries
are compared in full, with additional DER, chain, metadata, key and policy checks.
Three rows cover full SDK metadata, the original struct prefix and an incomplete
extension. The latter two must fall back to the SDK's sanitized display fields.
After each successful grant, malformed nested Sign parameters must produce the
reference's response 2 and `invalid_request` error without a PIN prompt.

The ordinary certificateadaptortest also accepts the certificate repository's
existing SOFTHSM_FIXTURE_DIR (including portal-test-rsa.der and module-path); its
acquisition rows skip cleanly without a fixture or reference helper.
