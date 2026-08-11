pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../base"
import "../shared"
import LAStudio

// A compact editor header.  Workflow stages own the flexible portion of the
// bar; project actions retain their complete labels and never compete with the
// stages for the same horizontal space.  This mirrors an NLE toolbar: the
// stage rail can scroll, while actions are fixed controls on the right.
Rectangle {
    id: root

    required property var dubbing
    required property var steps
    required property string statusText
    required property string defaultExportPath
    property bool historyOpen: false
    property bool settingsOpen: false
    // Kept for the production route smoke: project settings are a dialog, not
    // a permanent strip below the editor.
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
    Layout.preferredHeight: 60
    Layout.minimumHeight: 60
    color: Qt.rgba(0, 0, 0, 0.10)
    border.color: Qt.rgba(1, 1, 1, 0.06)
    border.width: 1

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.paddingMedium
        anchors.rightMargin: Theme.paddingMedium
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
            text: qsTr("Project setup")
            iconName: "sliders"
            quiet: true
            Layout.minimumWidth: requiredContentWidth
            Layout.preferredWidth: Math.max(112, requiredContentWidth)
            onClicked: root.projectStatusToggled()
        }

        RowLayout {
            Layout.preferredWidth: 154
            Layout.minimumWidth: 132
            Layout.maximumWidth: 154
            spacing: Theme.paddingSmall
            LineIcon {
                name: "dubbing"
                color: Theme.accentLight
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Dubbing Studio")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontMedium
                    font.bold: true
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: root.dubbing.hasProject ? qsTr("Project workspace") : qsTr("New project")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
            }
        }

        // This is the only flexible area in the header.  It scrolls the full
        // task labels instead of squeezing another fixed button into fragments
        // such as "Wor".
        Flickable {
            id: workflowStepsFlickable
            objectName: "workflowStepsFlickable"
            Layout.fillWidth: true
            Layout.minimumWidth: 150
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

        // The action cluster never scrolls or shares width with the task rail.
        // Labels therefore remain whole at the supported editor minimum size.
        RowLayout {
            id: headerActionCluster
            spacing: Theme.paddingSmall

            PrimaryButton {
                text: root.dubbing.processing ? qsTr("Running…") : qsTr("Generate")
                iconName: root.dubbing.processing ? "activity" : "play"
                Layout.minimumWidth: requiredContentWidth
                Layout.preferredWidth: Math.max(126, requiredContentWidth)
                enabled: !root.dubbing.settingsLocked && root.dubbing.sourceMediaPath.length > 0
                onClicked: root.generateRequested()
            }
            PrimaryButton {
                text: qsTr("Colab")
                iconName: "cloud"
                quiet: true
                Layout.minimumWidth: requiredContentWidth
                Layout.preferredWidth: Math.max(82, requiredContentWidth)
                enabled: !root.dubbing.settingsLocked
                toolTip: qsTr("Open Colab setup")
                onClicked: root.colabSetupRequested()
            }
            PrimaryButton {
                text: qsTr("Workflow")
                iconName: "workflow"
                quiet: true
                Layout.minimumWidth: requiredContentWidth
                Layout.preferredWidth: Math.max(112, requiredContentWidth)
                enabled: !root.dubbing.settingsLocked
                onClicked: root.workflowRequested()
            }
            PrimaryButton {
                id: moreActionsButton
                iconName: "more-horizontal"
                iconOnly: true
                quiet: true
                toolTip: qsTr("More Dubbing actions")
                accessibleName: toolTip
                onClicked: actionMenu.open()
            }
        }
    }

    Menu {
        id: actionMenu
        x: Math.max(0, moreActionsButton.x + moreActionsButton.width - width)
        y: moreActionsButton.y + moreActionsButton.height
        width: 220

        MenuItem {
            text: qsTr("Save project")
            enabled: root.dubbing.hasProject && !root.dubbing.settingsLocked
            onTriggered: root.saveRequested()
        }
        MenuItem {
            text: qsTr("Export / Output")
            enabled: root.dubbing.hasProject && !root.dubbing.processing && !root.dubbing.settingsLocked
            onTriggered: root.exportRequested()
        }
        MenuSeparator { visible: root.dubbing.settingsLocked }
        MenuItem {
            visible: root.dubbing.settingsLocked
            text: qsTr("Pause automatic workflow")
            onTriggered: root.pauseRequested()
        }
        MenuItem {
            visible: root.dubbing.settingsLocked
            text: qsTr("Stop processing")
            onTriggered: root.stopRequested()
        }
        MenuSeparator {}
        MenuItem {
            text: root.settingsOpen ? qsTr("Hide task controls") : qsTr("Show task controls")
            onTriggered: root.settingsToggled()
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
