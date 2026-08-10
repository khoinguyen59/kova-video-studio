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
    property bool projectStatusOpen: true

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
    Layout.preferredHeight: 58
    color: Qt.rgba(0, 0, 0, 0.10)
    border.color: Qt.rgba(1, 1, 1, 0.06)
    border.width: 1

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.paddingLarge
        anchors.rightMargin: Theme.paddingLarge
        spacing: Theme.paddingMedium

        SidebarToggleButton {
            iconName: "history"
            toolTip: root.historyOpen ? qsTr("Hide dubbing history") : qsTr("Show dubbing history")
            active: root.historyOpen
            onClicked: root.historyToggled()
        }
        PrimaryButton {
            id: projectStatusToggle
            objectName: "dubbingProjectStatusToggle"
            text: root.projectStatusOpen ? qsTr("Hide project setup") : qsTr("Project setup")
            iconName: "sliders"
            quiet: true
            implicitWidth: Math.max(118, requiredContentWidth)
            onClicked: root.projectStatusToggled()
        }

        RowLayout {
            Layout.preferredWidth: 165
            spacing: Theme.paddingSmall
            LineIcon { name: "dubbing"; color: Theme.accentLight; Layout.preferredWidth: 21; Layout.preferredHeight: 21 }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text { text: qsTr("Dubbing Studio"); color: Theme.textPrimary; font.pixelSize: Theme.fontLarge; font.bold: true }
                Text { text: root.dubbing.hasProject ? qsTr("Project workspace") : qsTr("New project"); color: Theme.textSecondary; font.pixelSize: 10 }
            }
        }

        // The flow is deliberately bounded to the left side of the top bar.
        // It scrolls instead of forcing utility actions offscreen on 1280px
        // displays or cutting labels near Export/Output.
        Flickable {
            id: workflowStepsFlickable
            Layout.preferredWidth: Math.max(250, Math.min(root.width * 0.48, workflowStepsRow.implicitWidth))
            Layout.minimumWidth: 220
            Layout.maximumWidth: Math.max(250, root.width * 0.55)
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
                        // Inspection stays available while a worker runs;
                        // individual run controls protect the active stage.
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
            AppToolTip { text: qsTr("Configure and check all Direct Colab GPU stages in one place"); visible: parent.hovered }
        }
        PrimaryButton {
            visible: root.dubbing.settingsLocked
            text: qsTr("Pause")
            iconName: "pause"
            quiet: true
            onClicked: root.pauseRequested()
        }
        PrimaryButton {
            visible: root.dubbing.settingsLocked
            text: qsTr("Stop")
            iconName: "stop"
            buttonColor: Theme.danger
            onClicked: root.stopRequested()
        }
        PrimaryButton {
            text: qsTr("Workflow")
            iconName: "workflow"
            quiet: true
            enabled: !root.dubbing.settingsLocked
            onClicked: root.workflowRequested()
            AppToolTip { text: qsTr("View and configure workflow"); visible: parent.hovered }
        }
        Rectangle {
            implicitWidth: statusRow.implicitWidth + 16
            implicitHeight: 28
            radius: 14
            color: Qt.rgba(root.dubbing.processing ? Theme.warning.r : Theme.success.r,
                           root.dubbing.processing ? Theme.warning.g : Theme.success.g,
                           root.dubbing.processing ? Theme.warning.b : Theme.success.b, 0.12)
            RowLayout {
                id: statusRow
                anchors.centerIn: parent
                spacing: 5
                Rectangle { Layout.preferredWidth: 6; Layout.preferredHeight: 6; radius: 3; color: root.dubbing.processing ? Theme.warning : Theme.success }
                Text { text: root.statusText; color: root.dubbing.processing ? Theme.warning : Theme.success; font.pixelSize: Theme.fontSmall; font.bold: true }
            }
        }
        PrimaryButton { text: qsTr("Save"); iconName: "save"; quiet: true; enabled: root.dubbing.hasProject && !root.dubbing.settingsLocked; onClicked: root.saveRequested() }
        PrimaryButton { text: qsTr("Export"); iconName: "download"; enabled: root.dubbing.hasProject && !root.dubbing.processing && !root.dubbing.settingsLocked; onClicked: root.exportRequested() }
        SidebarToggleButton {
            iconName: "sliders"
            toolTip: root.settingsOpen ? qsTr("Hide settings") : qsTr("Show settings")
            active: root.settingsOpen
            enabled: !root.dubbing.settingsLocked
            onClicked: root.settingsToggled()
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

        AppToolTip {
            text: sidebarButton.toolTip
            visible: sidebarButton.hovered
        }

        contentItem: LineIcon {
            anchors.centerIn: parent
            name: sidebarButton.iconName
            color: sidebarButton.active ? Theme.accentLight
                                        : (sidebarButton.hovered ? Theme.textPrimary : Theme.textSecondary)
            width: 18
            height: 18
        }

        background: Rectangle {
            radius: 7
            color: sidebarButton.active
                   ? Qt.rgba(0.49, 0.30, 1.0, 0.16)
                   : (sidebarButton.hovered ? Qt.rgba(1, 1, 1, 0.055) : "transparent")
            border.color: sidebarButton.active
                          ? Qt.rgba(0.49, 0.30, 1.0, 0.42)
                          : (sidebarButton.hovered ? Qt.rgba(1, 1, 1, 0.10) : "transparent")
            border.width: 1
        }

        HoverHandler { cursorShape: Qt.PointingHandCursor }
    }
}
