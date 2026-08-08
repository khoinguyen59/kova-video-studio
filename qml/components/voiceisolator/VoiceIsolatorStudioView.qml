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
    // Pair a direct separation worker before any local model is selected.
    settingsRequiresReady: false
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

    readonly property bool remoteFirstMode: AppController.settings.remoteFirstMode
    readonly property bool colabSelected: AppController.colabVoiceIsolator.colabActive
    property var isolator: colabSelected ? AppController.colabVoiceIsolator : AppController.voiceIsolator
    readonly property bool fastModel: selectedFamilyId === "sherpa-onnx-spleeter-2stems-fp16"
    property string exportSource: ""
    property string exportStatus: ""
    property string playingStem: ""
    readonly property bool canIsolate: (!root.remoteFirstMode || root.colabSelected) && root.studioReady && root.isolator.ready && root.isolator.sourcePath.length > 0 && !root.isolator.processing

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

    function exportPath(url) {
        var path = root.localPath(url)
        if (path.length > 0 && !/\.wav$/i.test(path)) path += ".wav"
        return path
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
                            text: root.isolator.processing
                                  ? (root.colabSelected
                                     ? qsTr("Separating source on Colab GPU · worker reports phases, not a measurable percentage")
                                     : qsTr("Separating source · %1%").arg(root.isolator.progress))
                                  : root.isolator.lastError.length > 0 ? root.isolator.lastError
                                  : root.isolator.warning.length > 0 ? root.isolator.warning
                                  : (root.colabSelected ? qsTr("Direct Colab GPU separation is ready.") : (root.remoteFirstMode ? qsTr("Remote-first: pair a direct Colab separation worker.") : qsTr("Configure and load a sherpa-onnx runtime and separation model.")))
                            color: root.isolator.lastError.length > 0 ? Theme.danger : Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                        }
                        ProgressBar { visible: root.isolator.processing && !root.colabSelected; from: 0; to: 100; value: root.isolator.progress; Layout.preferredWidth: 180 }
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
                            root.exportStatus = ""
                            root.exportSource = path
                            exportDialog.open()
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: root.exportStatus !== ""
                        text: root.exportStatus
                        color: root.exportStatus.indexOf("Saved") === 0 ? Theme.success : Theme.danger
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }

            FileDialog {
                id: exportDialog
                title: qsTr("Export stem WAV")
                fileMode: FileDialog.SaveFile
                defaultSuffix: "wav"
                nameFilters: [qsTr("WAV audio (*.wav)")]
                // A bare relative currentFile is coerced to an empty URL by
                // Qt Quick Dialogs during component creation. Do not bind a
                // non-existent file here; the native Save dialog supplies the
                // filename and its selected folder at interaction time.
                onAccepted: {
                    const destination = root.exportPath(selectedFile)
                    if (destination === "") {
                        root.exportStatus = qsTr("No export destination was selected.")
                    } else if (root.isolator.exportStem(root.exportSource, destination)) {
                        root.exportStatus = qsTr("Saved WAV: %1").arg(destination)
                    } else {
                        root.exportStatus = qsTr("Could not save WAV: %1").arg(destination)
                    }
                }
            }
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
                    Text { Layout.fillWidth: true; text: root.remoteFirstMode ? qsTr("Remote-first requires direct Colab GPU. Local Dev is available only after disabling Remote-first mode.") : qsTr("Local model and direct Colab GPU are independent choices."); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall; wrapMode: Text.WordWrap }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Qt.rgba(1, 1, 1, 0.08) }
                    Text { text: qsTr("DIRECT COLAB GPU"); color: Theme.textSecondary; font.pixelSize: 10; font.bold: true; font.letterSpacing: 0.8 }
                    Text { Layout.fillWidth: true; text: qsTr("The worker receives the selected media directly and returns vocals/background WAV artifacts. It never uses API Gateway."); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall; wrapMode: Text.WordWrap }
                    ColabNotebookLink { notebookFile: AppController.colabVoiceIsolator.colabNotebookFile }
                    Text { text: qsTr("Worker URL"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                    ColabField { id: colabUrl; text: AppController.colabSeparationSession.workerUrl; placeholderText: qsTr("https://â€¦trycloudflare.com") }
                    Text { text: qsTr("Session token"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                    ColabField { id: colabToken; echoMode: TextInput.Password; placeholderText: AppController.colabVoiceIsolator.colabConnected ? qsTr("Connected â€” enter token to replace") : qsTr("Temporary token from Colab") }
                    ColabSessionStatus { session: AppController.colabSeparationSession }
                    Text { text: qsTr("Selected Colab model"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                    Text { Layout.fillWidth: true; text: AppController.colabVoiceIsolator.model; color: Theme.textPrimary; font.pixelSize: Theme.fontSmall; wrapMode: Text.WordWrap }
                    Text { text: qsTr("Exact notebook"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                    Text { Layout.fillWidth: true; text: AppController.colabVoiceIsolator.colabNotebookFile; color: Theme.textPrimary; font.pixelSize: Theme.fontSmall; wrapMode: Text.WrapAnywhere }
                    PrimaryButton {
                        Layout.fillWidth: true
                        enabled: !AppController.colabSeparationSession.checking
                                 && !(root.remoteFirstMode && AppController.colabVoiceIsolator.colabActive)
                        text: AppController.colabSeparationSession.checking
                              ? qsTr("Verifying CUDA and exact model...")
                              : (root.remoteFirstMode
                              ? (AppController.colabVoiceIsolator.colabActive ? qsTr("Direct Colab isolation active") : (AppController.colabVoiceIsolator.colabConnected ? qsTr("Use direct Colab isolation") : qsTr("Connect direct Colab isolation")))
                              : (AppController.colabVoiceIsolator.colabActive ? qsTr("Use local isolation") : (AppController.colabVoiceIsolator.colabConnected ? qsTr("Use direct Colab isolation") : qsTr("Connect direct Colab isolation"))))
                        iconName: root.remoteFirstMode || !AppController.colabVoiceIsolator.colabActive ? "cloud" : "close"
                        onClicked: {
                            if (AppController.colabVoiceIsolator.colabActive && !root.remoteFirstMode) {
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
