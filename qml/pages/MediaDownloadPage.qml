import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LAStudio
import "../components"
import "../components/base"

Rectangle {
    id: root

    color: Theme.background
    signal openDubbingRequested()
    signal openSubtitleOcrRequested()

    readonly property var dubbing: AppController.dubbing
    readonly property var subtitleOcr: AppController.subtitleOcr
    property string batchExecutionMode: "per-media"

    ButtonGroup { id: batchExecutionModeGroup }

    function firstSelectedDownloadedPath() {
        var items = root.dubbing.mediaQueueItems
        for (var index = 0; index < items.length; ++index) {
            if (items[index].selected === true && items[index].downloadState === "downloaded"
                    && items[index].localPath) {
                return items[index].localPath
            }
        }
        return ""
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        anchors.margins: Theme.paddingLarge
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: scroll.availableWidth
            spacing: Theme.paddingLarge

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.paddingSmall
                Text {
                    text: qsTr("Download and batch media")
                    color: Theme.textPrimary
                    font.pixelSize: 26
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Queue public media downloads, select several downloaded files, then run the real Dubbing workers serially. The queue never substitutes local inference for a configured Colab or API route.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
            }

            Rectangle {
                Layout.fillWidth: true
                radius: Theme.radiusMedium
                color: Theme.surface
                border.color: Qt.rgba(1, 1, 1, 0.08)
                border.width: 1
                implicitHeight: downloadForm.implicitHeight + Theme.paddingLarge * 2

                ColumnLayout {
                    id: downloadForm
                    anchors.fill: parent
                    anchors.margins: Theme.paddingLarge
                    spacing: Theme.paddingMedium

                    Text {
                        text: qsTr("Public media links")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontLarge
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("One HTTPS link per line. Direct media files and public YouTube, TikTok, and Douyin pages are supported. Playlists, login/cookies, DRM/paywalls, user-info URLs, and unsafe redirects are blocked. HTTP is allowed only for local loopback testing.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                    }
                    TextArea {
                        id: sourceUrl
                        Layout.fillWidth: true
                        Layout.minimumHeight: 138
                        enabled: !root.dubbing.mediaQueueProcessing
                        placeholderText: qsTr("One public media link per line\nhttps://example.com/video-1.mp4\nhttps://www.youtube.com/watch?v=...")
                        selectByMouse: true
                        color: Theme.textPrimary
                        placeholderTextColor: Theme.textSecondary
                        wrapMode: TextEdit.WrapAnywhere
                        leftPadding: Theme.paddingMedium; rightPadding: Theme.paddingMedium
                        topPadding: Theme.paddingMedium; bottomPadding: Theme.paddingMedium
                        background: Rectangle {
                            radius: Theme.radiusSmall
                            color: Qt.rgba(1, 1, 1, 0.035)
                            border.color: sourceUrl.activeFocus ? Theme.accent : Theme.surfaceAlt
                            border.width: sourceUrl.activeFocus ? 2 : 1
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.paddingSmall
                        PrimaryButton {
                            text: qsTr("Add links to download queue")
                            iconName: "download"
                            enabled: sourceUrl.text.trim() !== "" && !root.dubbing.mediaQueueProcessing
                            onClicked: {
                                if (root.dubbing.enqueueMediaLinks(sourceUrl.text) > 0)
                                    sourceUrl.clear()
                            }
                        }
                        PrimaryButton {
                            text: qsTr("Cancel batch")
                            iconName: "close"
                            quiet: true
                            visible: root.dubbing.mediaQueueDownloading || root.dubbing.mediaQueueProcessing
                            onClicked: root.dubbing.cancelMediaQueue()
                        }
                        Item { Layout.fillWidth: true }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: root.dubbing.mediaQueueStatus !== ""
                        text: root.dubbing.mediaQueueStatus
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: root.dubbing.lastError !== "" && !root.dubbing.mediaQueueDownloading
                        text: root.dubbing.lastError
                        color: Theme.error
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                radius: Theme.radiusMedium
                color: Theme.surface
                border.color: Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.38)
                border.width: 1
                implicitHeight: queueLayout.implicitHeight + Theme.paddingLarge * 2

                ColumnLayout {
                    id: queueLayout
                    anchors.fill: parent
                    anchors.margins: Theme.paddingLarge
                    spacing: Theme.paddingSmall

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Downloaded media queue")
                            color: Theme.success
                            font.pixelSize: Theme.fontLarge
                            font.bold: true
                        }
                        Text {
                            text: root.dubbing.mediaQueueProcessing
                                  ? qsTr("%1% real runner progress").arg(root.dubbing.mediaQueueProgress)
                                  : qsTr("Select downloaded files")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Select any downloaded items and queue the real Dubbing tasks. Each item owns an output folder: STT writes source.srt, translation writes translated.srt, voice writes voice.wav, and isolation writes vocals.wav plus background.wav. Translation and voice include their required real dependency stages automatically; no local fallback is introduced.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.paddingMedium
                        CheckBox { id: isolateTask; text: qsTr("Isolate audio") }
                        CheckBox { id: transcribeTask; text: qsTr("STT to source.srt") }
                        CheckBox { id: translateTask; text: qsTr("Translate to translated.srt") }
                        CheckBox { id: voiceTask; text: qsTr("Voice / cloned voice to WAV") }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Text {
                            text: qsTr("Batch execution order")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSmall
                            font.bold: true
                        }
                        Text {
                            Layout.fillWidth: true
                            text: root.batchExecutionMode === "per-media"
                                  ? qsTr("Finish every selected task for one video before starting the next video.")
                                  : qsTr("Run the current task for every selected video, then advance the whole queue to the next task.")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.paddingMedium
                            RadioButton {
                                id: perMediaOrder
                                text: qsTr("Complete one video, then next")
                                checked: root.batchExecutionMode === "per-media"
                                ButtonGroup.group: batchExecutionModeGroup
                                onToggled: if (checked) root.batchExecutionMode = "per-media"
                            }
                            RadioButton {
                                id: perStageOrder
                                text: qsTr("Complete each step for all videos")
                                checked: root.batchExecutionMode === "stage-by-stage"
                                ButtonGroup.group: batchExecutionModeGroup
                                onToggled: if (checked) root.batchExecutionMode = "stage-by-stage"
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        PrimaryButton {
                            text: qsTr("Run selected batch")
                            iconName: "play"
                            enabled: !root.dubbing.mediaQueueDownloading && !root.dubbing.mediaQueueProcessing
                            onClicked: {
                                if (root.dubbing.startMediaQueue({
                                        "isolate": isolateTask.checked,
                                        "transcribe": transcribeTask.checked,
                                        "translate": translateTask.checked,
                                        "voice": voiceTask.checked,
                                        "executionMode": root.batchExecutionMode
                                    })) {
                                    root.openDubbingRequested()
                                }
                            }
                        }
                        PrimaryButton {
                            text: qsTr("Use in Subtitle OCR")
                            iconName: "scan"
                            quiet: true
                            enabled: !root.dubbing.mediaQueueDownloading && !root.dubbing.mediaQueueProcessing
                                     && root.firstSelectedDownloadedPath() !== ""
                            onClicked: {
                                if (root.subtitleOcr.useDownloadedMedia(root.firstSelectedDownloadedPath()))
                                    root.openSubtitleOcrRequested()
                            }
                        }
                        PrimaryButton {
                            text: qsTr("Clear finished")
                            iconName: "delete"
                            quiet: true
                            enabled: !root.dubbing.mediaQueueDownloading && !root.dubbing.mediaQueueProcessing
                            onClicked: root.dubbing.clearCompletedMediaQueue()
                        }
                        Item { Layout.fillWidth: true }
                    }

                    Repeater {
                        model: root.dubbing.mediaQueueItems
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
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
                                        text: qsTr("Remove")
                                        quiet: true
                                        enabled: modelData.processState !== "running" && modelData.downloadState !== "downloading"
                                        onClicked: root.dubbing.removeMediaQueueItem(modelData.id)
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.status || ""
                                    color: modelData.processState === "failed" ? Theme.error : Theme.textSecondary
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
                        visible: root.dubbing.mediaQueueItems.length === 0
                        text: qsTr("No queued media yet. Add one or more links above.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                    }
                }
            }
        }
    }
}
