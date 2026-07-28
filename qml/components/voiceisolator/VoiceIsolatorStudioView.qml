import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import LAStudio
import "../shared"
import "../base"
import ".."

StudioShell {
    id: root

    family: null
    families: []
    capability: "voice-isolation"
    selectedFamilyId: family ? family.id : ""
    studioContext: null
    studioReady: false
    studioIconName: "voice-isolator"
    showSettingsPanel: true
    showLeftPanel: true
    isLeftPanelOpen: true
    modalSelectionMode: true
    showSwitcher: false
    modalSelectionTitle: qsTr("Model + Runtime")
    modalSelectionValue: family ? family.title : qsTr("Select source-separation model")
    modalSelectionDetail: ""
    backToolTip: qsTr("Change model and runtime")

    readonly property bool colabSelected: AppController.colabVoiceIsolator.colabActive
    property var isolator: colabSelected ? AppController.colabVoiceIsolator : AppController.voiceIsolator
    readonly property bool fastModel: selectedFamilyId === "sherpa-onnx-spleeter-2stems-fp16"
    property string exportSource: ""
    property string playingStem: ""
    readonly property bool canIsolate: root.studioReady && root.isolator.ready && root.isolator.sourcePath.length > 0 && !root.isolator.processing

    signal backToGallery()
    signal reloadRequested()
    signal ejectRequested()
    signal modelSwitchRequested(string familyId)
    signal runtimeSwitchRequested(string runtimeId)

    onRequestBack: root.backToGallery()
    onRequestConfigurationPicker: root.backToGallery()
    onRequestReload: root.reloadRequested()
    onRequestEject: root.ejectRequested()
    onRequestModelSwitch: function(familyId) { root.modelSwitchRequested(familyId) }
    onRequestRuntimeSwitch: function(runtimeId) { root.runtimeSwitchRequested(runtimeId) }

    function localPath(url) {
        var value = url.toString()
        if (value.startsWith("file:///")) value = value.substring(8)
        else if (value.startsWith("file://")) value = value.substring(7)
        return value.replace(/^\/([a-zA-Z]:)/, "$1")
    }

    function playStem(kind, path) {
        root.playingStem = kind
        AppController.player.playFile(path)
    }

    Connections {
        target: AppController.player
        function onPlayingChanged() { if (!AppController.player.playing) root.playingStem = "" }
    }

    leftPanelContent: [
        VoiceIsolatorHistoryPanel {
            anchors.fill: parent
            isolator: root.isolator
            onCloseRequested: root.isLeftPanelOpen = false
        }
    ]

    mainContent: [
        Item {
            anchors.fill: parent

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.paddingXL
                spacing: Theme.paddingMedium

                MediaInputSourcePicker {
                    id: mediaInput
                    Layout.fillWidth: true
                    Layout.preferredHeight: selectedPath.length > 0 ? 210 : 136
                    mediaLabel: root.isolator.sourcePath.length > 0 ? qsTr("Source media loaded") : qsTr("Audio or video file")
                    mediaHint: qsTr("WAV, MP3, FLAC, MP4, MKV, MOV, WEBM supported")
                    fileDialogTitle: qsTr("Choose audio or video")
                    fileNameFilters: [qsTr("Media files (*.wav *.mp3 *.m4a *.flac *.mp4 *.mkv *.mov *.webm *.avi)"), qsTr("All files (*)")]
                    showMicrophone: false
                    showSystemSource: false
                    busy: root.isolator.processing
                    onMediaSelected: function(path) {
                        mediaInput.selectedPath = path
                        root.isolator.sourcePath = path
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.paddingSmall
                    Item { Layout.fillWidth: true }
                    PrimaryButton { text: qsTr("Clear"); iconName: "trash"; quiet: true; textColor: Theme.textSecondary; enabled: !root.isolator.processing; onClicked: root.isolator.clearResult() }
                    PrimaryButton {
                        text: root.isolator.processing ? qsTr("Cancel") : qsTr("Isolate Voice")
                        iconName: root.isolator.processing ? "stop" : "voice-isolator"
                        buttonColor: root.isolator.processing ? Theme.danger : Theme.accent
                        Layout.preferredWidth: 170
                        enabled: root.isolator.processing || root.canIsolate
                        onClicked: root.isolator.processing ? root.isolator.cancel() : root.isolator.isolate(root.fastModel)
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: statusRow.implicitHeight + Theme.paddingMedium * 2
                    visible: root.isolator.processing || root.isolator.lastError.length > 0 || root.isolator.warning.length > 0 || !root.isolator.ready
                    radius: Theme.radiusSmall
                    color: Qt.rgba(1, 1, 1, 0.025)
                    border.color: root.isolator.lastError.length > 0 ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.45) : Qt.rgba(1, 1, 1, 0.07)
                    RowLayout {
                        id: statusRow
                        anchors.fill: parent
                        anchors.margins: Theme.paddingMedium
                        BusyIndicator { visible: root.isolator.processing; running: visible; Layout.preferredWidth: 20; Layout.preferredHeight: 20; palette.dark: Theme.accent }
                        LineIcon { visible: !root.isolator.processing; name: "activity"; color: root.isolator.lastError.length > 0 ? Theme.danger : Theme.warning; Layout.preferredWidth: 18; Layout.preferredHeight: 18 }
                        Text {
                            Layout.fillWidth: true
                            text: root.isolator.processing ? qsTr("Separating source · %1%").arg(root.isolator.progress)
                                  : root.isolator.lastError.length > 0 ? root.isolator.lastError
                                  : root.isolator.warning.length > 0 ? root.isolator.warning
                                  : (root.colabSelected ? qsTr("Direct Colab GPU separation is ready.") : qsTr("Configure and load a sherpa-onnx runtime and separation model."))
                            color: root.isolator.lastError.length > 0 ? Theme.danger : Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                        }
                        ProgressBar { visible: root.isolator.processing; from: 0; to: 100; value: root.isolator.progress; Layout.preferredWidth: 180 }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: Theme.paddingMedium

                    VoiceSeparationOutput {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        vocalsPath: root.isolator.vocalsPath
                        backgroundPath: root.isolator.backgroundPath
                        vocalsSamples: root.isolator.vocalsSamples
                        backgroundSamples: root.isolator.backgroundSamples
                        playingStem: root.playingStem
                        onPlayRequested: function(kind, path) {
                            root.playingStem === kind && AppController.player.playing
                                ? AppController.player.stop() : root.playStem(kind, path)
                        }
                        onSeekRequested: function(kind, progress) {
                            AppController.player.seek(Math.round(progress * AppController.player.playbackDurationMs))
                        }
                        onExportRequested: function(kind, path) {
                            root.exportSource = path
                            exportDialog.open()
                        }
                    }
                }
            }

            FileDialog { id: exportDialog; title: qsTr("Export stem WAV"); fileMode: FileDialog.SaveFile; currentFile: "stem.wav"; onAccepted: root.isolator.exportStem(root.exportSource, root.localPath(file)) }
        }
    ]

    settingsContent: [
        Item {
            anchors.fill: parent

            component ColabField: TextField {
                Layout.fillWidth: true
                color: Theme.textPrimary
                placeholderTextColor: Theme.textSecondary
                selectByMouse: true
                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: Qt.rgba(1, 1, 1, 0.04)
                    border.color: parent.activeFocus ? Theme.accent : Qt.rgba(1, 1, 1, 0.12)
                }
            }

            ScrollView {
                anchors.fill: parent
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width - Theme.paddingLarge * 2
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: Theme.paddingMedium

                    Text { Layout.fillWidth: true; text: qsTr("Voice Isolation"); color: Theme.textPrimary; font.pixelSize: Theme.fontLarge; font.bold: true }
                    Text { Layout.fillWidth: true; text: qsTr("Local model and direct Colab GPU are independent choices."); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall; wrapMode: Text.WordWrap }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Qt.rgba(1, 1, 1, 0.08) }
                    Text { text: qsTr("DIRECT COLAB GPU"); color: Theme.textSecondary; font.pixelSize: 10; font.bold: true; font.letterSpacing: 0.8 }
                    Text { Layout.fillWidth: true; text: qsTr("The worker receives the selected media directly and returns vocals/background WAV artifacts. It never uses API Gateway."); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall; wrapMode: Text.WordWrap }
                    Text { text: qsTr("Worker URL"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                    ColabField { id: colabUrl; text: AppController.colabSession.workerUrl; placeholderText: qsTr("https://â€¦trycloudflare.com") }
                    Text { text: qsTr("Session token"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                    ColabField { id: colabToken; echoMode: TextInput.Password; placeholderText: AppController.colabVoiceIsolator.colabConnected ? qsTr("Connected â€” enter token to replace") : qsTr("Temporary token from Colab") }
                    Text { text: qsTr("Model"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                    Text { Layout.fillWidth: true; text: qsTr("Demucs htdemucs — vocals + background"); color: Theme.textPrimary; font.pixelSize: Theme.fontSmall; wrapMode: Text.WordWrap }
                    PrimaryButton {
                        Layout.fillWidth: true
                        text: AppController.colabVoiceIsolator.colabActive ? qsTr("Use local isolation") : (AppController.colabVoiceIsolator.colabConnected ? qsTr("Use direct Colab isolation") : qsTr("Connect direct Colab isolation"))
                        iconName: AppController.colabVoiceIsolator.colabActive ? "close" : "cloud"
                        onClicked: {
                            if (AppController.colabVoiceIsolator.colabActive) {
                                AppController.colabVoiceIsolator.useLocal()
                            } else if (AppController.colabVoiceIsolator.colabConnected) {
                                AppController.colabVoiceIsolator.useColab()
                            } else if (AppController.colabVoiceIsolator.connectColab(colabUrl.text.trim(), colabToken.text)) {
                                colabToken.text = ""
                            }
                        }
                    }
                }
            }
        }
    ]
}
