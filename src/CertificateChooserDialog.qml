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
    readonly property KI18nContext translations: KI18nContext {
        translationDomain: "xdg-desktop-portal-kde"
    }
    title: root.translations.i18nc("@title:window", "Choose Certificate")
    signal accept
    signal reject
    visible: false
    flags: Qt.Dialog | Qt.WindowCloseButtonHint | Qt.WindowTitleHint | Qt.WindowSystemMenuHint
    width: Kirigami.Units.gridUnit * 28
    minimumWidth: Kirigami.Units.gridUnit * 24
    height: Math.min(content.implicitHeight + 2 * Kirigami.Units.largeSpacing, screen.height - 4 * Kirigami.Units.gridUnit)
    property string focusedCandidateId: ""
    property string refreshFocusId: ""
    property bool restoreCandidateFocus: false
    property bool focusFirstCandidate: false
    property var details: ({})

    function present(): void {
        Kirigami.Settings.isMobile ? showMaximized() : show();
        Qt.callLater(() => root.focusCandidates(""));
    }
    function focusCandidates(candidateId: string): void {
        let row = candidateId.length > 0 ? candidateModel.rowForCandidateId(candidateId) : candidateModel.selectedRow;
        if (row < 0 && candidateModel.count > 0)
            row = 0;
        if (row >= 0) {
            candidates.currentIndex = row;
            candidates.positionViewAtIndex(row, ListView.Contain);
            // Delegates outside the viewport are created on demand. A later
            // callback must not pull focus back after the user tabs away.
            if (candidates.currentItem) {
                candidates.currentItem.forceActiveFocus(Qt.TabFocusReason);
            } else {
                candidateFocus.forceActiveFocus();
                Qt.callLater(() => {
                    if (root.visible && candidateFocus.activeFocus && candidates.currentItem)
                        candidates.currentItem.forceActiveFocus(Qt.TabFocusReason);
                });
            }
        } else {
            buttons.standardButton(QQC2.DialogButtonBox.Cancel).forceActiveFocus(Qt.TabFocusReason);
        }
    }
    function moveCandidate(step: int): void {
        let row = candidateModel.rowForCandidateId(focusedCandidateId);
        if (row < 0)
            row = candidateModel.selectedRow;
        if (row < 0)
            row = 0;
        else if (row === candidateModel.selectedRow)
            row = Math.max(0, Math.min(candidateModel.count - 1, row + step));
        candidateModel.selectRow(row);
        focusCandidates("");
    }
    function restoreFocusAfterRefresh(): void {
        const restoreList = restoreCandidateFocus && candidateFocus.activeFocus;
        const restoreEmpty = focusFirstCandidate && candidateModel.count > 0 && buttons.standardButton(QQC2.DialogButtonBox.Cancel).activeFocus;
        const candidateId = refreshFocusId;
        restoreCandidateFocus = false;
        focusFirstCandidate = false;
        refreshFocusId = "";
        if (visible && (restoreList || restoreEmpty))
            focusCandidates(candidateId);
    }
    function announceState(): void {
        if (!visible)
            return;
        const message = candidateModel.takeAnnouncement();
        if (message.length > 0)
            content.Accessible.announce(message);
    }
    onVisibleChanged: if (visible) Qt.callLater(root.announceState)
    Connections {
        target: root.candidateModel
        function onSnapshotAboutToChange(): void {
            root.focusFirstCandidate = root.candidateModel.count === 0 && buttons.standardButton(QQC2.DialogButtonBox.Cancel).activeFocus;
        }
        function onModelAboutToBeReset(): void {
            root.restoreCandidateFocus = candidateFocus.activeFocus || detailsPopup.opened;
            root.refreshFocusId = root.restoreCandidateFocus ? root.focusedCandidateId : "";
            if (root.restoreCandidateFocus)
                candidateFocus.forceActiveFocus();
            detailsPopup.close();
        }
        function onModelReset(): void {
            if (root.candidateModel.count === 0) {
                // Another reset may arrive before deferred restoration runs.
                // An empty snapshot ends the previous candidate's focus.
                root.focusedCandidateId = "";
                root.refreshFocusId = "";
            }
            Qt.callLater(root.restoreFocusAfterRefresh);
        }
        function onAnnouncementPending(): void { Qt.callLater(root.announceState); }
    }
    onClosing: close => {
        close.accepted = false;
        reject();
    }
    Shortcut {
        sequences: [StandardKey.Cancel]
        enabled: !detailsPopup.opened
        onActivated: root.reject()
    }
    Shortcut {
        sequence: "Down"
        enabled: !detailsPopup.opened
        onActivated: root.moveCandidate(1)
    }
    Shortcut {
        sequence: "Up"
        enabled: !detailsPopup.opened
        onActivated: root.moveCandidate(-1)
    }
    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: !detailsPopup.opened && root.candidateModel.hasSelection
            && ((candidates.currentItem && candidates.currentItem.activeFocus && candidates.currentIndex === root.candidateModel.selectedRow)
                || buttons.standardButton(QQC2.DialogButtonBox.Ok).activeFocus)
        onActivated: root.accept()
    }

    ColumnLayout {
        id: content
        Accessible.role: Accessible.Pane
        Accessible.name: root.title
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.largeSpacing
        Kirigami.Icon {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 64
            Layout.preferredHeight: 64
            source: "auth-smartcard"
            fallback: "dialog-password"
            opacity: 0.6
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.maximumWidth: Kirigami.Units.gridUnit * 28
            Layout.alignment: Qt.AlignHCenter
            spacing: content.spacing
            Kirigami.Heading {
                Layout.fillWidth: true
                level: 2
                text: root.candidateModel.context.heading
                textFormat: Text.PlainText
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
            QQC2.Label {
                Layout.fillWidth: true
                text: root.candidateModel.context.body
                textFormat: Text.PlainText
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
            QQC2.Label {
                Layout.fillWidth: true
                visible: text.length > 0
                text: root.candidateModel.context.identity
                color: root.candidateModel.context.unknown ? Kirigami.Theme.neutralTextColor : Kirigami.Theme.disabledTextColor
                textFormat: Text.PlainText
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
            QQC2.Label {
                Layout.fillWidth: true
                visible: text.length > 0
                text: root.candidateModel.context.reason
                color: Kirigami.Theme.textColor
                opacity: 0.7
                textFormat: Text.PlainText
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }
        }
        FocusScope {
            id: candidateFocus
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: root.candidateModel.count === 0 ? 144 : Math.min(360, Math.max(72, candidates.contentHeight + 2))
            Layout.minimumHeight: root.candidateModel.count === 0 ? 144 : 72
            Kirigami.Theme.colorSet: Kirigami.Theme.View
            Rectangle {
                anchors.fill: parent
                color: Kirigami.Theme.backgroundColor
                border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
                border.width: 1
            }
            QQC2.ScrollView {
                anchors.fill: parent
                anchors.margins: 1
                background: null
                contentWidth: availableWidth
                QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff
                ListView {
                    id: candidates
                    objectName: "certificateCandidates"
                    clip: true
                    model: root.candidateModel
                    currentIndex: -1
                    keyNavigationEnabled: false
                    boundsBehavior: Flickable.StopAtBounds
                    delegate: QQC2.ItemDelegate {
                        id: delegate
                        required property int index
                        required property string candidateId
                        required property string subject
                        required property string issuer
                        required property string token
                        required property string reader
                        required property string expiry
                        required property string detail
                        required property string extraDetail
                        required property string fingerprint
                        required property bool available
                        required property bool invalid
                        width: ListView.view.width
                        enabled: available
                        highlighted: root.candidateModel.selectedRow === index
                        Accessible.name: subject
                        Accessible.description: subtitle.text
                        onActiveFocusChanged: if (activeFocus) root.focusedCandidateId = candidateId
                        onClicked: {
                            root.candidateModel.selectRow(index);
                            candidates.currentIndex = index;
                            forceActiveFocus();
                        }
                        onDoubleClicked: {
                            root.candidateModel.selectRow(index);
                            if (root.candidateModel.hasSelection)
                                root.accept();
                        }
                        contentItem: RowLayout {
                            QQC2.RadioButton {
                                activeFocusOnTab: false
                                checked: root.candidateModel.selectedRow === delegate.index
                                onClicked: {
                                    root.candidateModel.selectRow(delegate.index);
                                    candidates.currentIndex = delegate.index;
                                    delegate.forceActiveFocus();
                                }
                                Accessible.name: delegate.subject
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    text: delegate.subject
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight
                                    color: delegate.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                                }
                                QQC2.Label {
                                    id: subtitle
                                    Layout.fillWidth: true
                                    text: root.translations.i18n("%1 · %2", delegate.issuer, delegate.expiry)
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight
                                    color: delegate.invalid ? Kirigami.Theme.negativeTextColor
                                        : delegate.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.disabledTextColor
                                    font: Kirigami.Theme.smallFont
                                }
                            }
                            QQC2.ToolButton {
                                objectName: "certificateDetails"
                                icon.name: "documentinfo"
                                text: root.translations.i18n("Certificate Details")
                                display: QQC2.AbstractButton.IconOnly
                                QQC2.ToolTip.text: text
                                QQC2.ToolTip.visible: hovered
                                // ToolButton's built-in Space activation is unchanged.
                                Keys.onReturnPressed: event => {
                                    event.accepted = true;
                                    click();
                                }
                                Keys.onEnterPressed: event => {
                                    event.accepted = true;
                                    click();
                                }
                                onClicked: {
                                    root.focusedCandidateId = delegate.candidateId;
                                    root.details = { subject: delegate.subject, issuer: delegate.issuer,
                                        token: delegate.token, reader: delegate.reader, key: delegate.detail,
                                        detail: delegate.extraDetail, fingerprint: delegate.fingerprint };
                                    detailsPopup.open();
                                }
                            }
                        }
                    }
                }
            }
            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - 2 * Kirigami.Units.largeSpacing
                visible: root.candidateModel.count === 0
                icon.name: "auth-smartcard"
                icon.width: 32
                icon.height: 32
                text: root.candidateModel.context.tokensPresent ? root.translations.i18n("No usable certificates on this token") : root.translations.i18n("Insert your security token")
                explanation: root.candidateModel.context.tokensPresent ? root.translations.i18n("Try a different security token.") : root.translations.i18n("Certificates on it will appear here.")
            }
        }
        QQC2.DialogButtonBox {
            id: buttons
            Layout.fillWidth: true
            standardButtons: QQC2.DialogButtonBox.Ok | QQC2.DialogButtonBox.Cancel
            onAccepted: root.accept()
            onRejected: root.reject()
            Component.onCompleted: {
                standardButton(QQC2.DialogButtonBox.Cancel).objectName = "certificateCancel";
                standardButton(QQC2.DialogButtonBox.Ok).text = root.translations.i18nc("@action:button", "Allow");
                standardButton(QQC2.DialogButtonBox.Ok).enabled = Qt.binding(() => root.candidateModel.hasSelection);
            }
        }
    }
    QQC2.Popup {
        id: detailsPopup
        objectName: "certificateDetailsPopup"
        parent: QQC2.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(root.width - 2 * Kirigami.Units.largeSpacing, Kirigami.Units.gridUnit * 26)
        height: Math.min(implicitHeight, root.height - 2 * Kirigami.Units.largeSpacing)
        modal: true
        focus: true
        onClosed: {
            if (root.visible && !root.restoreCandidateFocus)
                root.focusCandidates(root.focusedCandidateId);
        }
        contentItem: QQC2.ScrollView {
            id: detailsScroll
            clip: true
            contentWidth: availableWidth
            QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

            Kirigami.FormLayout {
                width: detailsScroll.availableWidth

                QQC2.Label {
                    objectName: "certificateDetailsSubject"
                    Kirigami.FormData.label: root.translations.i18n("Subject:")
                    Layout.fillWidth: true
                    text: root.details.subject ?? ""
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                }
                QQC2.Label {
                    objectName: "certificateDetailsIssuer"
                    Kirigami.FormData.label: root.translations.i18n("Issuer:")
                    Layout.fillWidth: true
                    text: root.details.issuer ?? ""
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                }
                QQC2.Label {
                    Kirigami.FormData.label: root.translations.i18n("Security token:")
                    Layout.fillWidth: true
                    text: root.details.token ?? ""
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                }
                QQC2.Label {
                    Kirigami.FormData.label: root.translations.i18n("Reader:")
                    Layout.fillWidth: true
                    text: root.details.reader ?? ""
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                }
                QQC2.Label {
                    Kirigami.FormData.label: root.translations.i18n("Key:")
                    Layout.fillWidth: true
                    text: root.details.key ?? ""
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                }
                QQC2.Label {
                    Kirigami.FormData.label: root.translations.i18n("Details:")
                    Layout.fillWidth: true
                    text: root.details.detail ?? ""
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                }
                QQC2.TextArea {
                    Kirigami.FormData.label: root.translations.i18n("Fingerprint:")
                    Layout.fillWidth: true
                    text: root.details.fingerprint ?? ""
                    textFormat: TextEdit.PlainText
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                }
            }
        }
    }
}
