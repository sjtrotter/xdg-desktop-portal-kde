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
    property string focusedCandidateId: ""
    property string refreshFocusId: ""
    property bool restoreCandidateFocus: false
    property bool focusFirstCandidate: false
    component CandidateDelegate: Kirigami.SubtitleDelegate {
        required property string candidateId
    }
    function present(): void {
        Kirigami.Settings.isMobile ? showMaximized() : show();
        focusCandidates("");
    }
    function firstEnabledCandidate() {
        for (let i = 0; i < candidates.count; ++i) {
            const candidate = candidates.itemAt(i);
            if (candidate && candidate.enabled)
                return candidate;
        }
        return null;
    }
    function focusCandidates(candidateId: string): void {
        let item = null;
        if (candidateId.length > 0) {
            for (let i = 0; i < candidates.count; ++i) {
                const candidate = candidates.itemAt(i) as CandidateDelegate;
                if (candidate && candidate.enabled && candidate.candidateId === candidateId) {
                    item = candidate;
                    break;
                }
            }
        }
        if (!item)
            item = candidates.itemAt(candidateModel.selectedRow);
        if (!item || !item.enabled)
            item = firstEnabledCandidate();
        // Focus is separate from consent: selectRow() is only called by input.
        if (item)
            item.forceActiveFocus(Qt.TabFocusReason);
        else
            buttons.standardButton(QQC2.DialogButtonBox.Cancel).forceActiveFocus(Qt.TabFocusReason);
    }
    function restoreFocusAfterRefresh(): void {
        // The focus scope survives delegate replacement. Recheck it so a Tab
        // to the buttons before this deferred callback cannot pull focus back.
        const restoreList = restoreCandidateFocus && candidateFocus.activeFocus;
        const restoreEmpty = focusFirstCandidate && firstEnabledCandidate() && buttons.standardButton(QQC2.DialogButtonBox.Cancel).activeFocus;
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
    // Defer until presentation has made the accessible item visible. Taking the
    // pending state once also prevents an unchanged remap from announcing again.
    onVisibleChanged: if (visible) Qt.callLater(root.announceState)
    Connections {
        target: root.candidateModel
        function onSnapshotAboutToChange(): void {
            // Removed delegates are disabled before modelAboutToBeReset.
            root.focusFirstCandidate = !root.firstEnabledCandidate() && buttons.standardButton(QQC2.DialogButtonBox.Cancel).activeFocus;
        }
        function onModelAboutToBeReset(): void {
            root.restoreCandidateFocus = candidateFocus.activeFocus;
            root.refreshFocusId = root.restoreCandidateFocus ? root.focusedCandidateId : "";
        }
        function onModelReset(): void {
            // Repeater recreates its delegates on reset, including unchanged rows.
            Qt.callLater(root.restoreFocusAfterRefresh);
        }
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
        onActivated: {
            root.candidateModel.selectRow(Math.min(root.candidateModel.selectedRow + 1, root.candidateModel.count - 1));
            root.focusCandidates("");
        }
    }
    Shortcut {
        sequence: "Up"
        onActivated: {
            root.candidateModel.selectRow(Math.max(root.candidateModel.selectedRow - 1, 0));
            root.focusCandidates("");
        }
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
        QQC2.ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            ColumnLayout {
                width: scroll.availableWidth
                spacing: Kirigami.Units.largeSpacing
                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 2
                    text: root.candidateModel.context.heading
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    text: root.candidateModel.context.identity
                    color: Kirigami.Theme.disabledTextColor
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    visible: text.length > 0 && text !== root.candidateModel.context.caller
                    text: root.candidateModel.context.appId
                    color: Kirigami.Theme.disabledTextColor
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    text: root.candidateModel.context.purposeDetail
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
                Kirigami.FormLayout {
                    Layout.fillWidth: true
                    QQC2.Label {
                        Kirigami.FormData.label: KI18n.i18n("Purpose:")
                        Layout.fillWidth: true
                        text: root.candidateModel.context.purpose
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                    }
                    QQC2.Label {
                        Kirigami.FormData.label: KI18n.i18n("Operations:")
                        Layout.fillWidth: true
                        text: root.candidateModel.context.scope
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                    }
                    QQC2.Label {
                        Kirigami.FormData.label: KI18n.i18n("Duration:")
                        Layout.fillWidth: true
                        text: root.candidateModel.context.duration
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                    }
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: root.candidateModel.context.laterPrompt
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
                QQC2.GroupBox {
                    Layout.fillWidth: true
                    focusPolicy: Qt.NoFocus
                    visible: root.candidateModel.context.reason.length > 0
                    title: KI18n.i18n("Reason given by the application")
                    label: QQC2.Label {
                        text: KI18n.i18n("Reason given by the application")
                        textFormat: Text.PlainText
                        font.bold: true
                    }
                    ColumnLayout {
                        width: parent.width
                        QQC2.Label {
                            Layout.fillWidth: true
                            text: KI18n.i18n("This reason is untrusted and has not been verified by the system.")
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            color: Kirigami.Theme.disabledTextColor
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            text: root.candidateModel.context.reason
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                        }
                    }
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
                FocusScope {
                    id: candidateFocus
                    Layout.fillWidth: true
                    implicitHeight: candidateRows.implicitHeight
                    ColumnLayout {
                        id: candidateRows
                        width: parent.width
                        spacing: Kirigami.Units.largeSpacing
                        Repeater {
                            id: candidates
                            model: root.candidateModel
                            delegate: CandidateDelegate {
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
                                text: subject
                                subtitle: KI18n.i18n("%1 · %2", issuer, expiry)
                                Accessible.description: subtitle
                                highlighted: root.candidateModel.selectedRow === index
                                onActiveFocusChanged: if (activeFocus) root.focusedCandidateId = candidateId
                                onClicked: root.candidateModel.selectRow(index)
                                onDoubleClicked: {
                                    root.candidateModel.selectRow(index);
                                    if (root.candidateModel.hasSelection)
                                        root.accept();
                                }
                                contentItem: ColumnLayout {
                                    Kirigami.TitleSubtitle {
                                        Layout.fillWidth: true
                                        title: delegate.text
                                        subtitle: delegate.subtitle
                                        font: delegate.font
                                        selected: delegate.highlighted || delegate.down
                                        textFormat: Text.PlainText
                                        wrapMode: Text.Wrap
                                    }
                                    QQC2.Label {
                                        Layout.fillWidth: true
                                        // The model formats validity and key data in the user's locale.
                                        text: KI18n.i18n("Token: %1 · Reader: %2 · %3 · Fingerprint: %4", delegate.token, delegate.reader, delegate.detail, delegate.fingerprint)
                                        color: delegate.highlighted || delegate.down ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.disabledTextColor
                                        font: Kirigami.Theme.smallFont
                                        textFormat: Text.PlainText
                                        wrapMode: Text.WrapAnywhere
                                    }
                                    QQC2.Label {
                                        Layout.fillWidth: true
                                        visible: delegate.invalid
                                        text: KI18n.i18n("This certificate is expired or not yet valid.")
                                        color: Kirigami.Theme.neutralTextColor
                                        textFormat: Text.PlainText
                                        wrapMode: Text.Wrap
                                    }
                                }
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
