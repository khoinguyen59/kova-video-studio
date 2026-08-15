import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import "../base"
import LAStudio

// Manual handoff for a completed Direct Colab task.  This is deliberately
// separate from Import/Download: a worker output is accepted only by the
// selected task's controller allow-list and is copied into the project cache.
Rectangle {
    id: root

    required property var dubbing
    required property string nodeId
    property var spec: dubbing ? dubbing.workflowArtifactSpec(nodeId) : ({})
    property string uploadStatus: ""

    visible: spec && spec.nodeId !== undefined && spec.nodeId !== ""
    Layout.fillWidth: true
    implicitHeight: content.implicitHeight + Theme.paddingMedium * 2
    radius: Theme.radiusSmall
    color: Qt.rgba(0.20, 0.55, 0.95, 0.07)
    border.color: Qt.rgba(0.20, 0.55, 0.95, 0.34)
    border.width: 1

    FileDialog {
        id: artifactDialog
        title: qsTr("Choose the Colab output for %1").arg(root.spec.title || root.nodeId)
        fileMode: root.spec.multiple ? FileDialog.OpenFiles : FileDialog.OpenFile
        // FileDialog expects shell patterns ("*.srt"), while the controller
        // still performs the authoritative extension and filename validation.
        nameFilters: [qsTr("Allowed output (%1)").arg(root.allowedFilePatterns().join(" "))]
        onAccepted: {
            var selected = root.spec.multiple ? selectedFiles : [selectedFile]
            var paths = []
            for (var index = 0; index < selected.length; ++index)
                paths.push(AppController.files.urlToLocalPath(selected[index].toString()))
            if (root.dubbing.importWorkflowArtifactFiles(root.nodeId, paths)) {
                root.uploadStatus = qsTr("Accepted and copied into the project cache.")
                root.spec = root.dubbing.workflowArtifactSpec(root.nodeId)
            } else {
                root.uploadStatus = root.dubbing.lastError
            }
        }
    }

    function allowedFilePatterns() {
        var extensions = root.spec.allowedExtensions || []
        var patterns = []
        for (var index = 0; index < extensions.length; ++index)
            patterns.push("*" + extensions[index])
        return patterns
    }

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: Theme.paddingMedium
        spacing: Theme.paddingSmall

        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: qsTr("Upload completed %1 output").arg(root.spec.title || root.nodeId)
                color: Theme.textPrimary
                font.bold: true
            }
            PrimaryButton {
                objectName: "dubbingArtifactUploadButton"
                text: qsTr("Upload output")
                iconName: "folder"
                enabled: !root.dubbing.processing
                onClicked: artifactDialog.open()
            }
        }
        Text {
            Layout.fillWidth: true
            text: root.spec.description || ""
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Colab save folder: %1").arg(root.spec.colabFolder || "")
            color: Theme.textPrimary
            font.pixelSize: 10
            wrapMode: Text.WrapAnywhere
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Worker path: %1").arg(root.spec.workerPath || "")
            color: Theme.textSecondary
            font.pixelSize: 10
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Accepted: %1").arg(root.allowedFilePatterns().join(", "))
            color: Theme.textSecondary
            font.pixelSize: 10
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            visible: root.uploadStatus !== ""
            text: root.uploadStatus
            color: root.uploadStatus.indexOf("Accepted") === 0 ? Theme.success : Theme.error
            font.pixelSize: 10
            wrapMode: Text.WordWrap
        }
    }
}
