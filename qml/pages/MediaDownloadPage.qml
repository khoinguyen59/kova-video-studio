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

    function formatBytes(bytes) {
        if (bytes < 0) return ""
        if (bytes < 1024) return bytes + " B"
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KiB"
        if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + " MiB"
        return (bytes / (1024 * 1024 * 1024)).toFixed(2) + " GiB"
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
                    text: qsTr("Download a direct video/audio file or a supported public YouTube, TikTok, or Douyin video into LA Studio-owned staging, then choose when to validate it in Dubbing.")
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
                        text: qsTr("Media or public video URL")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontLarge
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Supported: direct HTTPS media files and public YouTube, TikTok, and Douyin video pages. Only one video is accepted; playlists, login/cookies, DRM/paywalls, user-info URLs, and unsafe redirects are blocked. HTTP is allowed only for local loopback testing.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                    }
                    TextField {
                        id: sourceUrl
                        Layout.fillWidth: true
                        enabled: !root.dubbing.linkImporting && !root.dubbing.processing
                        placeholderText: qsTr("https://example.com/video.mp4 or https://www.youtube.com/watch?v=...")
                        selectByMouse: true
                        color: Theme.textPrimary
                        placeholderTextColor: Theme.textSecondary
                        leftPadding: Theme.paddingMedium
                        rightPadding: Theme.paddingMedium
                        background: Rectangle {
                            radius: Theme.radiusSmall
                            color: Qt.rgba(1, 1, 1, 0.035)
                            border.color: sourceUrl.activeFocus ? Theme.accent : Theme.surfaceAlt
                            border.width: sourceUrl.activeFocus ? 2 : 1
                        }
                        onAccepted: if (text.trim() !== "") root.dubbing.downloadMediaFromLink(text.trim())
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.paddingSmall
                        PrimaryButton {
                            text: qsTr("Download")
                            iconName: "download"
                            enabled: sourceUrl.text.trim() !== "" && !root.dubbing.linkImporting && !root.dubbing.processing
                            onClicked: root.dubbing.downloadMediaFromLink(sourceUrl.text.trim())
                        }
                        PrimaryButton {
                            text: qsTr("Retry")
                            iconName: "refresh"
                            quiet: true
                            enabled: sourceUrl.text.trim() !== "" && !root.dubbing.linkImporting && !root.dubbing.processing
                            onClicked: root.dubbing.downloadMediaFromLink(sourceUrl.text.trim())
                        }
                        PrimaryButton {
                            text: qsTr("Cancel")
                            iconName: "close"
                            quiet: true
                            visible: root.dubbing.linkImporting
                            onClicked: root.dubbing.cancelMediaLinkImport()
                        }
                        Item { Layout.fillWidth: true }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: root.dubbing.linkImporting || root.dubbing.linkImportStatus !== ""
                        text: {
                            var status = root.dubbing.linkImportStatus || qsTr("Downloading media")
                            var received = root.dubbing.linkImportReceivedBytes
                            var total = root.dubbing.linkImportTotalBytes
                            return total > 0 ? status + " — " + root.formatBytes(received) + " / " + root.formatBytes(total)
                                             : status + (received > 0 ? " — " + root.formatBytes(received) : "")
                        }
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: root.dubbing.lastError !== "" && !root.dubbing.linkImporting
                        text: root.dubbing.lastError
                        color: Theme.error
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                visible: root.dubbing.downloadedMediaReady
                radius: Theme.radiusMedium
                color: Theme.surface
                border.color: Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.38)
                border.width: 1
                implicitHeight: completedLayout.implicitHeight + Theme.paddingLarge * 2

                ColumnLayout {
                    id: completedLayout
                    anchors.fill: parent
                    anchors.margins: Theme.paddingLarge
                    spacing: Theme.paddingSmall
                    Text { text: qsTr("Download complete"); color: Theme.success; font.pixelSize: Theme.fontLarge; font.bold: true }
                    Text { Layout.fillWidth: true; text: root.dubbing.downloadedMediaFileName; color: Theme.textPrimary; font.pixelSize: Theme.fontMedium; elide: Text.ElideMiddle }
                    Text { Layout.fillWidth: true; text: root.dubbing.downloadedMediaPath; color: Theme.textSecondary; font.pixelSize: Theme.fontSmall; wrapMode: Text.WrapAnywhere }
                    Text { Layout.fillWidth: true; text: qsTr("The staged file is not project media yet. Use it in Dubbing to normalize it, or send the same staged file to Subtitle OCR without downloading it again."); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall; wrapMode: Text.WordWrap }
                    RowLayout {
                        Layout.fillWidth: true
                        PrimaryButton {
                            text: qsTr("Use in Dubbing")
                            iconName: "dubbing"
                            enabled: !root.dubbing.linkImporting && !root.dubbing.processing
                            onClicked: {
                                if (root.dubbing.handoffDownloadedMediaToDubbing())
                                    root.openDubbingRequested()
                            }
                        }
                        PrimaryButton {
                            text: qsTr("Use in Subtitle OCR")
                            iconName: "scan"
                            quiet: true
                            enabled: !root.dubbing.linkImporting && !root.dubbing.processing
                            onClicked: {
                                if (root.subtitleOcr.useDownloadedMedia(root.dubbing.downloadedMediaPath))
                                    root.openSubtitleOcrRequested()
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }
                }
            }
        }
    }
}
