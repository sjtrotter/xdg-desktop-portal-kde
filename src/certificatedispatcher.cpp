/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "certificatedispatcher.h"

#include <QMetaObject>
#include <QMutexLocker>

CertificateDispatcher::CertificateDispatcher(QObject *parent)
    : QObject(parent)
{
}

XccDispatch CertificateDispatcher::table() { return { sizeof(XccDispatch), XCC_ABI_VERSION, this, post }; }

void CertificateDispatcher::post(void *user, XccFunction function, void *closure)
{
    auto receiver = static_cast<CertificateDispatcher *>(user);
    // Serialize insertion from different producers. QueuedConnection also enqueues
    // posts made from this object's thread, without depending on a GLib dispatcher.
    QMutexLocker locker(&receiver->m_mutex);
    if (!QMetaObject::invokeMethod(receiver, [function, closure] { function(closure); }, Qt::QueuedConnection)) {
        qFatal("Certificate dispatcher could not enqueue a closure");
    }
}
