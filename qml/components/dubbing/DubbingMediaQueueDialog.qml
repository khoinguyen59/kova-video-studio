import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../base"
import "../shared"

// This dialog is intentionally hosted by DubbingSourceMediaPanel rather than
// only by the standalone Download route.  A Dubbing user can therefore add,
// select, schedule and run a multi-media job without losing the active
// project/stage context.
Dialog {
    id: root

    required property var dubbing
    property string batchExecutionMode: "per-media"
    property string selectedAction: "import"
    readonly property bool fullWorkflowSelected: selectedAction === "full-workflow"
    readonly property var actionChoices: [
        { "id": "import", "title": qsTr("Import / Normalize") },
        { "id": "isolate", "title": qsTr("Isolator") },
        { "id": "transcribe", "title": qsTr("Transcribe / STT") },
        { "id": "translate", "title": qsTr("Translate") },
        { "id": "voice", "title": qsTr("TTS / Voice") },
        { "id": "export", "title": qsTr("Export / Output") },
        { "id": "full-workflow", "title": qsTr("Full workflow (advanced)") }
    ]
    readonly property int selectedDownloadedCount: {
        var count = 0
        var items = root.dubbing.mediaQueueItems || []
        for (var index = 0; index < items.length; ++index) {
            if (items[index].selected === true && items[index].downloadState === "downloaded")
                count += 1
        }
        return count
    }

    objectName: "dubbingMediaQueueDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    title: qsTr("Downloaded media")
    width: Math.min(980, Math.max(680, parent ? parent.width - Theme.paddingXL * 2 : 860))
    height: Math.min(760, Math.max(520, parent ? parent.height - Theme.paddingXL * 2 : 640))
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    ButtonGroup { id: batchExecutionModeGroup }

    function runSelectedAction() {
        var request = root.fullWorkflowSelected
                ? {
                    "isolate": isolateTask.checked,
                    "transcribe": transcribeTask.checked,
                    "translate": translateTask.checked,
                    "voice": voiceTask.checked,
                    "executionMode": root.batchExecutionMode
                }
                : { "operation": root.selectedAction }
        if (root.dubbing.startMediaQueue(request)) {
            root.close()
        }
    }

    background: Rectangle {
        color: Theme.surface
        radius: Theme.radiusMedium
        border.color: Qt.rgba(1, 1, 1, 0.12)
        border.width: 1
    }

    header: Rectangle {
        implicitHeight: 66
        color: Theme.surfaceAlt
        border.color: Qt.rgba(1, 1, 1, 0.08)
        border.width: 1
        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.paddingMedium
            spacing: Theme.paddingSmall
            LineIcon { name: "download"; color: Theme.accent; Layout.preferredWidth: 22; Layout.preferredHeight: 22 }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text { text: root.title; color: Theme.textPrimary; font.pixelSize: Theme.fontLarge; font.bold: true }
                Text {
                    text: root.dubbing.mediaQueueProcessing
                          ? qsTr("%1% real runner progress").arg(root.dubbing.mediaQueueProgress)
                          : qsTr("Choose any downloaded subset and run one action when you are ready")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                }
            }
            PrimaryButton {
                text: qsTr("Cancel queue")
                iconName: "close"
                quiet: true
                visible: root.dubbing.mediaQueueDownloading || root.dubbing.mediaQueueProcessing
                onClicked: root.dubbing.cancelMediaQueue()
            }
        }
    }

    contentItem: ScrollView {
        id: queueScroll
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: queueScroll.availableWidth
            spacing: Theme.paddingMedium

            Text {
                Layout.fillWidth: true
                Layout.margins: Theme.paddingLarge
                text: qsTr("This is a downloaded-media library. Add/download as many links as you want, then return later and tick a different subset for each action. The selected checkboxes are not a workflow commitment: for example, import 8 files now, translate 4 later, then export 2.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.paddingLarge
                Layout.rightMargin: Theme.paddingLarge
                radius: Theme.radiusSmall
                color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.08)
                border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.28)
                border.width: 1
                implicitHeight: queueSetup.implicitHeight + Theme.paddingMedium * 2

                ColumnLayout {
                    id: queueSetup
                    anchors.fill: parent
                    anchors.margins: Theme.paddingMedium
                    spacing: Theme.paddingSmall
                    Text { text: qsTr("Action for the checked videos"); color: Theme.textPrimary; font.bold: true }
                    ComboBox {
                        id: actionSelector
                        objectName: "dubbingQueueActionSelector"
                        Layout.fillWidth: true
                        model: root.actionChoices
                        textRole: "title"
                        valueRole: "id"
                        currentIndex: {
                            for (var index = 0; index < root.actionChoices.length; ++index) {
                                if (root.actionChoices[index].id === root.selectedAction) return index
                            }
                            return 0
                        }
                        onActivated: root.selectedAction = currentValue
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.fullWorkflowSelected
                              ? qsTr("Advanced mode is optional. It runs the checked tasks together for the currently checked videos only.")
                              : qsTr("Runs only this action for the currently checked files. Prerequisites stay explicit: Isolator/STT require Import, Translate requires STT, TTS requires translated text, and Export requires generated voice audio.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                    }
                    Flow {
                        Layout.fillWidth: true
                        visible: root.fullWorkflowSelected
                        spacing: Theme.paddingMedium
                        CheckBox { id: isolateTask; objectName: "dubbingQueueIsolateTask"; text: qsTr("Isolate audio") }
                        CheckBox { id: transcribeTask; objectName: "dubbingQueueTranscribeTask"; text: qsTr("STT to source.srt") }
                        CheckBox { id: translateTask; objectName: "dubbingQueueTranslateTask"; text: qsTr("Translate to translated.srt") }
                        CheckBox { id: voiceTask; objectName: "dubbingQueueVoiceTask"; text: qsTr("Voice / cloned voice to WAV") }
                    }
                    Text {
                        visible: root.fullWorkflowSelected
                        text: qsTr("Batch execution order")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSmall
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: root.fullWorkflowSelected
                        text: root.batchExecutionMode === "per-media"
                              ? qsTr("Finish every selected task for one video before starting the next video.")
                              : qsTr("Run the current task for every selected video, then advance the whole queue to the next task.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                    }
                    Flow {
                        Layout.fillWidth: true
                        visible: root.fullWorkflowSelected
                        spacing: Theme.paddingMedium
                        RadioButton {
                            id: perMediaOrder
                            objectName: "dubbingQueuePerMediaOrder"
                            text: qsTr("Complete one video, then next")
                            checked: root.batchExecutionMode === "per-media"
                            ButtonGroup.group: batchExecutionModeGroup
                            onToggled: if (checked) root.batchExecutionMode = "per-media"
                        }
                        RadioButton {
                            id: perStageOrder
                            objectName: "dubbingQueuePerStageOrder"
                            text: qsTr("Complete each step for all videos")
                            checked: root.batchExecutionMode === "stage-by-stage"
                            ButtonGroup.group: batchExecutionModeGroup
                            onToggled: if (checked) root.batchExecutionMode = "stage-by-stage"
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.paddingLarge
                Layout.rightMargin: Theme.paddingLarge
                visible: root.dubbing.mediaQueueStatus !== ""
                text: root.dubbing.mediaQueueStatus
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.paddingLarge
                Layout.rightMargin: Theme.paddingLarge
                visible: root.dubbing.lastError !== "" && !root.dubbing.mediaQueueDownloading
                text: root.dubbing.lastError
                color: Theme.error
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }

            Repeater {
                model: root.dubbing.mediaQueueItems
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.paddingLarge
                    Layout.rightMargin: Theme.paddingLarge
                    implicitHeight: itemLayout.implicitHeight + Theme.paddingMedium * 2
                    radius: Theme.radiusSmall
                    color: Theme.surfaceAlt
                    border.color: modelData.processState === "failed" ? Theme.error
                                : (modelData.processState === "completed" ? Theme.success : Qt.rgba(1, 1, 1, 0.10))
                    border.width: 1

                    ColumnLayout {
                        id: itemLayout
                        anchors.fill: parent
                        anchors.margins: Theme.paddingMedium
                        spacing: Theme.paddingSmall
                        RowLayout {
                            Layout.fillWidth: true
                            CheckBox {
                                objectName: "dubbingQueueItemSelection"
                                checked: modelData.selected === true
                                enabled: modelData.downloadState === "downloaded" && modelData.processState !== "running"
                                onToggled: root.dubbing.setMediaQueueItemSelected(modelData.id, checked)
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.displayName || qsTr("Queued media")
                                color: Theme.textPrimary
                                font.bold: true
                                elide: Text.ElideMiddle
                            }
                            Text {
                                text: modelData.processState === "running"
                                      ? qsTr("%1%").arg(modelData.progress || 0)
                                      : (modelData.processState || modelData.downloadState)
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSmall
                            }
                            PrimaryButton {
                                visible: modelData.downloadState === "needs-auth"
                                text: qsTr("Retry with cookies")
                                quiet: true
                                enabled: !root.dubbing.mediaQueueDownloading && !root.dubbing.mediaQueueProcessing
                                onClicked: root.dubbing.retryMediaQueueItem(modelData.id)
                            }
                            PrimaryButton {
                                text: qsTr("Remove")
                                quiet: true
                                enabled: modelData.processState !== "running" && modelData.downloadState !== "downloading"
                                onClicked: root.dubbing.removeMediaQueueItem(modelData.id)
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.status || ""
                            color: (modelData.processState === "failed" || modelData.downloadState === "needs-auth")
                                   ? Theme.error : Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.WrapAnywhere
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: modelData.localPath !== ""
                            text: modelData.localPath || ""
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.WrapAnywhere
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: modelData.outputs && Object.keys(modelData.outputs).length > 0
                            text: {
                                var outputs = modelData.outputs || {}
                                var lines = []
                                if (outputs.sourceSrt) lines.push("STT SRT: " + outputs.sourceSrt)
                                if (outputs.translatedSrt) lines.push("Translated SRT: " + outputs.translatedSrt)
                                if (outputs.voiceWav) lines.push("Voice WAV: " + outputs.voiceWav)
                                if (outputs.vocalsWav) lines.push("Vocals WAV: " + outputs.vocalsWav)
                                if (outputs.backgroundWav) lines.push("Background WAV: " + outputs.backgroundWav)
                                if (outputs.exportedMedia) lines.push("Exported media: " + outputs.exportedMedia)
                                return lines.join("\n")
                            }
                            color: Theme.success
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.paddingLarge
                Layout.rightMargin: Theme.paddingLarge
                Layout.bottomMargin: Theme.paddingLarge
                visible: root.dubbing.mediaQueueItems.length === 0
                text: qsTr("No queued media yet. Add one or more links in Import/Download, then open this panel.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
        }
    }

    footer: Rectangle {
        implicitHeight: 62
        color: Theme.surfaceAlt
        border.color: Qt.rgba(1, 1, 1, 0.08)
        border.width: 1
        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.paddingMedium
            spacing: Theme.paddingSmall
            Text {
                Layout.fillWidth: true
                text: root.selectedDownloadedCount > 0
                      ? qsTr("%1 downloaded item(s) selected for %2").arg(root.selectedDownloadedCount)
                          .arg(root.actionChoices[actionSelector.currentIndex].title)
                      : qsTr("Select at least one downloaded item")
                color: root.selectedDownloadedCount > 0 ? Theme.success : Theme.textSecondary
                font.pixelSize: Theme.fontSmall
            }
            PrimaryButton {
                text: qsTr("Clear finished")
                iconName: "delete"
                quiet: true
                enabled: !root.dubbing.mediaQueueDownloading && !root.dubbing.mediaQueueProcessing
                onClicked: root.dubbing.clearCompletedMediaQueue()
            }
            PrimaryButton {
                objectName: "dubbingQueueRunButton"
                text: root.fullWorkflowSelected ? qsTr("Run selected full workflow") : qsTr("Run selected action")
                iconName: "play"
                enabled: root.selectedDownloadedCount > 0
                         && !root.dubbing.mediaQueueDownloading && !root.dubbing.mediaQueueProcessing
                toolTip: enabled ? qsTr("Run only the selected action using its configured route")
                                 : qsTr("Download and select at least one media item first")
                onClicked: root.runSelectedAction()
            }
            PrimaryButton { text: qsTr("Close"); quiet: true; onClicked: root.close() }
        }
    }
}
