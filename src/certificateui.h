/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include <QHash>
#include <QObject>
#include <xdp-certificate/ui.h>

class CertificateUi : public QObject
{
public:
    explicit CertificateUi(QObject *parent = nullptr);
    ~CertificateUi() override;
    XccUiV1 table();
    void attach(XccContext *context);

private:
    struct Interaction;
    void show(XccUiId id, const XccChooserView *view);
    void update(XccUiId id, const XccChooserView *view);
    void hide(XccUiId id);
    void close(XccUiId id);
    XccContext *m_context = nullptr;
    QHash<XccUiId, Interaction *> m_interactions;
};
