/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once
#include <cstring>
#include <xdp-certificate/ui.h>

// Link-time fake coordinator: consume secrets synchronously without retaining a
// PIN string. Script SDK view callbacks in tests, including worker settlement.
struct CertificateFakePinCore {
    int ready = 0, submitted = 0, warnings = 0, cancelled = 0, failed = 0;
    int allocated = 0, freed = 0;
    bool matched = false, accepted = false;
    const char *expected = "123456";
    size_t length = 0;
    uint64_t warning = 0;
    XccUiId id = 0;
};
static CertificateFakePinCore fakePin;
inline XccContext *fakePinContext() { return reinterpret_cast<XccContext *>(quintptr(1)); }

extern "C" XccSecret *__real_xcc_secret_new();
extern "C" void __real_xcc_secret_free(XccSecret *);
extern "C" XccSecret *__wrap_xcc_secret_new()
{
    ++fakePin.allocated;
    return __real_xcc_secret_new();
}
extern "C" void __wrap_xcc_secret_free(XccSecret *secret)
{
    if (secret) {
        ++fakePin.freed;
    }
    __real_xcc_secret_free(secret);
}
extern "C" void __wrap_xcc_pin_ready(XccContext *, XccUiId id)
{
    ++fakePin.ready;
    fakePin.id = id;
}
extern "C" void __wrap_xcc_pin_submit(XccContext *, XccUiId id, XccSecret *secret)
{
    ++fakePin.submitted;
    fakePin.id = id;
    fakePin.length = xcc_secret_length(secret);
    fakePin.matched = secret && std::strcmp(xcc_secret_view(secret), fakePin.expected) == 0;
    __wrap_xcc_secret_free(secret); // Consumes ownership, as the SDK does.
}
extern "C" void __wrap_xcc_warning_reply(XccContext *, XccUiId id, uint64_t warning, bool accepted)
{
    ++fakePin.warnings;
    fakePin.id = id;
    fakePin.warning = warning;
    fakePin.accepted = accepted;
}
extern "C" void __wrap_xcc_ui_cancel(XccContext *, XccUiId id)
{
    ++fakePin.cancelled;
    fakePin.id = id;
}
extern "C" void __wrap_xcc_ui_failed(XccContext *, XccUiId, XccErrorCode) { ++fakePin.failed; }
