pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../base"
import "../shared"
import LAStudio

Rectangle {
    id: root

    required property var dubbing
    required property var steps
    required property string statusText
    required property string defaultExportPath
    property bool historyOpen: false
    property bool settingsOpen: false
    // Kept as a compatibility property for the route smoke.  Project setup is
    // now a dialog, not a permanent panel below the timeline.
    property bool projectStatusOpen: false

    signal stepSelected(string stepId)
    signal historyToggled()
    signal settingsToggled()
    signal projectStatusToggled()
    signal generateRequested()
    signal pauseRequested()
    signal stopRequested()
    signal workflowRequested()
    signal colabSetupRequested()
    signal saveRequested()
    signal exportRequested()

    function qmlSmokeClickProjectStatusToggle() {
        if (!projectStatusToggle.enabled)
            return false
        projectStatusToggle.click()
        return true
    }

    Layout.fillWidth: true
    // Two fixed rows prevent task labels and the Workflow action from being
    // squeezed into clipped fragments on a narrow editor window.
    Layout.preferredHeight: 84
    color: Qt.rgba(0, 0, 0, 0.10)
    border.color: Qt.rgba(1, 1, 1, 0.06)
    border.width: 1

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.paddingLarge
        anchors.rightMargin: Theme.paddingLarge
        spacing: 2

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 39
            spacing: Theme.paddingSmall

            SidebarToggleButton {
                iconName: "history"
                toolTip: root.historyOpen ? qsTr("Hide dubbing history") : qsTr("Show dubbing history")
                active: root.historyOpen
                onClicked: root.historyToggled()
            }
            PrimaryButton {
                id: projectStatusToggle
                objectName: "dubbingProjectStatusToggle"
                text: qsTr("Project settings")
                iconName: "sliders"
                quiet: true
                implicitWidth: Math.max(136, requiredContentWidth)
                onClicked: root.projectStatusToggled()
            }
            RowLayout {
                Layout.preferredWidth: 188
                spacing: Theme.paddingSmall
                LineIcon { name: "dubbing"; color: Theme.accentLight; Layout.preferredWidth: 21; Layout.preferredHeight: 21 }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text { text: qsTr("Dubbing Studio"); color: Theme.textPrimary; font.pixelSize: Theme.fontLarge; font.bold: true; elide: Text.ElideRight }
                    Text { text: root.dubbing.hasProject ? qsTr("Project workspace") : qsTr("New project"); color: Theme.textSecondary; font.pixelSize: 10; elide: Text.ElideRight }
                }
            }
            Item { Layout.fillWidth: true }
            PrimaryButton {
                text: root.dubbing.processing ? qsTr("Running…") : qsTr("Generate Final Dubbing")
                iconName: root.dubbing.processing ? "activity" : "play"
                enabled: !root.dubbing.settingsLocked && root.dubbing.sourceMediaPath.length > 0
                onClicked: root.generateRequested()
                AppToolTip { text: qsTr("Run every stage automatically and create the final dubbed output"); visible: parent.hovered }
            }
            PrimaryButton {
                text: qsTr("Colab setup")
                iconName: "cloud"
                quiet: true
                enabled: !root.dubbing.settingsLocked
                onClicked: root.colabSetupRequested()
            }
            PrimaryButton {
                text: qsTr("Workflow")
                iconName: "workflow"
                quiet: true
                enabled: !root.dubbing.settingsLocked
                onClicked: root.workflowRequested()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.paddingSmall

            Flickable {
                id: workflowStepsFlickable
                objectName: "workflowStepsFlickable"
                Layout.fillWidth: true
                Layout.minimumWidth: 260
                Layout.fillHeight: true
                contentWidth: workflowStepsRow.implicitWidth
                contentHeight: height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.HorizontalFlick

                Row {
                    id: workflowStepsRow
                    height: workflowStepsFlickable.height
                    spacing: Theme.paddingSmall
                    Repeater {
                        model: root.steps
                        delegate: DubbingWorkflowStep {
                            required property var modelData
                            width: implicitWidth
                            height: workflowStepsRow.height
                            stepId: modelData.stepId
                            title: modelData.title
                            iconName: modelData.iconName
                            complete: modelData.complete
                            active: modelData.active
                            enabled: true
                            onSelected: root.stepSelected(stepId)
                        }
                    }
                }
                ScrollBar.horizontal: ScrollBar {
                    policy: workflowStepsFlickable.contentWidth > workflowStepsFlickable.width
                            ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                }
            }

            Rectangle {
                Layout.preferredWidth: Math.min(190, statusRow.implicitWidth + 16)
                Layout.minimumWidth: 74
                Layout.preferredHeight: 28
                radius: 14
                color: Qt.rgba(root.dubbing.processing ? Theme.warning.r : Theme.success.r,
                               root.dubbing.processing ? Theme.warning.g : Theme.success.g,
                               root.dubbing.processing ? Theme.warning.b : Theme.success.b, 0.12)
                RowLayout {
                    id: statusRow
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 5
                    Rectangle { Layout.preferredWidth: 6; Layout.preferredHeight: 6; radius: 3; color: root.dubbing.processing ? Theme.warning : Theme.success }
                    Text {
                        Layout.fillWidth: true
                        text: root.statusText
                        color: root.dubbing.processing ? Theme.warning : Theme.success
                        font.pixelSize: Theme.fontSmall
                        font.bold: true
                        elide: Text.ElideRight
                    }
                }
            }
            PrimaryButton { text: qsTr("Save"); iconName: "save"; quiet: true; enabled: root.dubbing.hasProject && !root.dubbing.settingsLocked; onClicked: root.saveRequested() }
            PrimaryButton { text: qsTr("Export"); iconName: "download"; enabled: root.dubbing.hasProject && !root.dubbing.processing && !root.dubbing.settingsLocked; onClicked: root.exportRequested() }
            PrimaryButton { visible: root.dubbing.settingsLocked; text: qsTr("Pause"); iconName: "pause"; quiet: true; onClicked: root.pauseRequested() }
            PrimaryButton { visible: root.dubbing.settingsLocked; text: qsTr("Stop"); iconName: "stop"; buttonColor: Theme.danger; onClicked: root.stopRequested() }
            SidebarToggleButton {
                iconName: "sliders"
                toolTip: root.settingsOpen ? qsTr("Hide task controls") : qsTr("Show task controls")
                active: root.settingsOpen
                enabled: true
                onClicked: root.settingsToggled()
            }
        }
    }

    component SidebarToggleButton: Button {
        id: sidebarButton
        property string iconName: "sliders"
        property string toolTip: ""
        property bool active: false
        implicitWidth: 32
        implicitHeight: 32
        flat: true
        AppToolTip { text: sidebarButton.toolTip; visible: sidebarButton.hovered }
        contentItem: LineIcon {
            anchors.centerIn: parent
            name: sidebarButton.iconName
            color: sidebarButton.active ? Theme.accentLight : (sidebarButton.hovered ? Theme.textPrimary : Theme.textSecondary)
            width: 18
            height: 18
        }
        background: Rectangle {
            radius: 7
            color: sidebarButton.active ? Qt.rgba(0.49, 0.30, 1.0, 0.16) : (sidebarButton.hovered ? Qt.rgba(1, 1, 1, 0.055) : "transparent")
            border.color: sidebarButton.active ? Qt.rgba(0.49, 0.30, 1.0, 0.42) : (sidebarButton.hovered ? Qt.rgba(1, 1, 1, 0.10) : "transparent")
            border.width: 1
        }
        HoverHandler { cursorShape: Qt.PointingHandCursor }
    }
}
