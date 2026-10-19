Certificate portal PIN policy (open KDE review question)
------------------------------------------------------

The KDE backend uses scoped PIN protection. Retained PIN bytes live only in the
SDK's XccSecret, which uses mlock and MADV_DONTDUMP; its pages are excluded from
dumps. mlock is best effort (the SDK logs failure); MADV_DONTDUMP depends on kernel
support and successful madvise. Conversion buffers and removed bytes are wiped;
submission transfers ownership, and hide/destruction discard the secret. No PIN
text is retained in QML, QString/QVariant properties, signals, clipboard, undo or
IME state, or logged by certificate code. Key text is read synchronously, and
non-navigation key presses and releases are consumed in the C++ field.

Unlike the standalone xdg-desktop-portal-certificate service (non-dumpable, no
cores), the KDE backend preserves normal dumpability, core limits and KCrash.
A crash dump or same-user ptrace of this backend can contain transient PIN copies
from Qt key events and the input stack despite XccSecret pages being excluded
from dumps. DrKonqi/KCrash crash reports may contain PIN fragments typed shortly
before a crash. Scoped protection does not prevent ptrace from reading live PINs.

Open question for KDE reviewers: accept this residual risk, apply process-wide
hardening, or collect PINs in a separate helper process. Process-wide hardening
breaks Documents.Info host/KIOFuse path translation and portal-backed Settings,
directory dialogs and OpenURI; it also removes crash reports for every portal.
A zero hard RLIMIT_CORE is inherited by launched children (including kbuildsycoca,
Discover and printing commands), restricting their crash diagnostics too; actual
dump behaviour also depends on core_pattern. A separate PIN helper would keep
these restrictions outside the desktop-wide backend, but is not implemented.

Certificate configuration
-------------------------

The backend reads `xdg-desktop-portal-kderc`, group `[Certificate]`, once at
startup. Restart the backend after changing it. `Modules` is a KConfig string
list (comma-separated paths, with KConfig escaping); an absent or empty list
uses p11-kit's registered modules. `AllowSoftwareTokens` is a boolean, default
`false`. For a disposable SoftHSM test, for example:

```ini
[Certificate]
Modules=/usr/lib64/pkcs11/libsofthsm2.so
AllowSoftwareTokens=true
```

These are user settings with the same trust level as the standalone service's
`--module` and `--allow-software-tokens` flags. Module paths load code in the
backend. Neither setting is read from environment variables. As with the
standalone service, explicitly selected modules can expose software tokens even
when `AllowSoftwareTokens=false`; the SDK's existing module policy is unchanged.

`certificateconfigtest` uses a temporary `XDG_CONFIG_HOME` (and config search
path) to check defaults, list parsing, both overrides and the startup snapshot.
It needs no display, bus or PKCS#11 module.

Test isolation
--------------

All certificate UI CTest entries, including the acquisition and both signing
fixtures, run through certificate-test-session.sh before QApplication starts.
It uses dbus-run-session with certificate-test-bus.conf: no service directories,
session-config includes or systemd activation. The adaptor's additional private
bus uses the same configuration. Real frontend/document portals cannot activate;
the adaptor still registers its fake frontend and tests the real SDK on its bus.
Each UI test checks that its bus has no activatable services; the adaptor checks
both buses. The bridge sends typing and releases through the actual QQuickWindow
and checks that they do not propagate to ancestor items.

The wrapper unsets QT_QPA_PLATFORMTHEME and QT_QPA_GENERIC_PLUGINS, sets
QT_NO_XDG_DESKTOP_PORTAL=1, and selects offscreen/software rendering. In Qt 6.11,
[QDesktopUnixServices](https://github.com/qt/qtbase/blob/6.11/src/gui/platform/unix/qdesktopunixservices.cpp)
honours that variable for its portal probe. The
[explicit portal theme](https://github.com/qt/qtbase/blob/6.11/src/plugins/platformthemes/xdgdesktopportal/qxdgdesktopportaltheme.cpp)
still probes, so the inherited theme selection must also be cleared.

Run UI binaries through the same wrapper when invoking them manually:

```sh
bash autotests/certificate-test-session.sh /path/to/certificatepinbridgetest
```

Acquisition fixture
-------------------

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
bash autotests/certificate-test-session.sh bash autotests/certificate-softhsm-fixture.sh /path/to/certificateadaptortest successfulAcquisitionAndGrantedMalformedSign
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
