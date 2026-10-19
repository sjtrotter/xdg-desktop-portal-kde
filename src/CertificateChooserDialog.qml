/* SPDX-License-Identifier: LGPL-2.1-or-later */
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.ki18n

Kirigami.AbstractApplicationWindow {
    id: root
    required property var candidateModel
    title: KI18n.i18nc("@title:window", "Choose a Certificate")
    signal accept
    signal reject
    visible: false
    flags: Qt.Dialog | Qt.WindowCloseButtonHint | Qt.WindowTitleHint | Qt.WindowSystemMenuHint
    minimumWidth: Kirigami.Units.gridUnit * 22
    minimumHeight: Kirigami.Units.gridUnit * 16
    function present(): void {
        Kirigami.Settings.isMobile ? showMaximized() : show();
    }
    function announceState(): void {
        if (!visible)
            return;
        const message = candidateModel.takeAnnouncement();
        if (message.length > 0)
            content.Accessible.announce(message);
    }
    // Defer until presentation has made the accessible item visible. Taking the
    // pending state once also prevents an unchanged remap from announcing again.
    onVisibleChanged: if (visible) Qt.callLater(root.announceState)
    Connections {
        target: root.candidateModel
        function onAnnouncementPending(): void {
            Qt.callLater(root.announceState);
        }
    }
    onClosing: close => {
        close.accepted = false;
        reject();
    }
    width: Kirigami.Units.gridUnit * 34
    height: Kirigami.Units.gridUnit * 36

    Shortcut {
        sequence: StandardKey.Cancel
        onActivated: root.reject()
    }
    Shortcut {
        sequence: "Down"
        onActivated: root.candidateModel.selectRow(Math.min(root.candidateModel.selectedRow + 1, root.candidateModel.count - 1))
    }
    Shortcut {
        sequence: "Up"
        onActivated: root.candidateModel.selectRow(Math.max(root.candidateModel.selectedRow - 1, 0))
    }
    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: root.candidateModel.hasSelection && !buttons.standardButton(QQC2.DialogButtonBox.Cancel).activeFocus
        onActivated: root.accept()
    }

    // This dialog stays alive until the core closes it, including stale choices.
    ColumnLayout {
        id: content
        Accessible.role: Accessible.Pane
        Accessible.name: root.title
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
        QQC2.Label {
            Layout.fillWidth: true
            text: root.title
            textFormat: Text.PlainText
            font.bold: true
            wrapMode: Text.Wrap
        }
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
                    text: root.candidateModel.context.caller || root.candidateModel.context.appId
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    visible: text.length > 0 && text !== root.candidateModel.context.caller
                    text: root.candidateModel.context.appId
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    text: root.candidateModel.context.identity
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    text: root.candidateModel.context.purpose + "\n" + root.candidateModel.context.purposeDetail + "\n" + root.candidateModel.context.scope + "\n" + root.candidateModel.context.duration + "\n" + root.candidateModel.context.laterPrompt
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
                QQC2.Label {
                    visible: root.candidateModel.context.reason.length > 0
                    text: KI18n.i18n("The application says (untrusted):")
                    textFormat: Text.PlainText
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: root.candidateModel.context.reason
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    visible: root.candidateModel.count === 0
                    text: KI18n.i18n("Insert your security token")
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    visible: root.candidateModel.count === 0 && root.candidateModel.context.tokensPresent
                    text: KI18n.i18n("No certificate on the available tokens matches this request.")
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
                Repeater {
                    model: root.candidateModel
                    delegate: QQC2.ItemDelegate {
                        id: delegate
                        required property int index
                        required property string subject
                        required property string issuer
                        required property string token
                        required property string reader
                        required property string expiry
                        required property string detail
                        required property string fingerprint
                        required property bool available
                        required property bool invalid
                        Layout.fillWidth: true
                        enabled: available
                        highlighted: root.candidateModel.selectedRow === index
                        onClicked: root.candidateModel.selectRow(index)
                        contentItem: ColumnLayout {
                            QQC2.Label {
                                Layout.fillWidth: true
                                text: delegate.subject
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                                font.bold: true
                            }
                            QQC2.Label {
                                Layout.fillWidth: true
                                text: delegate.issuer + "\n" + delegate.token + "\n" + delegate.reader
                                textFormat: Text.PlainText
                                wrapMode: Text.WrapAnywhere
                            }
                            QQC2.Label {
                                Layout.fillWidth: true
                                // The model formats validity and key data in the user's locale.
                                text: delegate.expiry + "\n" + delegate.detail + "\n" + delegate.fingerprint
                                textFormat: Text.PlainText
                                wrapMode: Text.WrapAnywhere
                            }
                            QQC2.Label {
                                Layout.fillWidth: true
                                visible: delegate.invalid
                                text: KI18n.i18n("This certificate is expired or not yet valid.")
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }
        }
        QQC2.DialogButtonBox {
            id: buttons
            Layout.fillWidth: true
            standardButtons: QQC2.DialogButtonBox.Ok | QQC2.DialogButtonBox.Cancel
            onAccepted: root.accept()
            onRejected: root.reject()
            Component.onCompleted: {
                standardButton(QQC2.DialogButtonBox.Ok).enabled = Qt.binding(() => root.candidateModel.hasSelection);
            }
        }
    }
}
