import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import LAStudio
import "../components"
import "../components/base"

Rectangle {
    id: root

    color: Theme.background
    signal openDubbingRequested()
    // Kept for route compatibility; Download no longer runs OCR or Dubbing tasks.
    signal openSubtitleOcrRequested()

    readonly property var dubbing: AppController.dubbing

    function downloadedCount() {
        var count = 0
        var items = root.dubbing.mediaQueueItems || []
        for (var index = 0; index < items.length; ++index) {
            if (items[index].downloadState === "downloaded") count += 1
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
                    text: qsTr("Download media")
                    color: Theme.textPrimary
                    font.pixelSize: 26
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Download public media here. No media processing starts on this page; choose downloaded files later from Dubbing.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
            }

            Rectangle {
                Layout.fillWidth: true
                radius: Theme.radiusMedium
                color: Qt.rgba(0.45, 0.20, 1.0, 0.08)
                border.color: Qt.rgba(0.55, 0.35, 1.0, 0.35)
                border.width: 1
                implicitHeight: browserSessionLayout.implicitHeight + Theme.paddingLarge * 2

                ColumnLayout {
                    id: browserSessionLayout
                    anchors.fill: parent
                    anchors.margins: Theme.paddingLarge
                    spacing: Theme.paddingSmall
                    Text {
                        text: qsTr("Douyin Chromium session")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontLarge
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.dubbing.douyinBrowserVerified
                              ? qsTr("Connected. Douyin downloads use the LA Studio profile; Chrome and Edge cookies are never read.")
                              : qsTr("Optional. Set up a separate Chromium profile, sign in to Douyin, then check the connection before downloading.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.paddingSmall
                        PrimaryButton {
                            objectName: "mediaDownloadBrowserSetupButton"
                            text: qsTr("Set up Chromium")
                            iconName: "folder"
                            enabled: !root.dubbing.douyinBrowserBusy
                                     && !root.dubbing.mediaQueueDownloading
                                     && !root.dubbing.mediaQueueProcessing
                            onClicked: root.dubbing.openDouyinBrowserSession()
                        }
                        PrimaryButton {
                            objectName: "mediaDownloadBrowserCheckButton"
                            text: qsTr("Check connection")
                            iconName: "play"
                            quiet: true
                            enabled: root.dubbing.douyinBrowserConfigured
                                     && !root.dubbing.douyinBrowserBusy
                                     && !root.dubbing.mediaQueueDownloading
                                     && !root.dubbing.mediaQueueProcessing
                            onClicked: root.dubbing.checkDouyinBrowserSession()
                        }
                        PrimaryButton {
                            objectName: "mediaDownloadBrowserDisableButton"
                            visible: root.dubbing.douyinBrowserVerified
                            text: qsTr("Disable")
                            iconName: "close"
                            quiet: true
                            onClicked: root.dubbing.disconnectDouyinBrowserSession()
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.dubbing.douyinBrowserStatus !== ""
                              ? root.dubbing.douyinBrowserStatus
                              : (root.dubbing.douyinBrowserAvailable
                                 ? qsTr("Chromium helper is ready.")
                                 : qsTr("Chromium helper is not available. Install Playwright and Chromium in the configured Python environment."))
                        color: root.dubbing.douyinBrowserVerified ? Theme.success : Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                    }
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
                        text: qsTr("Paste one HTTPS link per line. Direct media files and public YouTube, TikTok, and Douyin pages are supported. Douyin share text is filtered to the URL before the download starts.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                    }
                    TextArea {
                        id: sourceUrl
                        objectName: "mediaDownloadLinkInput"
                        Layout.fillWidth: true
                        Layout.minimumHeight: 138
                        enabled: !root.dubbing.mediaQueueProcessing
                        placeholderText: qsTr("Paste public links here, one per line\nhttps://example.com/video-1.mp4\nhttps://www.youtube.com/watch?v=...")
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
                            objectName: "mediaDownloadButton"
                            text: qsTr("Download")
                            iconName: "download"
                            enabled: sourceUrl.text.trim() !== "" && !root.dubbing.mediaQueueProcessing
                            onClicked: {
                                if (root.dubbing.enqueueMediaLinks(sourceUrl.text) > 0)
                                    sourceUrl.clear()
                            }
                        }
                        PrimaryButton {
                            objectName: "mediaDownloadCancelButton"
                            text: qsTr("Cancel downloads")
                            iconName: "close"
                            quiet: true
                            visible: root.dubbing.mediaQueueDownloading
                            onClicked: root.dubbing.cancelMediaQueue()
                        }
                        Item { Layout.fillWidth: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.paddingSmall
                        Text {
                            Layout.fillWidth: true
                            text: root.dubbing.douyinCookieConfigured
                                  ? qsTr("Douyin cookies: %1 (temporary for this download run)").arg(root.dubbing.douyinCookieFileName)
                                  : qsTr("Netscape cookies are optional and used only when Douyin requires them. Browser cookies are never imported automatically.")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                        }
                        PrimaryButton {
                            text: qsTr("Choose Douyin cookies")
                            iconName: "folder"
                            quiet: true
                            enabled: !root.dubbing.mediaQueueDownloading && !root.dubbing.mediaQueueProcessing
                            onClicked: douyinCookieFileDialog.open()
                        }
                        PrimaryButton {
                            visible: root.dubbing.douyinCookieConfigured
                            text: qsTr("Clear")
                            iconName: "close"
                            quiet: true
                            enabled: !root.dubbing.mediaQueueDownloading && !root.dubbing.mediaQueueProcessing
                            onClicked: root.dubbing.clearDouyinCookieFile()
                        }
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
                implicitHeight: downloadedLayout.implicitHeight + Theme.paddingLarge * 2

                ColumnLayout {
                    id: downloadedLayout
                    anchors.fill: parent
                    anchors.margins: Theme.paddingLarge
                    spacing: Theme.paddingSmall

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Downloaded media")
                            color: Theme.success
                            font.pixelSize: Theme.fontLarge
                            font.bold: true
                        }
                        Text {
                            text: qsTr("%1 file(s)").arg(root.downloadedCount())
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Downloaded files stay here until you choose them later from Dubbing.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        PrimaryButton {
                            objectName: "mediaDownloadOpenDubbingButton"
                            text: qsTr("Open Dubbing actions")
                            iconName: "workflow"
                            enabled: root.downloadedCount() > 0
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
                            border.color: modelData.downloadState === "failed" || modelData.downloadState === "needs-auth"
                                          ? Theme.error
                                          : (modelData.downloadState === "downloaded" ? Theme.success : Qt.rgba(1, 1, 1, 0.10))
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
                                        text: modelData.downloadState || qsTr("queued")
                                        color: modelData.downloadState === "downloaded" ? Theme.success : Theme.textSecondary
                                        font.pixelSize: Theme.fontSmall
                                    }
                                    PrimaryButton {
                                        visible: modelData.downloadState === "needs-auth"
                                        text: qsTr("Retry")
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
                                    text: modelData.status || ""
                                    color: modelData.downloadState === "failed" || modelData.downloadState === "needs-auth"
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
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: root.dubbing.mediaQueueItems.length === 0
                        text: qsTr("No downloads yet. Paste one or more public links above.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                    }
                }
            }
        }
    }

    FileDialog {
        id: douyinCookieFileDialog
        title: qsTr("Choose a fresh Douyin Netscape cookie file")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Netscape cookie files (*.txt *.cookies)"), qsTr("All files (*)")]
        onAccepted: root.dubbing.setDouyinCookieFile(AppController.files.urlToLocalPath(selectedFile.toString()))
    }
}
