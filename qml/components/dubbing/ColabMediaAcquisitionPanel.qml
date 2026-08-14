import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "../base"

// Downloading a public file is a CPU-only acquisition task, not AI inference.
// Keep its controls deliberately independent from every Colab/GPU route.
Rectangle {
    id: root

    required property var dubbing
    property bool compact: false
    property bool showLibraryAction: true
    signal localFilesRequested()
    signal libraryRequested()

    readonly property bool busy: root.dubbing.mediaQueueDownloading || root.dubbing.mediaQueueProcessing

    implicitHeight: content.implicitHeight + Theme.paddingMedium * 2
    radius: Theme.radiusSmall
    color: Theme.surfaceAlt
    border.color: Qt.rgba(0.55, 0.35, 1.0, 0.42)
    border.width: 1

    FileDialog {
        id: douyinCookieFileDialog
        title: qsTr("Choose a Netscape Douyin cookies.txt file")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Cookie export (*.txt *.cookies)"), qsTr("All files (*)")]
        onAccepted: root.dubbing.setMediaDownloadCookieFile(selectedFile.toString())
    }

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: Theme.paddingMedium
        spacing: Theme.paddingSmall

        Text {
            Layout.fillWidth: true
            text: qsTr("Add media")
            color: Theme.textPrimary
            font.bold: true
            font.pixelSize: Theme.fontLarge
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Download public links on this computer, or add files you already downloaded. This page does not use Colab, GPU, API Gateway, STT, translation, voice, or isolation.")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: downloaderLayout.implicitHeight + Theme.paddingMedium * 2
            radius: Theme.radiusSmall
            color: Qt.rgba(0.45, 0.20, 1.0, 0.07)
            border.color: Qt.rgba(0.55, 0.35, 1.0, 0.30)
            border.width: 1

            ColumnLayout {
                id: downloaderLayout
                anchors.fill: parent
                anchors.margins: Theme.paddingMedium
                spacing: Theme.paddingSmall

                Text { text: qsTr("1. Download public links locally"); color: Theme.textPrimary; font.bold: true }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Uses LA Studio's managed yt-dlp adapter and local CPU. Paste a public HTTPS link or a full share message; LA Studio extracts only the URL. No Colab URL or token is needed.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
                TextArea {
                    id: publicLinks
                    Layout.fillWidth: true
                    Layout.minimumHeight: root.compact ? 86 : 116
                    enabled: !root.busy
                    placeholderText: qsTr("Paste one public HTTPS link or share message per line")
                    selectByMouse: true
                    color: Theme.textPrimary
                    placeholderTextColor: Theme.textSecondary
                    wrapMode: TextEdit.WrapAnywhere
                    leftPadding: Theme.paddingMedium; rightPadding: Theme.paddingMedium
                    topPadding: Theme.paddingSmall; bottomPadding: Theme.paddingSmall
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: Qt.rgba(1, 1, 1, 0.035)
                        border.color: publicLinks.activeFocus ? Theme.accent : Qt.rgba(1, 1, 1, 0.09)
                        border.width: publicLinks.activeFocus ? 2 : 1
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    PrimaryButton {
                        objectName: "localMediaDownloadAddLinksButton"
                        text: qsTr("Download link(s)")
                        iconName: "download"
                        enabled: publicLinks.text.trim().length > 0 && !root.busy
                        onClicked: {
                            if (root.dubbing.enqueueMediaLinks(publicLinks.text) > 0)
                                publicLinks.clear()
                        }
                    }
                    PrimaryButton {
                        visible: root.dubbing.mediaQueueDownloading || root.dubbing.mediaQueueProcessing
                        text: qsTr("Cancel")
                        iconName: "close"
                        quiet: true
                        onClicked: root.dubbing.cancelMediaQueue()
                    }
                    Item { Layout.fillWidth: true }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Douyin only: if the local downloader asks for fresh cookies, export a Netscape cookies.txt yourself and select it below. LA Studio copies it into a private temporary file for one retry, then removes the copy; it never reads Chrome or Edge cookies.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    PrimaryButton {
                        objectName: "douyinCookieFileButton"
                        text: root.dubbing.mediaDownloadCookieFileConfigured
                              ? qsTr("Douyin cookies selected") : qsTr("Choose optional Douyin cookies")
                        iconName: "folder"
                        quiet: true
                        enabled: !root.busy
                        onClicked: douyinCookieFileDialog.open()
                    }
                    PrimaryButton {
                        visible: root.dubbing.mediaDownloadCookieFileConfigured
                        text: qsTr("Clear cookies")
                        iconName: "close"
                        quiet: true
                        enabled: !root.busy
                        onClicked: root.dubbing.clearMediaDownloadCookieFile()
                    }
                    Item { Layout.fillWidth: true }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: localLayout.implicitHeight + Theme.paddingMedium * 2
            radius: Theme.radiusSmall
            color: Qt.rgba(0.16, 0.70, 0.45, 0.06)
            border.color: Qt.rgba(0.16, 0.70, 0.45, 0.30)
            border.width: 1
            ColumnLayout {
                id: localLayout
                anchors.fill: parent
                anchors.margins: Theme.paddingMedium
                spacing: Theme.paddingSmall
                Text { text: qsTr("2. Add files already downloaded"); color: Theme.textPrimary; font.bold: true }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("If you downloaded or created media in Colab yourself, click the Files folder in Colab's left sidebar and download the exact output path printed by that notebook's final cell. Then choose that file here. No worker URL or token from that notebook belongs on this page.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    PrimaryButton {
                        objectName: "manualMediaFilesButton"
                        text: qsTr("Choose downloaded file(s)")
                        iconName: "folder"
                        enabled: !root.busy
                        onClicked: root.localFilesRequested()
                    }
                    PrimaryButton {
                        visible: root.showLibraryAction
                        text: qsTr("Open media library")
                        iconName: "workflow"
                        quiet: true
                        enabled: !root.busy
                        onClicked: root.libraryRequested()
                    }
                    Item { Layout.fillWidth: true }
                }
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
