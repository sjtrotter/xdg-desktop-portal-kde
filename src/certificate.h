/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QVariantMap>
#include <memory>
#include <xdp-certificate/ui.h>

class CertificatePortal : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.experimental.Certificate1")
    Q_PROPERTY(uint version READ version CONSTANT)
public:
    explicit CertificatePortal(QObject *parent);
    // Explicit SDK configuration/UI injection keeps private-bus tests independent
    // of host PKCS#11 modules and the display server.
    CertificatePortal(QObject *parent, const QDBusConnection &bus, const XccConfig &config, const XccUiV1 *ui = nullptr);
    ~CertificatePortal() override;
    static constexpr uint version() { return 1; }
    bool isValid() const;
public Q_SLOTS:
    void CreateSession(const QDBusObjectPath &handle, const QDBusObjectPath &session_handle, const QString &app_id, const QVariantMap &options,
        const QDBusMessage &message, uint &response, QVariantMap &results);
    void AcquireCredential(const QDBusObjectPath &handle, const QDBusObjectPath &session_handle, const QString &app_id, const QString &parent_window,
        const QVariantMap &options, const QDBusMessage &message, uint &response, QVariantMap &results);
    void Sign(const QDBusObjectPath &handle, const QDBusObjectPath &session_handle, const QString &app_id, const QString &parent_window,
        const QVariantMap &options, const QDBusMessage &message, uint &response, QVariantMap &results);
    void GetCapabilities(const QString &app_id, const QVariantMap &options, const QDBusMessage &message, QVariantMap &capabilities);
Q_SIGNALS:
    void SessionInvalidated(const QDBusObjectPath &session_handle, const QString &reason);

private:
    class State;
    std::unique_ptr<State> d;
};
