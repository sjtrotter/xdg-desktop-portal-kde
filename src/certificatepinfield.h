/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include <QQuickItem>
#include <xdp-certificate/ui.h>

// No editable text API: QML and accessibility receive only the mask length.
class CertificatePinField : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(int maskedCount READ maskedCount NOTIFY maskedCountChanged FINAL)
public:
    explicit CertificatePinField(QQuickItem *parent = nullptr);
    ~CertificatePinField() override;
    int maskedCount() const;
    void reset();
    void discard();
    bool submit(XccContext *context, XccUiId id);
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

Q_SIGNALS:
    void maskedCountChanged();
    void submitRequested();

protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;

private:
    void append(QStringView input);
    XccSecret *m_secret = nullptr;
    size_t m_bytes = 0;
    int m_count = 0;
};
