/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "certificateui.h"

#include "certificatechoosermodel.h"
#include "quickdialog.h"
#include "utils.h"

#include <KLocalizedQmlContext>
#include <KWindowSystem>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>
#include <utility>

using namespace Qt::StringLiterals;

namespace
{
// QuickDialog's default creation schedules presentation before callers can hook
// activation. Keep its ownership/signals, but schedule our own presentation so
// parent import and the one-use token are applied to the actual window.
class ChooserDialog : public QuickDialog
{
public:
    using QuickDialog::QuickDialog;
    bool createChooser(CertificateChooserModel *model, const QString &parentWindow, QString activationToken)
    {
        auto engine = new QQmlApplicationEngine(this);
        KLocalization::setupLocalizedContext(engine);
        engine->setInitialProperties({ { u"candidateModel"_s, QVariant::fromValue(model) } });
        engine->loadFromModule("org.kde.xdgdesktopportal"_L1, "CertificateChooserDialog"_L1);
        if (engine->rootObjects().isEmpty()) {
            return false;
        }
        m_theDialog = qobject_cast<QQuickWindow *>(engine->rootObjects().constFirst());
        if (!m_theDialog) {
            return false;
        }
        // A reply may be stale after a hotplug refresh. Keep the chooser alive
        // until the core closes it, so it can receive the next view and reply.
        connect(m_theDialog, SIGNAL(accept()), this, SIGNAL(accepted()));
        connect(m_theDialog, SIGNAL(reject()), this, SIGNAL(rejected()));
        Utils::setParentWindow(m_theDialog, parentWindow);
        QTimer::singleShot(0, this, [this, activationToken = std::move(activationToken)]() mutable {
            if (m_suppressed) {
                return;
            }
            if (!activationToken.isEmpty()) {
                KWindowSystem::setCurrentXdgActivationToken(activationToken);
                activationToken.clear();
            }
            QMetaObject::invokeMethod(m_theDialog, "present", Qt::DirectConnection);
            KWindowSystem::activateWindow(m_theDialog);
        });
        return true;
    }
    void conceal()
    {
        m_suppressed = true;
        if (m_theDialog) {
            m_theDialog->hide();
        }
    }

private:
    bool m_suppressed = false;
};
}

struct CertificateUi::Interaction {
    QPointer<ChooserDialog> dialog;
    CertificateChooserModel *model;
    bool settled = false;
};

CertificateUi::CertificateUi(QObject *parent)
    : QObject(parent)
{
}

CertificateUi::~CertificateUi()
{
    const auto ids = m_interactions.keys();
    for (auto id : ids) {
        close(id);
    }
}

void CertificateUi::attach(XccContext *context) { m_context = context; }

XccUiV1 CertificateUi::table()
{
    XccUiV1 ui{ };
    ui.struct_size = sizeof(ui);
    ui.abi_version = XCC_ABI_VERSION;
    ui.capabilities = XCC_UI_NEEDS_DISPLAY;
    ui.user = this;
    ui.chooser_show = [](void *user, XccUiId id, const XccChooserView *view) { static_cast<CertificateUi *>(user)->show(id, view); };
    ui.chooser_update = [](void *user, XccUiId id, const XccChooserView *view) { static_cast<CertificateUi *>(user)->update(id, view); };
    // Slice 7b supplies secure PIN entry, warnings and process hardening. Never
    // collect a PIN in QML, QVariant, signals, or logs in this implementation.
    ui.pin_show = [](void *user, XccUiId id, const XccPinView *) { xcc_ui_failed(static_cast<CertificateUi *>(user)->m_context, id, XCC_ERROR_NOT_SUPPORTED); };
    ui.pin_update = ui.pin_show;
    ui.warning = [](void *user, XccUiId id, const XccWarning *) { xcc_ui_failed(static_cast<CertificateUi *>(user)->m_context, id, XCC_ERROR_NOT_SUPPORTED); };
    ui.hide = [](void *user, XccUiId id) { static_cast<CertificateUi *>(user)->hide(id); };
    ui.close = [](void *user, XccUiId id) { static_cast<CertificateUi *>(user)->close(id); };
    return ui;
}

void CertificateUi::show(XccUiId id, const XccChooserView *view)
{
    if (m_interactions.contains(id)) {
        return;
    }
    auto model = new CertificateChooserModel(this);
    if (!model->update(view)) {
        delete model;
        xcc_ui_failed(m_context, id, XCC_ERROR_NOT_SUPPORTED);
        return;
    }
    auto dialog = new ChooserDialog(this);
    auto interaction = new Interaction{ dialog, model };
    m_interactions.insert(id, interaction);
    connect(dialog, &QuickDialog::accepted, this, [this, id] {
        auto interaction = m_interactions.value(id);
        if (!interaction || interaction->settled) {
            return;
        }
        if (interaction->model->hasSelection()) {
            xcc_choose(m_context, id, interaction->model->revision(), interaction->model->selectedId());
        }
    });
    connect(dialog, &QuickDialog::rejected, this, [this, id] {
        auto interaction = m_interactions.value(id);
        if (interaction && !interaction->settled) {
            hide(id);
            xcc_ui_cancel(m_context, id);
        }
    });
    if (!dialog->createChooser(model, QString::fromUtf8(view->parent_window), QString::fromUtf8(view->activation_token))) {
        hide(id);
        xcc_ui_failed(m_context, id, XCC_ERROR_NOT_SUPPORTED);
    }
}

void CertificateUi::update(XccUiId id, const XccChooserView *view)
{
    if (auto interaction = m_interactions.value(id)) {
        if (!interaction->model->update(view)) {
            hide(id);
            xcc_ui_failed(m_context, id, XCC_ERROR_NOT_SUPPORTED);
        }
    }
}

void CertificateUi::hide(XccUiId id)
{
    if (auto interaction = m_interactions.value(id)) {
        interaction->settled = true;
        if (interaction->dialog) {
            interaction->dialog->conceal();
        }
    }
}

void CertificateUi::close(XccUiId id)
{
    hide(id);
    if (auto interaction = m_interactions.take(id)) {
        delete interaction->dialog.data();
        delete interaction->model;
        delete interaction;
    }
}
