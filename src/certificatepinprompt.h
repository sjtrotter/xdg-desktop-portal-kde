/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include <QObject>
#include <QVariantMap>
#include <xdp-certificate/ui.h>

class CertificatePinField;

// Display state only. The field transfers its secret directly to the SDK.
class CertificatePinPrompt : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap view READ view NOTIFY changed FINAL)
public:
    CertificatePinPrompt(XccContext *context, XccUiId id, QObject *parent = nullptr);
    bool update(const XccPinView *view);
    bool warning(const XccWarning *warning);
    QVariantMap view() const;
    void presented();
    void submit(CertificatePinField *field);
    void cancel();
    void conceal();

Q_SIGNALS:
    void changed();
    void freshInput();
    void discardInput();

private:
    void setBusy(bool busy);
    XccContext *m_context;
    XccUiId m_id;
    QVariantMap m_view;
    uint64_t m_warning = 0;
    bool m_warningPending = false;
    bool m_presented = false;
    bool m_settled = false;
    bool m_submitted = false;
};
