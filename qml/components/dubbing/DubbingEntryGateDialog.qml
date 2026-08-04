import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../base"

// A route-entry gate, not a workflow configuration dialog.  The operator must
// deliberately choose a mode on every visit; the only escape is leaving the
// Dubbing route.  Popup.NoAutoClose prevents outside-click, Escape and window
    // decoration shortcuts from exposing an unapproved workspace.
Dialog {
    id: root

    required property var dubbing
    signal automaticRequested()
    signal stepByStepRequested()
    signal leaveDubbingRequested()

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(720, parent ? parent.width - Theme.paddingXL * 2 : 720)
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.NoAutoClose

    function openGate() {
        open()
    }

    background: Rectangle {
        color: Theme.surface
        radius: Theme.radiusMedium
        border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.62)
        border.width: 1
    }

    contentItem: ColumnLayout {
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.paddingLarge
            LineIcon { name: "workflow"; color: Theme.accentLight; Layout.preferredWidth: 24; Layout.preferredHeight: 24 }
            ColumnLayout {
                Layout.fillWidth: true
                Text { text: qsTr("Choose how to use Dubbing"); color: Theme.textPrimary; font.pixelSize: Theme.fontXLarge; font.bold: true }
                Text {
                    Layout.fillWidth: true
                    text: root.dubbing.savedDubbingEntryMode === "automatic"
                          ? qsTr("This project was last configured for Automatic. Confirm it or choose step-by-step; no project data will be removed.")
                          : root.dubbing.savedDubbingEntryMode === "step"
                            ? qsTr("This project was last configured for step-by-step. Confirm it or choose Automatic; no project data will be removed.")
                            : qsTr("Choose one mode before the Dubbing workspace can be used.")
                    color: Theme.textSecondary; font.pixelSize: Theme.fontSmall; wrapMode: Text.WordWrap
                }
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.surfaceAlt }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.paddingLarge
            spacing: Theme.paddingMedium
            Rectangle {
                Layout.fillWidth: true; implicitHeight: automaticColumn.implicitHeight + Theme.paddingLarge * 2
                radius: Theme.radiusSmall; color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.08)
                border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.45)
                ColumnLayout {
                    id: automaticColumn; anchors.fill: parent; anchors.margins: Theme.paddingLarge; spacing: Theme.paddingSmall
                    Text { text: qsTr("Automatic"); color: Theme.textPrimary; font.bold: true; font.pixelSize: Theme.fontLarge }
                    Text { Layout.fillWidth: true; text: qsTr("Configure source languages, routes, models and any required Direct Colab workers in one review wizard before the workflow starts."); color: Theme.textSecondary; wrapMode: Text.WordWrap }
                    PrimaryButton {
                        id: automaticButton
                        objectName: "dubbingEntryAutomaticButton"
                        text: qsTr("Automatic"); Layout.fillWidth: true; iconName: "play"
                        onClicked: root.automaticRequested()
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true; implicitHeight: stepColumn.implicitHeight + Theme.paddingLarge * 2
                radius: Theme.radiusSmall; color: Qt.rgba(1, 1, 1, 0.025); border.color: Qt.rgba(1, 1, 1, 0.12)
                ColumnLayout {
                    id: stepColumn; anchors.fill: parent; anchors.margins: Theme.paddingLarge; spacing: Theme.paddingSmall
                    Text { text: qsTr("Review one by one"); color: Theme.textPrimary; font.bold: true; font.pixelSize: Theme.fontLarge }
                    Text { Layout.fillWidth: true; text: qsTr("Open the first valid Dubbing step. Each later stage stays blocked until its own required configuration is complete."); color: Theme.textSecondary; wrapMode: Text.WordWrap }
                    PrimaryButton {
                        objectName: "dubbingEntryStepByStepButton"
                        text: qsTr("Review one by one"); Layout.fillWidth: true; quiet: true; iconName: "workflow"
                        onClicked: root.stepByStepRequested()
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.margins: Theme.paddingLarge
            PrimaryButton { text: qsTr("Leave Dubbing"); quiet: true; iconName: "arrow-left"; onClicked: root.leaveDubbingRequested() }
            Item { Layout.fillWidth: true }
        }
    }

    // Used only by the production-shell offscreen interaction regression.
    // It activates the real button, which in turn emits automaticRequested.
    function qmlSmokeClickAutomatic() { automaticButton.click() }
}
