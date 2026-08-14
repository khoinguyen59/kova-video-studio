import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import LAStudio
import "../base"

// Project selection is deliberately global application chrome rather than a
// Dubbing-page step.  Every production studio shares this gate, so an operator
// cannot configure a model, media route or worker and only afterwards discover
// that there is nowhere durable to save the work.
Dialog {
    id: root

    property string requestedFeatureLabel: ""
    property string actionError: ""
    signal projectReady()
    signal leaveRequested()

    function openFor(featureLabel) {
        requestedFeatureLabel = featureLabel || qsTr("this feature")
        actionError = ""
        open()
    }

    function createProject(url) {
        actionError = ""
        if (!AppController.dubbing.newProject(url.toString())) {
            actionError = AppController.dubbing.lastError || qsTr("LA Studio could not create the project.")
            return
        }
        close()
        projectReady()
    }

    function openProject(url) {
        actionError = ""
        if (!AppController.dubbing.openProject(url.toString())) {
            actionError = AppController.dubbing.lastError || qsTr("LA Studio could not open the project.")
            return
        }
        close()
        projectReady()
    }

    objectName: "globalProjectGate"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.NoAutoClose
    width: Math.min(700, Math.max(520, parent ? parent.width - Theme.paddingXL * 2 : 700))

    background: Rectangle {
        color: Theme.surface
        radius: Theme.radiusMedium
        border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.64)
        border.width: 1
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.paddingLarge
            spacing: Theme.paddingMedium

            Rectangle {
                Layout.preferredWidth: 42
                Layout.preferredHeight: 42
                radius: Theme.radiusSmall
                color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)
                LineIcon { anchors.centerIn: parent; name: "folder"; color: Theme.accentLight; width: 21; height: 21 }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Text {
                    text: qsTr("Choose an LA Studio project first")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontXLarge
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("%1 is available after you create a new project or open an existing .ladub.json project.").arg(root.requestedFeatureLabel)
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Qt.rgba(1, 1, 1, 0.08) }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.paddingLarge
            spacing: Theme.paddingMedium

            Text {
                Layout.fillWidth: true
                text: qsTr("A project saves the source media, selected routes and model choices, staged outputs and review state. Nothing in a studio starts before this choice.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.paddingMedium

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: createProjectColumn.implicitHeight + Theme.paddingLarge * 2
                    radius: Theme.radiusSmall
                    color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.08)
                    border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.48)

                    ColumnLayout {
                        id: createProjectColumn
                        anchors.fill: parent
                        anchors.margins: Theme.paddingLarge
                        spacing: Theme.paddingSmall
                        Text { text: qsTr("Create new project"); color: Theme.textPrimary; font.bold: true; font.pixelSize: Theme.fontLarge }
                        Text { Layout.fillWidth: true; text: qsTr("Choose a name and folder for a new reusable workspace."); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall; wrapMode: Text.WordWrap }
                        PrimaryButton {
                            objectName: "globalProjectCreateButton"
                            text: qsTr("Create new project")
                            iconName: "plus"
                            Layout.fillWidth: true
                            onClicked: newProjectFileDialog.open()
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: openProjectColumn.implicitHeight + Theme.paddingLarge * 2
                    radius: Theme.radiusSmall
                    color: Qt.rgba(1, 1, 1, 0.025)
                    border.color: Qt.rgba(1, 1, 1, 0.12)

                    ColumnLayout {
                        id: openProjectColumn
                        anchors.fill: parent
                        anchors.margins: Theme.paddingLarge
                        spacing: Theme.paddingSmall
                        Text { text: qsTr("Open existing project"); color: Theme.textPrimary; font.bold: true; font.pixelSize: Theme.fontLarge }
                        Text { Layout.fillWidth: true; text: qsTr("Resume saved media, model choices and completed steps."); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall; wrapMode: Text.WordWrap }
                        PrimaryButton {
                            objectName: "globalProjectOpenButton"
                            text: qsTr("Open existing project")
                            iconName: "folder"
                            quiet: true
                            Layout.fillWidth: true
                            onClicked: openProjectFileDialog.open()
                        }
                    }
                }
            }

            Text {
                visible: root.actionError !== ""
                Layout.fillWidth: true
                text: root.actionError
                color: Theme.danger
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                PrimaryButton {
                    objectName: "globalProjectLeaveButton"
                    text: qsTr("Back to Home")
                    iconName: "arrow-left"
                    quiet: true
                    onClicked: {
                        root.close()
                        root.leaveRequested()
                    }
                }
            }
        }
    }

    FileDialog {
        id: newProjectFileDialog
        objectName: "globalProjectNewFileDialog"
        title: qsTr("Create LA Studio project")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "ladub.json"
        nameFilters: [qsTr("LA Studio project (*.ladub.json)"), qsTr("All files (*)")]
        onAccepted: root.createProject(selectedFile)
    }

    FileDialog {
        id: openProjectFileDialog
        objectName: "globalProjectOpenFileDialog"
        title: qsTr("Open LA Studio project")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("LA Studio project (*.ladub.json)"), qsTr("All files (*)")]
        onAccepted: root.openProject(selectedFile)
    }
}
