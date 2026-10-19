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
    title: KI18n.i18nc("@title:window", "Security Token PIN")
    signal accept
    signal reject
    visible: false
    flags: Qt.Dialog | Qt.WindowCloseButtonHint | Qt.WindowTitleHint | Qt.WindowSystemMenuHint
    width: Kirigami.Units.gridUnit * 30
    height: Kirigami.Units.gridUnit * 30
    minimumWidth: Kirigami.Units.gridUnit * 22
    minimumHeight: Kirigami.Units.gridUnit * 20
    function present(): void {
        Kirigami.Settings.isMobile ? showMaximized() : show();
        if (entryLoader.item) {
            entryLoader.item.forceActiveFocus();
        } else {
            cancelButton.forceActiveFocus();
        }
    }
    onClosing: close => {
        close.accepted = false;
        reject();
    }
    Shortcut {
        sequence: StandardKey.Cancel
        onActivated: root.reject()
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
        QQC2.ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            ColumnLayout {
                width: scroll.availableWidth
                spacing: Kirigami.Units.largeSpacing
                QQC2.Label {
                    Layout.fillWidth: true
                    text: root.pinPrompt.view.caller
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    font.bold: true
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    text: root.pinPrompt.view.purpose
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    objectName: "pinToken"
                    text: KI18n.i18n("Security token: %1", root.pinPrompt.view.token)
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    visible: root.pinPrompt.view.reader.length > 0
                    text: KI18n.i18n("Reader: %1", root.pinPrompt.view.reader)
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    text: root.pinPrompt.view.authentication
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
                // Token flags are visible before any input. No inferred counts.
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: hint.implicitHeight + Kirigami.Units.largeSpacing * 2
                    visible: hint.text.length > 0
                    color: Kirigami.Theme.backgroundColor
                    border.width: 2
                    border.color: root.pinPrompt.view.finalTry || root.pinPrompt.view.locked ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.neutralTextColor
                    radius: Kirigami.Units.smallSpacing
                    QQC2.Label {
                        id: hint
                        objectName: "pinHint"
                        anchors.fill: parent
                        anchors.margins: Kirigami.Units.largeSpacing
                        text: root.pinPrompt.view.hint
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        font.bold: true
                        color: root.pinPrompt.view.finalTry || root.pinPrompt.view.locked ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.neutralTextColor
                        Accessible.role: Accessible.AlertMessage
                        Accessible.name: text
                    }
                }
                QQC2.Label {
                    objectName: "pinRetry"
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: root.pinPrompt.view.retry
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    color: Kirigami.Theme.negativeTextColor
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    visible: root.pinPrompt.view.protectedPath
                    text: root.pinPrompt.view.warningPending
                        ? KI18n.i18n("Continue to enter the PIN on the reader, or Cancel.")
                        : KI18n.i18n("Enter the PIN on your security token or reader.")
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
                Loader {
                    id: entryLoader
                    Layout.fillWidth: true
                    active: !root.pinPrompt.view.protectedPath
                    sourceComponent: CertificatePinField {
                        id: field
                        objectName: "certificatePinField"
                        implicitHeight: Kirigami.Units.gridUnit * 2
                        enabled: root.pinPrompt.view.ready && !root.pinPrompt.view.busy && !root.pinPrompt.view.locked
                        Accessible.role: Accessible.EditableText
                        Accessible.name: KI18n.i18n("PIN")
                        Accessible.passwordEdit: true
                        Rectangle {
                            anchors.fill: parent
                            color: Kirigami.Theme.backgroundColor
                            border.color: field.activeFocus ? Kirigami.Theme.focusColor : Kirigami.Theme.disabledTextColor
                            radius: Kirigami.Units.smallSpacing
                            QQC2.Label {
                                anchors.fill: parent
                                anchors.margins: Kirigami.Units.smallSpacing
                                verticalAlignment: Text.AlignVCenter
                                text: "\u25cf".repeat(field.maskedCount)
                                textFormat: Text.PlainText
                                elide: Text.ElideLeft
                                Accessible.ignored: true
                            }
                        }
                    }
                }
                QQC2.BusyIndicator {
                    Layout.alignment: Qt.AlignHCenter
                    running: root.pinPrompt.view.busy
                    visible: running
                    Accessible.name: KI18n.i18n("Unlocking security token")
                }
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            QQC2.Button {
                id: cancelButton
                text: KI18n.i18nc("@action:button", "Cancel")
                onClicked: root.reject()
            }
            QQC2.Button {
                objectName: "pinSubmit"
                visible: !root.pinPrompt.view.protectedPath || root.pinPrompt.view.warningPending
                text: root.pinPrompt.view.protectedPath ? KI18n.i18nc("@action:button", "Continue") : KI18n.i18nc("@action:button", "Unlock")
                enabled: root.pinPrompt.view.ready && !root.pinPrompt.view.busy && !root.pinPrompt.view.locked
                    && (root.pinPrompt.view.protectedPath ? root.pinPrompt.view.warningPending : entryLoader.item !== null && entryLoader.item.maskedCount > 0)
                onClicked: root.accept()
            }
        }
    }
}
