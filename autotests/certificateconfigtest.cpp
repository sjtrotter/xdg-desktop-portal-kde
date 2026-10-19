/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "certificateconfig.h"

#include <KConfig>
#include <KConfigGroup>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTest>

class CertificateConfigTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void readUserConfig()
    {
        const CertificateConfig defaults;
        const auto initial = defaults.config();
        QCOMPARE(initial.struct_size, uint32_t(sizeof(XccConfig)));
        QCOMPARE(initial.abi_version, uint32_t(XCC_ABI_VERSION));
        QVERIFY(initial.module_paths == nullptr);
        QVERIFY(!initial.allow_software);

        KConfig file(QStringLiteral("xdg-desktop-portal-kderc"), KConfig::NoGlobals);
        KConfigGroup group(&file, QStringLiteral("Certificate"));
        const QStringList modules{ QStringLiteral("/tmp/module one.so"), QStringLiteral("/tmp/module,two.so") };
        group.writeEntry("Modules", modules);
        group.writeEntry("AllowSoftwareTokens", true);
        QVERIFY(file.sync());

        const CertificateConfig configured;
        const auto config = configured.config();
        QVERIFY(config.allow_software);
        QVERIFY(config.module_paths != nullptr);
        QCOMPARE(QByteArray(config.module_paths[0]), modules[0].toUtf8());
        QCOMPARE(QByteArray(config.module_paths[1]), modules[1].toUtf8());
        QVERIFY(config.module_paths[2] == nullptr);
        // A loaded snapshot does not change when the file changes.
        group.writeEntry("Modules", QStringList{ });
        group.writeEntry("AllowSoftwareTokens", false);
        QVERIFY(file.sync());
        QVERIFY(configured.config().allow_software);
        const CertificateConfig reset;
        QVERIFY(reset.config().module_paths == nullptr);
        QVERIFY(!reset.config().allow_software);
    }
};

int main(int argc, char **argv)
{
    // Set before QCoreApplication/KConfig can cache any standard paths.
    QTemporaryDir configHome;
    if (!configHome.isValid()) {
        return 1;
    }
    qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());
    qputenv("XDG_CONFIG_DIRS", configHome.path().toUtf8());
    QCoreApplication app(argc, argv);
    CertificateConfigTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "certificateconfigtest.moc"
