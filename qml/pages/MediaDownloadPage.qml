import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import LAStudio
import "../components"
import "../components/base"
import "../components/dubbing"

Rectangle {
    id: root

    color: Theme.background
    signal openDubbingRequested()
    // Kept for route compatibility; this screen only acquires local media.
    signal openSubtitleOcrRequested()
    readonly property var dubbing: AppController.dubbing

    readonly property int readyMediaCount: {
        var count = 0
        var items = root.dubbing.mediaQueueItems || []
        for (var index = 0; index < items.length; ++index) {
            if (items[index].downloadState === "downloaded")
                ++count
        }
        return count
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
                    text: qsTr("Media library")
                    color: Theme.textPrimary
                    font.pixelSize: 26
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Add media in one of two explicit ways, then choose any subset later from Dubbing. This page never starts isolation, STT, translation, voice, or export.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
            }

            ColabMediaAcquisitionPanel {
                Layout.fillWidth: true
                dubbing: root.dubbing
                showLibraryAction: false
                onLocalFilesRequested: localMediaFilesDialog.open()
            }

            Rectangle {
                Layout.fillWidth: true
                radius: Theme.radiusMedium
                color: Theme.surface
                border.color: Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.38)
                border.width: 1
                implicitHeight: mediaLayout.implicitHeight + Theme.paddingLarge * 2

                ColumnLayout {
                    id: mediaLayout
                    anchors.fill: parent
                    anchors.margins: Theme.paddingLarge
                    spacing: Theme.paddingSmall

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Available media")
                            color: Theme.success
                            font.pixelSize: Theme.fontLarge
                            font.bold: true
                        }
                        Text {
                            text: qsTr("%1 ready").arg(root.readyMediaCount)
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Each completed local download and each chosen file is stored as a separate library item. Open Dubbing to select only the files and only the task you want to run.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        PrimaryButton {
                            objectName: "mediaDownloadOpenDubbingButton"
                            text: qsTr("Choose Dubbing actions")
                            iconName: "workflow"
                            enabled: root.readyMediaCount > 0
                            onClicked: root.openDubbingRequested()
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
                            border.color: modelData.downloadState === "failed"
                                          ? Theme.error
                                          : (modelData.downloadState === "downloaded"
                                             ? Theme.success : Qt.rgba(1, 1, 1, 0.10))
                            border.width: 1
                            ColumnLayout {
                                id: itemLayout
                                anchors.fill: parent
                                anchors.margins: Theme.paddingMedium
                                spacing: Theme.paddingSmall
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.displayName || qsTr("Queued media")
                                        color: Theme.textPrimary
                                        font.bold: true
                                        elide: Text.ElideMiddle
                                    }
                                    Text {
                                        text: modelData.sourceMode === "manual-upload"
                                              ? qsTr("Local file") : qsTr("Local download")
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontSmall
                                    }
                                    Text {
                                        text: modelData.downloadState || qsTr("queued")
                                        color: modelData.downloadState === "downloaded" ? Theme.success : Theme.textSecondary
                                        font.pixelSize: Theme.fontSmall
                                    }
                                    PrimaryButton {
                                        visible: modelData.downloadState === "failed"
                                        text: qsTr("Retry download")
                                        quiet: true
                                        enabled: !root.dubbing.mediaQueueDownloading && !root.dubbing.mediaQueueProcessing
                                        onClicked: root.dubbing.retryMediaQueueItem(modelData.id)
                                    }
                                    PrimaryButton {
                                        text: qsTr("Remove")
                                        quiet: true
                                        enabled: !root.dubbing.mediaQueueDownloading && modelData.downloadState !== "downloading"
                                        onClicked: root.dubbing.removeMediaQueueItem(modelData.id)
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    visible: (modelData.status || "") !== ""
                                    text: modelData.status || ""
                                    color: modelData.downloadState === "failed" ? Theme.error : Theme.textSecondary
                                    font.pixelSize: Theme.fontSmall
                                    wrapMode: Text.WrapAnywhere
                                }
                                Text {
                                    Layout.fillWidth: true
                                    visible: (modelData.localPath || "") !== ""
                                    text: modelData.localPath || ""
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSmall
                                    wrapMode: Text.WrapAnywhere
                                }
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: root.dubbing.mediaQueueItems.length === 0
                        text: qsTr("No media yet. Download public links locally, or add files you downloaded yourself.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                    }
                }
            }
        }
    }

    FileDialog {
        id: localMediaFilesDialog
        title: qsTr("Choose local media files")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Media files (*.wav *.mp3 *.flac *.mp4 *.mkv *.mov *.webm *.avi)"), qsTr("All files (*)")]
        onAccepted: {
            var paths = []
            for (var index = 0; index < selectedFiles.length; ++index)
                paths.push(AppController.files.urlToLocalPath(selectedFiles[index].toString()))
            root.dubbing.enqueueMediaFiles(paths)
        }
    }
}
