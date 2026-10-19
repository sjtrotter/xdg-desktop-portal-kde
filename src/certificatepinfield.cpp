/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "certificatepinfield.h"

#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <cstring>
#include <utility>

namespace
{
// Volatile stores must survive optimization, including on rejected input.
void wipe(void *memory, size_t size)
{
    auto bytes = static_cast<volatile unsigned char *>(memory);
    while (size--) {
        *bytes++ = 0;
    }
}
} // namespace

CertificatePinField::CertificatePinField(QQuickItem *parent)
    : QQuickItem(parent)
    , m_secret(xcc_secret_new())
{
    setFlag(ItemAcceptsInputMethod, false);
    setActiveFocusOnTab(true);
    setAcceptedMouseButtons(Qt::LeftButton);
}

CertificatePinField::~CertificatePinField() { xcc_secret_free(m_secret); }
int CertificatePinField::maskedCount() const { return m_count; }

void CertificatePinField::discard()
{
    xcc_secret_free(std::exchange(m_secret, nullptr));
    m_bytes = 0;
    m_count = 0;
    Q_EMIT maskedCountChanged();
}

void CertificatePinField::reset()
{
    discard();
    m_secret = xcc_secret_new();
}

bool CertificatePinField::submit(XccContext *context, XccUiId id)
{
    if (!context || !m_secret || !m_bytes || !xcc_secret_set_length(m_secret, m_bytes)) {
        return false;
    }
    auto secret = std::exchange(m_secret, nullptr);
    m_bytes = 0;
    m_count = 0;
    Q_EMIT maskedCountChanged();
    xcc_pin_submit(context, id, secret); // Consumes even when id is stale.
    return true;
}

QVariant CertificatePinField::inputMethodQuery(Qt::InputMethodQuery query) const
{
    if (query == Qt::ImHints) {
        return int(Qt::ImhHiddenText | Qt::ImhSensitiveData | Qt::ImhNoPredictiveText);
    }
    if (query == Qt::ImEnabled) {
        return false;
    }
    return { }; // In particular, never expose surrounding/selected text.
}

void CertificatePinField::inputMethodEvent(QInputMethodEvent *event) { event->ignore(); }

bool CertificatePinField::event(QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        auto key = static_cast<QKeyEvent *>(event);
        // Claim editing shortcuts so neither ancestors nor application actions
        // can operate on a secret. Tab and Escape retain normal navigation.
        if (key->key() != Qt::Key_Tab && key->key() != Qt::Key_Backtab && key->key() != Qt::Key_Escape) {
            key->accept();
            return true;
        }
    }
    return QQuickItem::event(event);
}

void CertificatePinField::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus(Qt::MouseFocusReason);
    event->accept();
}

void CertificatePinField::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
        event->ignore();
        return;
    }
    event->accept();
    if (!isEnabled() || !m_secret) {
        return;
    }
    // No clipboard, undo, selection, or command processing. Permit AltGr text
    // whether the platform marks it as GroupSwitch or as Ctrl+Alt.
    const auto modifiers = event->modifiers();
    const bool altGr = modifiers.testFlag(Qt::GroupSwitchModifier)
        || (modifiers.testFlag(Qt::ControlModifier) && modifiers.testFlag(Qt::AltModifier));
    if (modifiers.testFlag(Qt::MetaModifier) || (!altGr && (modifiers & (Qt::ControlModifier | Qt::AltModifier)))) {
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        Q_EMIT submitRequested();
    } else if (event->key() == Qt::Key_Backspace && m_bytes) {
        size_t capacity;
        auto data = xcc_secret_data(m_secret, &capacity);
        size_t start = m_bytes - 1;
        while (start && (data[start] & 0xc0) == 0x80) {
            --start;
        }
        wipe(data + start, m_bytes - start);
        m_bytes = start;
        --m_count;
        Q_EMIT maskedCountChanged();
    } else {
        // text() returns an implicitly shared QString owned by the event. The
        // temporary survives this call; no owned conversion string is made.
        // Never log it or forward it to QML/queued handlers. Qt/input-stack
        // copies are outside our wipe control (see autotests/README.certificate.md).
        append(QStringView(event->text()));
    }
}

void CertificatePinField::keyReleaseEvent(QKeyEvent *event)
{
    // Releases can contain text too. Keep them away from ancestor QML Keys
    // handlers, which can retain their last event. Preserve normal navigation.
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
        event->ignore();
    } else {
        event->accept();
    }
}

void CertificatePinField::append(QStringView input)
{
    // Convert straight from the event into bounded stack storage. No QString,
    // QByteArray, QVariant, queued argument, or undo buffer owns our PIN bytes.
    unsigned char utf8[511] = { };
    size_t length = 0;
    int count = 0;
    bool valid = true;
    for (qsizetype i = 0; i < input.size(); ++i) {
        char32_t code = input[i].unicode();
        if (QChar::isHighSurrogate(code)) {
            if (++i == input.size() || !input[i].isLowSurrogate()) {
                valid = false;
                break;
            }
            code = QChar::surrogateToUcs4(input[i - 1], input[i]);
        } else if (QChar::isLowSurrogate(code)) {
            valid = false;
            break;
        }
        if (!code || code < 0x20 || code == 0x7f) {
            valid = false;
            break;
        }
        const size_t width = code < 0x80 ? 1 : code < 0x800 ? 2 : code < 0x10000 ? 3 : 4;
        if (m_bytes + length + width > sizeof(utf8)) {
            valid = false;
            break;
        }
        if (width == 1) {
            utf8[length++] = code;
        } else {
            utf8[length++] = (width == 2 ? 0xc0 : width == 3 ? 0xe0 : 0xf0) | (code >> (6 * (width - 1)));
            for (size_t remaining = width - 1; remaining; --remaining) {
                utf8[length++] = 0x80 | ((code >> (6 * (remaining - 1))) & 0x3f);
            }
        }
        ++count;
    }
    if (valid && length) {
        size_t capacity;
        auto data = xcc_secret_data(m_secret, &capacity);
        if (m_bytes + length < capacity) {
            std::memcpy(data + m_bytes, utf8, length);
            m_bytes += length;
            data[m_bytes] = 0;
            m_count += count;
        }
    }
    wipe(utf8, sizeof(utf8));
    Q_EMIT maskedCountChanged();
}

#include "moc_certificatepinfield.cpp"
