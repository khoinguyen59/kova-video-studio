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
    property string selectedOutputPath: ""
    property string selectedVocalsPath: ""
    property string selectedBackgroundPath: ""
    readonly property bool isIsolation: (spec.nodeId || "") === "source-separate"
    // `processing` and `currentStepId` make this binding refresh whenever the
    // active worker changes.  The controller remains the authority on whether
    // this task, and only this task, may replace the running worker output.
    readonly property var handoffState: {
        var processingState = root.dubbing ? root.dubbing.processing : false
        var activeStep = root.dubbing ? root.dubbing.currentStepId : ""
        return root.dubbing ? root.dubbing.workflowArtifactHandoffStatus(root.nodeId) : ({})
    }
    readonly property bool mayChooseOutput: !dubbing.processing || handoffState.canOverride === true

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
        fileMode: FileDialog.OpenFile
        // FileDialog expects shell patterns ("*.srt"), while the controller
        // still performs the authoritative extension and filename validation.
        nameFilters: [qsTr("Allowed output (%1)").arg(root.allowedFilePatterns().join(" "))]
        onAccepted: root.selectedOutputPath = AppController.files.urlToLocalPath(selectedFile.toString())
    }

    FileDialog {
        id: vocalsDialog
        title: qsTr("Choose vocals.wav from the completed Colab job")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Vocals WAV (vocals.wav)")]
        onAccepted: root.selectedVocalsPath = AppController.files.urlToLocalPath(selectedFile.toString())
    }

    FileDialog {
        id: backgroundDialog
        title: qsTr("Choose background.wav from the completed Colab job")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Background WAV (background.wav)")]
        onAccepted: root.selectedBackgroundPath = AppController.files.urlToLocalPath(selectedFile.toString())
    }

    function allowedFilePatterns() {
        var extensions = root.spec.allowedExtensions || []
        var patterns = []
        for (var index = 0; index < extensions.length; ++index)
            patterns.push("*" + extensions[index])
        return patterns
    }

    function acceptOutput(paths) {
        if (root.dubbing.importWorkflowArtifactFiles(root.nodeId, paths)) {
            root.uploadStatus = qsTr("Accepted. The automatic transfer for this task was stopped and the next task is ready.")
            root.spec = root.dubbing.workflowArtifactSpec(root.nodeId)
            root.selectedOutputPath = ""
            root.selectedVocalsPath = ""
            root.selectedBackgroundPath = ""
        } else {
            root.uploadStatus = root.dubbing.lastError
        }
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
                visible: !root.isIsolation
                text: qsTr("Choose output")
                iconName: "folder"
                enabled: root.mayChooseOutput
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
        Rectangle {
            visible: root.handoffState.active === true
            Layout.fillWidth: true
            implicitHeight: colabHandoffStatus.implicitHeight + Theme.paddingSmall * 2
            radius: Theme.radiusSmall
            color: Qt.rgba(0.95, 0.60, 0.10, 0.10)
            border.color: Qt.rgba(0.95, 0.60, 0.10, 0.36)
            border.width: 1
            ColumnLayout {
                id: colabHandoffStatus
                anchors.fill: parent
                anchors.margins: Theme.paddingSmall
                spacing: 3
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Colab worker is active: %1").arg(root.handoffState.status || qsTr("working"))
                    color: Theme.warning
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    visible: root.handoffState.progressAvailable === true
                    text: qsTr("Measured artifact transfer: %1%").arg(root.handoffState.progress)
                    color: Theme.textSecondary
                    font.pixelSize: 10
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("You may import the exact completed file(s) below. Once accepted, LA Studio cancels this task's automatic Cloudflare transfer and continues with the normal next task.")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                }
            }
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
        ColumnLayout {
            visible: root.isIsolation
            Layout.fillWidth: true
            spacing: Theme.paddingSmall
            Text {
                Layout.fillWidth: true
                text: qsTr("Select both stems separately. LA Studio will not accept source.wav or an arbitrary WAV file.")
                color: Theme.textPrimary
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                PrimaryButton {
                    text: qsTr("Choose vocals.wav")
                    iconName: "folder"
                    Layout.fillWidth: true
                    enabled: root.mayChooseOutput
                    onClicked: vocalsDialog.open()
                }
                Text {
                    Layout.fillWidth: true
                    text: root.selectedVocalsPath === "" ? qsTr("Not selected") : root.selectedVocalsPath
                    color: root.selectedVocalsPath === "" ? Theme.textSecondary : Theme.success
                    font.pixelSize: 10
                    elide: Text.ElideMiddle
                }
            }
            RowLayout {
                Layout.fillWidth: true
                PrimaryButton {
                    text: qsTr("Choose background.wav")
                    iconName: "folder"
                    Layout.fillWidth: true
                    enabled: root.mayChooseOutput
                    onClicked: backgroundDialog.open()
                }
                Text {
                    Layout.fillWidth: true
                    text: root.selectedBackgroundPath === "" ? qsTr("Not selected") : root.selectedBackgroundPath
                    color: root.selectedBackgroundPath === "" ? Theme.textSecondary : Theme.success
                    font.pixelSize: 10
                    elide: Text.ElideMiddle
                }
            }
            PrimaryButton {
                objectName: "dubbingArtifactUploadIsolationContinueButton"
                Layout.fillWidth: true
                text: qsTr("Use uploaded stems and continue")
                iconName: "play"
                enabled: root.mayChooseOutput && root.selectedVocalsPath !== ""
                         && root.selectedBackgroundPath !== ""
                onClicked: root.acceptOutput([root.selectedVocalsPath, root.selectedBackgroundPath])
            }
        }
        ColumnLayout {
            visible: !root.isIsolation
            Layout.fillWidth: true
            spacing: Theme.paddingSmall
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: root.selectedOutputPath === "" ? qsTr("No output selected")
                                                         : root.selectedOutputPath
                    color: root.selectedOutputPath === "" ? Theme.textSecondary : Theme.success
                    font.pixelSize: 10
                    elide: Text.ElideMiddle
                }
                PrimaryButton {
                    text: qsTr("Use uploaded output and continue")
                    iconName: "play"
                    enabled: root.mayChooseOutput && root.selectedOutputPath !== ""
                    onClicked: root.acceptOutput([root.selectedOutputPath])
                }
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Selecting a file does not interrupt Colab. The automatic Cloudflare transfer stops only after you confirm this exact output.")
                color: Theme.textSecondary
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Required name: %1\nAllowed format: %2")
                  .arg((root.spec.expectedFiles || []).join(", "))
                  .arg(root.allowedFilePatterns().join(", "))
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
