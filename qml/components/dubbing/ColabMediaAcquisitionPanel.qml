import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../base"

// Explicit acquisition boundary for public media.  It intentionally contains
// no browser/cookie/local-yt-dlp controls: link downloads happen in the
// temporary, separately verified Colab worker, while local files bypass that
// worker entirely.
Rectangle {
    id: root

    required property var dubbing
    property bool compact: false
    property bool showLibraryAction: true
    signal localFilesRequested()
    signal libraryRequested()

    readonly property var colab: root.dubbing.mediaDownloadColabSetup || ({})
    readonly property bool colabVerified: colab.verified === true
    readonly property bool colabChecking: colab.checking === true
    readonly property bool busy: root.dubbing.mediaQueueDownloading || root.dubbing.mediaQueueProcessing
    readonly property string notebookUrl: "https://colab.research.google.com/github/khoinguyen59/kova-video-studio/blob/main/notebooks/LA_STUDIO_MEDIA_DOWNLOAD_YTDLP_COLAB.ipynb"

    implicitHeight: content.implicitHeight + Theme.paddingMedium * 2
    radius: Theme.radiusSmall
    color: Theme.surfaceAlt
    border.color: Qt.rgba(0.55, 0.35, 1.0, 0.42)
    border.width: 1

    function connectWorker() {
        root.dubbing.connectWorkflowColabStage("media-download", "yt-dlp-media-download",
                                                workerUrl.text.trim(), sessionToken.text.trim())
    }

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: Theme.paddingMedium
        spacing: Theme.paddingSmall

        Text {
            Layout.fillWidth: true
            text: qsTr("Choose how to add media")
            color: Theme.textPrimary
            font.bold: true
            font.pixelSize: Theme.fontLarge
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Option 1 downloads public links in a dedicated Colab worker and copies the completed file into LA Studio. Option 2 adds files you already downloaded. Neither option starts STT, translation, voice, or isolation.")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: colabLayout.implicitHeight + Theme.paddingMedium * 2
            radius: Theme.radiusSmall
            color: Qt.rgba(0.45, 0.20, 1.0, 0.07)
            border.color: Qt.rgba(0.55, 0.35, 1.0, 0.30)
            border.width: 1
            ColumnLayout {
                id: colabLayout
                anchors.fill: parent
                anchors.margins: Theme.paddingMedium
                spacing: Theme.paddingSmall
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("1. Download public links with Colab")
                        color: Theme.textPrimary
                        font.bold: true
                    }
                    Text {
                        text: root.colabVerified ? qsTr("Verified")
                              : (root.colabChecking ? qsTr("Checking…") : qsTr("Not connected"))
                        color: root.colabVerified ? Theme.success
                              : (root.colabChecking ? Theme.warning : Theme.textSecondary)
                        font.pixelSize: Theme.fontSmall
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Run the dedicated media-download notebook once in Colab, then paste only its temporary Worker URL and session token below. This CPU worker is independent of all GPU model workers and API Gateway.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    PrimaryButton {
                        text: qsTr("Open downloader notebook in Colab")
                        iconName: "external-link"
                        quiet: true
                        onClicked: Qt.openUrlExternally(root.notebookUrl)
                    }
                    PrimaryButton {
                        visible: root.colabVerified || root.colabChecking
                        text: root.colabChecking ? qsTr("Checking…") : qsTr("Check connection")
                        iconName: "play"
                        quiet: true
                        enabled: !root.colabChecking && !root.busy
                        onClicked: root.dubbing.checkWorkflowColabStage("media-download")
                    }
                    PrimaryButton {
                        visible: root.colabVerified || root.colabChecking
                        text: qsTr("Disconnect")
                        iconName: "close"
                        quiet: true
                        enabled: !root.busy
                        onClicked: root.dubbing.disconnectWorkflowColabStage("media-download")
                    }
                    Item { Layout.fillWidth: true }
                }
                TextField {
                    id: workerUrl
                    Layout.fillWidth: true
                    enabled: !root.busy
                    placeholderText: qsTr("Temporary Colab Worker URL (https://…trycloudflare.com)")
                    selectByMouse: true
                    color: Theme.textPrimary
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: Qt.rgba(1, 1, 1, 0.035)
                        border.color: workerUrl.activeFocus ? Theme.accent : Qt.rgba(1, 1, 1, 0.09)
                        border.width: workerUrl.activeFocus ? 2 : 1
                    }
                }
                TextField {
                    id: sessionToken
                    Layout.fillWidth: true
                    enabled: !root.busy
                    echoMode: TextInput.Password
                    placeholderText: root.colabVerified ? qsTr("Connected — enter a replacement token only if the notebook was restarted")
                                                      : qsTr("Temporary session token from the Colab notebook")
                    selectByMouse: true
                    color: Theme.textPrimary
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: Qt.rgba(1, 1, 1, 0.035)
                        border.color: sessionToken.activeFocus ? Theme.accent : Qt.rgba(1, 1, 1, 0.09)
                        border.width: sessionToken.activeFocus ? 2 : 1
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    PrimaryButton {
                        objectName: "colabMediaDownloadConnectButton"
                        text: root.colabChecking ? qsTr("Checking…") : qsTr("Connect and check Colab downloader")
                        iconName: "link"
                        enabled: workerUrl.text.trim().length > 0 && sessionToken.text.trim().length > 0
                                 && !root.colabChecking && !root.busy
                        onClicked: root.connectWorker()
                    }
                    Item { Layout.fillWidth: true }
                }
                Text {
                    Layout.fillWidth: true
                    visible: root.colab.diagnostic !== undefined && root.colab.diagnostic !== ""
                    text: root.colab.diagnostic || ""
                    color: root.colabVerified ? Theme.success : Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
                TextArea {
                    id: publicLinks
                    Layout.fillWidth: true
                    Layout.minimumHeight: root.compact ? 86 : 116
                    enabled: root.colabVerified && !root.busy
                    placeholderText: qsTr("Paste one public HTTPS link or share message per line. LA Studio extracts the URL, sends it only to this Colab worker, then downloads the completed file into its local media library.")
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
                        objectName: "colabMediaDownloadAddLinksButton"
                        text: qsTr("Download link(s) with Colab")
                        iconName: "download"
                        enabled: root.colabVerified && publicLinks.text.trim().length > 0 && !root.busy
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
                    text: qsTr("Choose multiple local video or audio files. They are available immediately in the same media library and no Colab downloader, local yt-dlp, Chromium, or cookies are used.")
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
