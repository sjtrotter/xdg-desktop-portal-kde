/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include <QByteArray>
#include <vector>
#include <xdp-certificate/core.h>

// Own the module strings until xcc_context_new() has copied its inputs.
class CertificateConfig
{
public:
    CertificateConfig();
    CertificateConfig(const CertificateConfig &) = delete;
    CertificateConfig &operator=(const CertificateConfig &) = delete;
    XccConfig config() const;

private:
    std::vector<QByteArray> m_modules;
    std::vector<const char *> m_modulePaths;
    bool m_allowSoftwareTokens = false;
};
