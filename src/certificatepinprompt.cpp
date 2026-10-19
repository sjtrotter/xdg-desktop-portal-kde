/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "certificatepinprompt.h"
#include "certificatepinfield.h"

#include <KLocalizedString>

using namespace Qt::StringLiterals;

CertificatePinPrompt::CertificatePinPrompt(XccContext *context, XccUiId id, QObject *parent)
    : QObject(parent)
    , m_context(context)
    , m_id(id)
{
}

QVariantMap CertificatePinPrompt::view() const { return m_view; }

bool CertificatePinPrompt::update(const XccPinView *view)
{
    if (!view || view->struct_size < XCC_PIN_VIEW_V1_SIZE || view->abi_version != XCC_ABI_VERSION) {
        return false;
    }
    if (m_settled) {
        return true;
    }
    if (!m_view.isEmpty() && m_view.value(u"protectedPath"_s).toBool() != view->protected_path) {
        return false;
    }
    QString purpose;
    if (view->struct_size < offsetof(XccPinView, generic_purpose) + sizeof(view->generic_purpose)) {
        purpose = QString::fromUtf8(view->purpose_display);
    } else {
        purpose = i18n("Use a certificate");
        if (!view->generic_purpose) {
            switch (view->purpose) {
            case XCC_PURPOSE_CLIENT_AUTH:
                purpose = i18n("Authenticate to a server");
                break;
            case XCC_PURPOSE_SIGNING:
                purpose = i18n("Sign a document or data");
                break;
            case XCC_PURPOSE_EMAIL:
                purpose = i18n("Use secure email");
                break;
            case XCC_PURPOSE_SSH:
                purpose = i18n("Authenticate an SSH connection");
                break;
            }
        }
    }
    QString retry = m_view.value(u"retry"_s).toString();
    const bool hasStatusCode = view->struct_size >= offsetof(XccPinView, status_code) + sizeof(view->status_code);
    const bool wantsFreshInput = hasStatusCode ? view->status_code != XCC_PIN_STATUS_NONE : view->status != nullptr;
    if (wantsFreshInput) {
        if (!hasStatusCode) {
            retry = QString::fromUtf8(view->status);
        } else {
            switch (view->status_code) {
            case XCC_PIN_STATUS_INCORRECT:
                retry = i18n("That PIN was not accepted. Try again.");
                break;
            case XCC_PIN_STATUS_REQUIRED:
                retry = i18n("Enter the PIN for this token.");
                break;
            case XCC_PIN_STATUS_TOO_LONG:
                retry = i18n("PIN exceeds the supported length.");
                break;
            case XCC_PIN_STATUS_MEMORY_UNAVAILABLE:
                retry = i18n("The PIN could not be held in secure memory.");
                break;
            default:
                retry = i18n("The PIN could not be submitted. Try again.");
            }
        }
        m_submitted = false;
        Q_EMIT freshInput();
    }
    const auto hint = view->locked ? i18n("The PIN is locked.")
        : view->final_try          ? i18n("This is the final PIN attempt. Another "
                                          "incorrect PIN will lock the token.")
        : view->count_low          ? i18n("The PIN has been entered incorrectly. Only a "
                                          "few attempts may remain.")
                                   : QString();
    const bool tokenFallback = view->struct_size >= offsetof(XccPinView, display_flags) + sizeof(view->display_flags)
        && (view->display_flags & XCC_DISPLAY_TOKEN_FALLBACK);
    m_view = {
        { u"token"_s, tokenFallback ? i18n("Unnamed token") : QString::fromUtf8(view->token_label) },
        { u"reader"_s, QString::fromUtf8(view->reader) },
        { u"caller"_s, view->caller_display && *view->caller_display ? QString::fromUtf8(view->caller_display) : i18n("An unidentified application") },
        { u"purpose"_s, purpose },
        { u"authentication"_s,
            view->context_specific ? i18n("Enter your PIN to authorize this operation.") : i18n("Enter your PIN to unlock the security token.") },
        { u"contextSpecific"_s, view->context_specific },
        { u"protectedPath"_s, view->protected_path },
        { u"busy"_s, view->busy || m_submitted },
        { u"countLow"_s, view->count_low },
        { u"finalTry"_s, view->final_try },
        { u"locked"_s, view->locked },
        { u"hint"_s, hint },
        { u"retry"_s, retry },
        { u"warningPending"_s, m_warningPending },
        { u"ready"_s, m_presented },
    };
    if (view->busy) {
        Q_EMIT discardInput();
    }
    Q_EMIT changed();
    return true;
}

bool CertificatePinPrompt::warning(const XccWarning *warning)
{
    if (!warning || warning->struct_size < sizeof(*warning) || warning->abi_version != XCC_ABI_VERSION || warning->code != XCC_WARNING_FINAL_TRY
        || !m_view.value(u"protectedPath"_s).toBool()) {
        return false;
    }
    if (m_settled || m_submitted) {
        return true;
    }
    m_warning = warning->warning_id;
    m_warningPending = true;
    m_view[u"warningPending"_s] = true;
    m_view[u"finalTry"_s] = true;
    m_view[u"hint"_s] = i18n("This is the final PIN attempt. Another incorrect "
                             "PIN will lock the token.");
    Q_EMIT changed();
    return true;
}

void CertificatePinPrompt::presented()
{
    if (m_settled || m_presented) {
        return;
    }
    m_presented = true;
    m_view[u"ready"_s] = true;
    Q_EMIT changed();
    // This acknowledges typed prompts too. For protected paths the core starts
    // NULL-PIN login automatically, or delivers the final-try warning first.
    xcc_pin_ready(m_context, m_id);
}

void CertificatePinPrompt::setBusy(bool busy)
{
    m_submitted = busy;
    m_view[u"busy"_s] = busy;
    m_view[u"retry"_s] = QString();
    Q_EMIT changed();
}

void CertificatePinPrompt::submit(CertificatePinField *field)
{
    if (!m_presented || m_settled || m_view.value(u"busy"_s).toBool() || m_view.value(u"locked"_s).toBool()) {
        return;
    }
    if (m_view.value(u"protectedPath"_s).toBool()) {
        if (m_warningPending) {
            m_warningPending = false;
            m_view[u"warningPending"_s] = false;
            setBusy(true); // Latch before replying: repeated Continue spends no attempt.
            xcc_warning_reply(m_context, m_id, m_warning, true);
        }
    } else if (field && field->maskedCount()) {
        setBusy(true); // Disable both input and submission before transfer.
        if (!field->submit(m_context, m_id)) {
            setBusy(false);
        }
    }
}

void CertificatePinPrompt::conceal()
{
    m_settled = true;
    m_view[u"ready"_s] = false;
    Q_EMIT discardInput();
    Q_EMIT changed();
}

void CertificatePinPrompt::cancel()
{
    if (m_settled) {
        return;
    }
    conceal();
    if (m_warningPending) {
        m_warningPending = false;
        xcc_warning_reply(m_context, m_id, m_warning, false);
    }
    xcc_ui_cancel(m_context, m_id);
}

#include "moc_certificatepinprompt.cpp"
