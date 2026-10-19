/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include <QMutex>
#include <QObject>
#include <xdp-certificate/core.h>

// This receiver must outlive xcc_context_shutdown_async's completion.
class CertificateDispatcher : public QObject
{
public:
    explicit CertificateDispatcher(QObject *parent = nullptr);
    XccDispatch table();

private:
    static void post(void *user, XccFunction function, void *closure);
    QMutex m_mutex;
};
