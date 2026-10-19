/* SPDX-License-Identifier: LGPL-2.1-or-later */
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.ki18n
import org.kde.xdgdesktopportal

Kirigami.AbstractApplicationWindow {
    id: root
    required property var pinPrompt
    readonly property KI18nContext translations: KI18nContext {
        translationDomain: "xdg-desktop-portal-kde"
    }
    property alias pinRetry: message
    readonly property CertificatePinField pinField: entryLoader.item as CertificatePinField
    title: root.translations.i18nc("@title:window", "Unlock Security Token")
    signal accept
    signal reject
    visible: false
    flags: Qt.Dialog | Qt.WindowCloseButtonHint | Qt.WindowTitleHint | Qt.WindowSystemMenuHint
    width: Kirigami.Units.gridUnit * 28
    minimumWidth: Kirigami.Units.gridUnit * 24
    height: content.implicitHeight + 2 * Kirigami.Units.largeSpacing
    function present(): void {
        Kirigami.Settings.isMobile ? showMaximized() : show();
        if (pinField)
            pinField.forceActiveFocus();
        else
            buttons.standardButton(QQC2.DialogButtonBox.Cancel).forceActiveFocus();
    }
    function announceRetry(): void {
        if (visible && message.plainText.length > 0)
            message.Accessible.announce(message.plainText);
    }
    onVisibleChanged: if (visible) Qt.callLater(root.announceRetry)
    onClosing: close => {
        close.accepted = false;
        reject();
    }
    Shortcut {
        sequences: [StandardKey.Cancel]
        onActivated: root.reject()
    }
    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.largeSpacing
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing
            Kirigami.Icon {
                Layout.alignment: Qt.AlignTop
                Layout.preferredWidth: Kirigami.Units.iconSizes.large
                Layout.preferredHeight: Kirigami.Units.iconSizes.large
                source: "dialog-password"
            }
            ColumnLayout {
                Layout.fillWidth: true
                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 2
                    text: root.pinPrompt.view.heading
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    text: root.pinPrompt.view.body
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    objectName: "pinToken"
                    text: root.pinPrompt.view.tokenLine
                    color: Kirigami.Theme.disabledTextColor
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
            }
        }
        Kirigami.InlineMessage {
            id: message
            objectName: "pinHint"
            readonly property string plainText: root.pinPrompt.view.message
            Layout.fillWidth: true
            visible: plainText.length > 0
            // InlineMessage uses AutoText internally. Escape legacy external
            // status text so that markup still appears literally.
            text: "<span>" + plainText.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/\n/g, "<br>") + "</span>"
            type: root.pinPrompt.view.messageError ? Kirigami.MessageType.Error : Kirigami.MessageType.Warning
            Accessible.role: Accessible.AlertMessage
            Accessible.name: plainText
            onPlainTextChanged: Qt.callLater(root.announceRetry)
        }
        QQC2.Label {
            Layout.fillWidth: true
            visible: root.pinPrompt.view.protectedPath && root.pinPrompt.view.warningPending
            text: root.translations.i18n("Enter the PIN on the reader after you continue.")
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            Loader {
                id: entryLoader
                Layout.fillWidth: true
                active: !root.pinPrompt.view.protectedPath
                sourceComponent: CertificatePinField {
                    id: field
                    objectName: "certificatePinField"
                    implicitHeight: decoration.implicitHeight
                    enabled: root.pinPrompt.view.ready && !root.pinPrompt.view.busy && !root.pinPrompt.view.locked
                    Accessible.role: Accessible.EditableText
                    Accessible.name: root.translations.i18n("PIN")
                    Accessible.passwordEdit: true
                    // Use the current QQC2 style's TextField for the frame and
                    // placeholder. Only the mask length crosses into QML.
                    QQC2.TextField {
                        id: decoration
                        anchors.fill: parent
                        readOnly: true
                        focusPolicy: Qt.NoFocus
                        // TextField enables tab focus independently of the
                        // stored focusPolicy. Keep all input on the secure field.
                        activeFocusOnTab: false
                        activeFocusOnPress: false
                        selectByMouse: false
                        cursorVisible: false
                        text: "\u25cf".repeat(field.maskedCount)
                        placeholderText: root.translations.i18n("PIN")
                        Accessible.ignored: true
                        MouseArea {
                            anchors.fill: parent
                            onPressed: mouse => {
                                field.forceActiveFocus(Qt.MouseFocusReason);
                                mouse.accepted = true;
                            }
                        }
                    }
                    Rectangle {
                        objectName: "certificatePinFocusIndicator"
                        anchors.fill: parent
                        visible: field.activeFocus
                        color: "transparent"
                        border.color: Kirigami.Theme.focusColor
                        border.width: 2
                        radius: Kirigami.Units.smallSpacing
                    }
                }
            }
            QQC2.BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                running: root.pinPrompt.view.busy
                visible: running
                Accessible.name: root.translations.i18n("Unlocking security token")
            }
        }
        QQC2.DialogButtonBox {
            id: buttons
            Layout.fillWidth: true
            standardButtons: QQC2.DialogButtonBox.Cancel
            onRejected: root.reject()
            QQC2.Button {
                objectName: "pinSubmit"
                QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.AcceptRole
                visible: !root.pinPrompt.view.protectedPath || root.pinPrompt.view.warningPending
                text: root.pinPrompt.view.protectedPath ? root.translations.i18nc("@action:button", "Continue") : root.translations.i18nc("@action:button", "Unlock")
                enabled: root.pinPrompt.view.ready && !root.pinPrompt.view.busy && !root.pinPrompt.view.locked
                    && (root.pinPrompt.view.protectedPath ? root.pinPrompt.view.warningPending : root.pinField !== null && root.pinField.maskedCount > 0)
                onClicked: root.accept()
            }
        }
    }
}
