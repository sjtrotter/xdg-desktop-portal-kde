/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>

inline bool certificateTestBusHasNoActivation(const QDBusConnection &bus)
{
    if (!bus.isConnected() || !bus.interface()) {
        return false;
    }
    const auto reply = bus.interface()->activatableServiceNames();
    if (!reply.isValid()) {
        return false;
    }
    auto names = reply.value();
    names.removeAll(QStringLiteral("org.freedesktop.DBus")); // The daemon itself is allowed.
    return names.isEmpty();
}
