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
    // At compact editor widths, keep actions available as icon buttons with
    // tooltips.  A partially visible word such as "Wor" is not a usable
    // control; the workflow rail is the only header region permitted to scroll.
    readonly property bool compactHeaderChrome: width < 1460
    // At desktop widths a clipped action label is worse than a deliberate
    // icon control with its tooltip.  Reserve enough rail width for stage
    // labels before showing the full action captions.
    readonly property bool compactActionCluster: width < 1980

    signal stepSelected(string stepId)
    signal historyToggled()
    signal settingsToggled()
    signal projectStatusToggled()
    signal generateRequested()
    signal pauseRequested()
    signal stopRequested()
    signal workflowRequested()
    signal colabSetupRequested()
    signal newProjectRequested()
    signal openProjectRequested()
    signal saveRequested()
    signal saveProjectAsRequested()
    signal exportRequested()

    function qmlSmokeClickProjectStatusToggle() {
        if (!projectStatusToggle.enabled)
            return false
        projectStatusToggle.click()
        return true
    }

    // The header has one flexible task rail and one fixed action cluster.
    // Exercise that contract at runtime so a regression cannot silently turn
    // a complete action label into a clipped fragment such as "Wor".
    function qmlSmokeLayoutCheck() {
        if (workflowStepsFlickable.width <= 0
                || headerActionCluster.width <= 0
                || workflowStepsFlickable.x + workflowStepsFlickable.width
                   > headerActionCluster.x + 1)
            return false

        var actions = [generateAction, colabAction, workflowAction]
        for (var index = 0; index < actions.length; ++index) {
            var action = actions[index]
            if (action.width <= 0 || action.x + action.width > root.width + 1)
                return false
            if (root.compactActionCluster) {
                // Generate intentionally keeps its semantic text for
                // accessibility while PrimaryButton hides it visually in
                // icon-only mode. Check the rendered mode and footprint, not
                // the internal accessible label.
                if (!action.iconOnly || action.width < 38 || action.toolTip === "")
                    return false
            } else if (action.iconOnly || action.width + 1 < action.requiredContentWidth) {
                return false
            }
        }

        return root.compactActionCluster || workflowAction.text === qsTr("Workflow")
    }

    Layout.fillWidth: true
    Layout.preferredHeight: 52
    Layout.minimumHeight: 52
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
            text: root.compactHeaderChrome ? "" : qsTr("Project setup")
            iconName: "sliders"
            iconOnly: root.compactHeaderChrome
            quiet: true
            Layout.minimumWidth: root.compactHeaderChrome ? 38 : requiredContentWidth
            Layout.preferredWidth: root.compactHeaderChrome ? 38 : Math.max(112, requiredContentWidth)
            toolTip: qsTr("Project setup")
            accessibleName: toolTip
            onClicked: root.projectStatusToggled()
        }

        RowLayout {
            Layout.preferredWidth: root.compactHeaderChrome ? 28 : 154
            Layout.minimumWidth: root.compactHeaderChrome ? 28 : 132
            Layout.maximumWidth: root.compactHeaderChrome ? 28 : 154
            spacing: Theme.paddingSmall
            LineIcon {
                name: "dubbing"
                color: Theme.accentLight
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
            }
            ColumnLayout {
                Layout.fillWidth: !root.compactHeaderChrome
                visible: !root.compactHeaderChrome
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
                id: generateAction
                iconName: root.dubbing.processing ? "activity" : "play"
                iconOnly: root.compactActionCluster
                Layout.minimumWidth: root.compactActionCluster ? 38 : requiredContentWidth
                Layout.preferredWidth: root.compactActionCluster ? 38 : Math.max(126, requiredContentWidth)
                enabled: !root.dubbing.settingsLocked && root.dubbing.sourceMediaPath.length > 0
                toolTip: root.dubbing.processing ? qsTr("Running automatic dubbing") : qsTr("Generate final dubbing")
                accessibleName: toolTip
                onClicked: root.generateRequested()
            }
            PrimaryButton {
                text: root.compactActionCluster ? "" : qsTr("Colab")
                id: colabAction
                iconName: "cloud"
                iconOnly: root.compactActionCluster
                quiet: true
                Layout.minimumWidth: root.compactActionCluster ? 38 : requiredContentWidth
                Layout.preferredWidth: root.compactActionCluster ? 38 : Math.max(82, requiredContentWidth)
                enabled: !root.dubbing.settingsLocked
                toolTip: qsTr("Open Colab setup")
                accessibleName: toolTip
                onClicked: root.colabSetupRequested()
            }
            PrimaryButton {
                text: root.compactActionCluster ? "" : qsTr("Workflow")
                id: workflowAction
                iconName: "workflow"
                iconOnly: root.compactActionCluster
                quiet: true
                Layout.minimumWidth: root.compactActionCluster ? 38 : requiredContentWidth
                Layout.preferredWidth: root.compactActionCluster ? 38 : Math.max(112, requiredContentWidth)
                enabled: !root.dubbing.settingsLocked
                toolTip: qsTr("Open workflow")
                accessibleName: toolTip
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
            text: qsTr("New project…")
            enabled: !root.dubbing.processing && !root.dubbing.settingsLocked
            onTriggered: root.newProjectRequested()
        }
        MenuItem {
            text: qsTr("Open project…")
            enabled: !root.dubbing.processing && !root.dubbing.settingsLocked
            onTriggered: root.openProjectRequested()
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Save project")
            enabled: root.dubbing.hasProject && !root.dubbing.settingsLocked
            onTriggered: root.saveRequested()
        }
        MenuItem {
            text: qsTr("Save project as…")
            enabled: root.dubbing.hasProject && !root.dubbing.processing && !root.dubbing.settingsLocked
            onTriggered: root.saveProjectAsRequested()
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
