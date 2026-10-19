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

void CertificatePinPrompt::updateMessage()
{
    const bool locked = m_view.value(u"locked"_s).toBool();
    const bool finalTry = m_view.value(u"finalTry"_s).toBool();
    const auto retry = m_view.value(u"retry"_s).toString();
    QString message;
    if (locked) {
        message = i18n("The PIN is locked. Unlock it with your PUK or ask your administrator.");
    } else if (m_incorrect && finalTry) {
        message = i18n("Wrong PIN. Another wrong PIN locks the token.");
    } else if (m_incorrect && m_view.value(u"countLow"_s).toBool()) {
        message = i18n("Wrong PIN. Few attempts may remain.");
    } else {
        // A status replaces a hint; hints appear only without a status.
        message = retry.isEmpty() ? m_view.value(u"hint"_s).toString() : retry;
    }
    m_view[u"message"_s] = message;
    m_view[u"messageError"_s] = locked || finalTry || m_incorrect || !retry.isEmpty();
}

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
    const bool callerFallback = view->struct_size >= offsetof(XccPinView, display_flags) + sizeof(view->display_flags)
        && (view->display_flags & XCC_DISPLAY_CALLER_FALLBACK);
    const auto caller = callerFallback || !view->caller_display || !*view->caller_display ? i18n("An unknown app") : QString::fromUtf8(view->caller_display);
    QString body = i18n("%1 needs it to use your certificate.", caller);
    if (view->struct_size >= offsetof(XccPinView, generic_purpose) + sizeof(view->generic_purpose) && !view->generic_purpose) {
        switch (view->purpose) {
        case XCC_PURPOSE_CLIENT_AUTH:
            body = i18n("%1 needs it to identify you to a website.", caller);
            break;
        case XCC_PURPOSE_SIGNING:
            body = i18n("%1 needs it to sign documents and data.", caller);
            break;
        case XCC_PURPOSE_EMAIL:
            body = i18n("%1 needs it to use your email certificate.", caller);
            break;
        case XCC_PURPOSE_SSH:
            body = i18n("%1 needs it to sign in to an SSH server.", caller);
            break;
        }
    }
    if (view->context_specific) {
        body += u' ' + i18n("Your security token asks for the PIN before each signature.");
    }
    const auto heading = view->locked ? i18n("PIN locked")
        : view->final_try ? (view->protected_path ? i18n("Last PIN attempt on the reader") : i18n("Last PIN attempt"))
        : view->protected_path
        ? (view->context_specific ? i18n("Enter your PIN again on the reader") : i18n("Enter your PIN on the reader"))
        : (view->context_specific ? i18n("Enter your PIN again") : i18n("Enter your PIN"));
    QString retry = m_view.value(u"retry"_s).toString();
    const bool hasStatusCode = view->struct_size >= offsetof(XccPinView, status_code) + sizeof(view->status_code);
    const bool wantsFreshInput = hasStatusCode ? view->status_code != XCC_PIN_STATUS_NONE : view->status != nullptr;
    if (wantsFreshInput) {
        m_incorrect = hasStatusCode && view->status_code == XCC_PIN_STATUS_INCORRECT;
        if (!hasStatusCode) {
            retry = QString::fromUtf8(view->status);
        } else {
            switch (view->status_code) {
            case XCC_PIN_STATUS_INCORRECT:
                retry = i18n("Wrong PIN. Try again.");
                break;
            case XCC_PIN_STATUS_REQUIRED:
                retry = i18n("Enter your PIN.");
                break;
            case XCC_PIN_STATUS_TOO_LONG:
                retry = i18n("This PIN is too long.");
                break;
            case XCC_PIN_STATUS_MEMORY_UNAVAILABLE:
                retry = i18n("The PIN could not be kept in secure memory.");
                break;
            default:
                retry = i18n("The PIN could not be sent to the token. Try again.");
            }
        }
        m_submitted = false;
        Q_EMIT freshInput();
    }
    const auto hint = view->locked ? i18n("The PIN is locked. Unlock it with your PUK or ask your administrator.")
        : view->final_try ? i18n("Another wrong PIN locks the token.")
        : view->count_low ? i18n("A wrong PIN was entered before. Few attempts may remain.") : QString();
    const bool tokenFallback = view->struct_size >= offsetof(XccPinView, display_flags) + sizeof(view->display_flags)
        && (view->display_flags & XCC_DISPLAY_TOKEN_FALLBACK);
    const auto token = tokenFallback || !view->token_label || !*view->token_label ? i18n("Unnamed token") : QString::fromUtf8(view->token_label);
    const auto tokenLine = tokenFallback && view->reader && *view->reader ? i18n("Security token “%1” in %2", token, QString::fromUtf8(view->reader))
        : i18n("Security token “%1”", token);
    m_view = {
        { u"heading"_s, heading },
        { u"body"_s, body },
        { u"tokenLine"_s, tokenLine },
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
    updateMessage();
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
    m_view[u"heading"_s] = m_view.value(u"locked"_s).toBool() ? i18n("PIN locked") : i18n("Last PIN attempt on the reader");
    m_view[u"hint"_s] = i18n("Another wrong PIN locks the token.");
    updateMessage();
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
    m_incorrect = false;
    updateMessage();
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
            m_view[u"retry"_s] = i18n("The PIN could not be sent to the token. Try again.");
            updateMessage();
            Q_EMIT freshInput();
            Q_EMIT changed();
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
