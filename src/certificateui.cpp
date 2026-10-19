/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "certificateui.h"

#include "certificatechoosermodel.h"
#include "certificatepinfield.h"
#include "certificatepinprompt.h"
#include "quickdialog.h"
#include "utils.h"

#include <KLocalizedQmlContext>
#include <KWindowSystem>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>
#include <utility>
#include <functional>

using namespace Qt::StringLiterals;

namespace
{
// QuickDialog's default creation schedules presentation before callers can hook
// activation. Keep its ownership/signals, but schedule our own presentation so
// parent import and the one-use token are applied to the actual window.
class CertificateDialog : public QuickDialog
{
public:
    using QuickDialog::QuickDialog;
    bool createDialog(const QString &component, const QVariantMap &properties, const QString &parentWindow, QString activationToken,
                      std::function<void()> presented = {})
    {
        auto engine = new QQmlApplicationEngine(this);
        KLocalization::setupLocalizedContext(engine);
        engine->setInitialProperties(properties);
        engine->loadFromModule("org.kde.xdgdesktopportal"_L1, component);
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
        QTimer::singleShot(0, this, [this, activationToken = std::move(activationToken), presented = std::move(presented)]() mutable {
            if (m_suppressed) {
                return;
            }
            // Explicitly clear any process-global token for PIN presentation:
            // Sign has no activation-token option in the current contract.
            KWindowSystem::setCurrentXdgActivationToken(activationToken);
            activationToken.clear();
            QMetaObject::invokeMethod(m_theDialog, "present", Qt::DirectConnection);
            KWindowSystem::activateWindow(m_theDialog);
            if (presented) {
                presented();
            }
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
    QPointer<CertificateDialog> dialog;
    CertificateChooserModel *model = nullptr;
    CertificatePinPrompt *pin = nullptr;
    bool settled = false;
};

CertificateUi::CertificateUi(QObject *parent)
    : QObject(parent)
{
    // This library is static; register explicitly before loading the portal's
    // QML module, including in standalone offscreen bridge tests.
    static const int fieldType = qmlRegisterType<CertificatePinField>("org.kde.xdgdesktopportal", 1, 0, "CertificatePinField");
    Q_UNUSED(fieldType);
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
    ui.pin_show = [](void *user, XccUiId id, const XccPinView *view) { static_cast<CertificateUi *>(user)->pinShow(id, view); };
    ui.pin_update = [](void *user, XccUiId id, const XccPinView *view) { static_cast<CertificateUi *>(user)->pinUpdate(id, view); };
    ui.warning = [](void *user, XccUiId id, const XccWarning *warning) { static_cast<CertificateUi *>(user)->warning(id, warning); };
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
    auto dialog = new CertificateDialog(this);
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
    if (!dialog->createDialog(u"CertificateChooserDialog"_s, { { u"candidateModel"_s, QVariant::fromValue(model) } },
                              QString::fromUtf8(view->parent_window), QString::fromUtf8(view->activation_token))) {
        hide(id);
        xcc_ui_failed(m_context, id, XCC_ERROR_NOT_SUPPORTED);
    }
}

void CertificateUi::update(XccUiId id, const XccChooserView *view)
{
    if (auto interaction = m_interactions.value(id)) {
        if (!interaction->settled && interaction->model && !interaction->model->update(view)) {
            hide(id);
            xcc_ui_failed(m_context, id, XCC_ERROR_NOT_SUPPORTED);
        }
    }
}

void CertificateUi::pinShow(XccUiId id, const XccPinView *view)
{
    if (m_interactions.contains(id)) {
        return;
    }
    auto pin = new CertificatePinPrompt(m_context, id, this);
    if (!pin->update(view)) {
        delete pin;
        xcc_ui_failed(m_context, id, XCC_ERROR_NOT_SUPPORTED);
        return;
    }
    auto dialog = new CertificateDialog(this);
    auto interaction = new Interaction{ dialog, nullptr, pin };
    m_interactions.insert(id, interaction);
    connect(dialog, &QuickDialog::accepted, pin, [pin, dialog] {
        pin->submit(dialog->windowHandle()->findChild<CertificatePinField *>());
    });
    connect(dialog, &QuickDialog::rejected, this, [this, id, pin] {
        pin->cancel();
        hide(id);
    });
    // XccPinView has only the current operation's parent, deliberately no
    // activation token. Never borrow the acquisition chooser's token for Sign.
    if (!dialog->createDialog(u"CertificatePinDialog"_s, { { u"pinPrompt"_s, QVariant::fromValue(pin) } },
                              QString::fromUtf8(view->parent_window), {}, [pin, dialog] {
                                  pin->presented();
                                  if (auto field = dialog->windowHandle()->findChild<CertificatePinField *>()) {
                                      field->forceActiveFocus();
                                  }
                              })) {
        hide(id);
        xcc_ui_failed(m_context, id, XCC_ERROR_NOT_SUPPORTED);
        return;
    }
    if (auto field = dialog->windowHandle()->findChild<CertificatePinField *>()) {
        connect(field, &CertificatePinField::submitRequested, pin, [pin, field] { pin->submit(field); });
        connect(pin, &CertificatePinPrompt::freshInput, field, [field] {
            field->reset();
            // The view still reports busy until update() publishes the fresh
            // round. Focus once QML has enabled the field for that round.
            QTimer::singleShot(0, field, [field] {
                if (field->isEnabled()) {
                    field->forceActiveFocus();
                }
            });
        });
        connect(pin, &CertificatePinPrompt::discardInput, field, &CertificatePinField::discard);
    }
}

void CertificateUi::pinUpdate(XccUiId id, const XccPinView *view)
{
    auto interaction = m_interactions.value(id);
    if (interaction && !interaction->settled && interaction->pin && !interaction->pin->update(view)) {
        hide(id);
        xcc_ui_failed(m_context, id, XCC_ERROR_NOT_SUPPORTED);
    }
}

void CertificateUi::warning(XccUiId id, const XccWarning *warning)
{
    auto interaction = m_interactions.value(id);
    if (interaction && !interaction->settled && interaction->pin && !interaction->pin->warning(warning)) {
        hide(id);
        xcc_ui_failed(m_context, id, XCC_ERROR_NOT_SUPPORTED);
    }
}

void CertificateUi::hide(XccUiId id)
{
    if (auto interaction = m_interactions.value(id)) {
        interaction->settled = true;
        if (interaction->pin) {
            interaction->pin->conceal();
        }
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
        delete interaction->pin;
        delete interaction;
    }
}
