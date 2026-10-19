/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "certificateconfig.h"

#include <KConfig>
#include <KConfigGroup>
#include <QStringList>

CertificateConfig::CertificateConfig()
{
    // These are trusted user settings, like the standalone service's flags.
    // Do not accept module paths or software-token policy from the environment.
    KConfig settings(QStringLiteral("xdg-desktop-portal-kderc"), KConfig::NoGlobals);
    const KConfigGroup group(&settings, QStringLiteral("Certificate"));
    const auto modules = group.readEntry("Modules", QStringList{ });
    m_allowSoftwareTokens = group.readEntry("AllowSoftwareTokens", false);
    for (const auto &module : modules) {
        m_modules.push_back(module.toUtf8());
    }
    if (!m_modules.empty()) {
        for (const auto &module : m_modules) {
            m_modulePaths.push_back(module.constData());
        }
        m_modulePaths.push_back(nullptr);
    }
}

XccConfig CertificateConfig::config() const
{
    XccConfig config{ };
    config.struct_size = sizeof(config);
    config.abi_version = XCC_ABI_VERSION;
    // NULL selects p11-kit's registered modules; an empty array would select none.
    config.module_paths = m_modulePaths.empty() ? nullptr : m_modulePaths.data();
    config.allow_software = m_allowSoftwareTokens;
    return config;
}
