import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LAStudio
import ".."
import "../base"
import "../shared/settings"

Item {
    id: root
    property string preferredAnchorModelId: ""
    readonly property bool remoteFirstMode: AppController.settings.remoteFirstMode
    readonly property bool colabSelected: AppController.colabAlignment.colabActive
    readonly property string languageCode: languageSelector.language
    readonly property string timestampUnit: timestampUnitInput.currentIndex === 1 ? "character" : "word"
    readonly property string outputFormat: outputFormatInput.currentIndex === 1 ? "srt" : (outputFormatInput.currentIndex === 2 ? "webvtt" : "json")
    readonly property var anchorModels: {
        // Keep the derived list reactive when downloads install or remove models.
        var registryRevision = AppController.models.version
        return AppController.alignment.installedAnchorModels()
    }
    readonly property bool anchorModelAvailable: selectedAnchorModel() !== null
    signal closeRequested()

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

    function selectedAnchorModel() {
        var items = root.anchorModels
        if (root.preferredAnchorModelId !== "") {
            var needle = root.preferredAnchorModelId.toLowerCase()
            for (var i = 0; i < items.length; ++i) {
                var id = String(items[i].id || items[i].modelId || "").toLowerCase()
                if (id === needle || id.indexOf(needle) >= 0 || needle.indexOf(id) >= 0)
                    return items[i]
            }
        }
        return items.length > 0 ? items[0] : null
    }
    function vadOptions() {
        return { threshold: vadThreshold.realValue, minSpeechMs: 250, minSilenceMs: vadSilence.value,
                 speechPadMs: vadPadding.value, maxChunkSeconds: vadMaxChunk.value }
    }

    ScrollView {
        id: settingsScroll
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            height: Math.max(settingsScroll.availableHeight, implicitHeight)
            spacing: Theme.paddingLarge

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.paddingSmall

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text { Layout.fillWidth: true; text: qsTr("Alignment Settings"); color: Theme.textPrimary; font.pixelSize: Theme.fontLarge; font.bold: true }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Options for the current alignment job")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                    }
                }

                Button {
                    flat: true
                    implicitWidth: 32
                    implicitHeight: 32
                    onClicked: root.closeRequested()
                    contentItem: LineIcon { name: "close"; color: parent.hovered ? Theme.textPrimary : Theme.textSecondary; width: 17; height: 17 }
                    background: Rectangle { radius: Theme.radiusSmall; color: parent.hovered ? Qt.rgba(1, 1, 1, 0.06) : "transparent" }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Qt.rgba(1, 1, 1, 0.08) }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.paddingSmall

                Text { text: qsTr("PROCESSING"); color: Theme.textSecondary; font.pixelSize: 10; font.bold: true; font.letterSpacing: 0.8 }

                Text { Layout.fillWidth: true; text: root.colabSelected ? qsTr("Direct Colab GPU alignment is selected. It does not use API Gateway or local model downloads.") : (root.remoteFirstMode ? qsTr("Remote-first: pair a direct Colab alignment worker.") : qsTr("Local alignment uses installed models and runtimes.")); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall; wrapMode: Text.WordWrap }

                LanguageSelector {
                    id: languageSelector
                    Layout.fillWidth: true
                    labelText: qsTr("Language")
                    language: "vie"
                    family: ({ "supportedLanguageSetId": "alignment-mms-nemotron-v1" })
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.paddingSmall
                    AlignmentOptionField {
                        label: qsTr("Timestamps")
                        Layout.fillWidth: true
                        AppComboBox { id: timestampUnitInput; Layout.fillWidth: true; model: [qsTr("Word"), qsTr("Character")] }
                    }
                    AlignmentOptionField {
                        label: qsTr("Export format")
                        Layout.fillWidth: true
                        AppComboBox { id: outputFormatInput; Layout.fillWidth: true; model: ["JSON", "SRT", "WebVTT"] }
                    }
                }

                CheckBox { id: normalizeTranscriptInput; text: qsTr("Normalize transcript"); checked: true; palette.windowText: Theme.textPrimary }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: colabContent.implicitHeight + Theme.paddingMedium * 2
                radius: Theme.radiusSmall
                color: Qt.rgba(1, 1, 1, 0.025)
                border.color: Qt.rgba(1, 1, 1, 0.08)

                ColumnLayout {
                    id: colabContent
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: Theme.paddingMedium
                    spacing: Theme.paddingSmall

                    Text { text: qsTr("DIRECT COLAB GPU"); color: Theme.textSecondary; font.pixelSize: 10; font.bold: true; font.letterSpacing: 0.8 }
                    Text { Layout.fillWidth: true; text: qsTr("Qwen3 ForcedAligner receives only the selected audio and transcript directly from this app. This is independent of API Gateway."); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall; wrapMode: Text.WordWrap }
                    Text { text: qsTr("Worker URL"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                    ColabField {
                        id: colabUrl
                        text: AppController.colabAlignmentSession.workerUrl
                        placeholderText: qsTr("https://â€¦trycloudflare.com")
                    }
                    Text { text: qsTr("Session token"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                    ColabField {
                        id: colabToken
                        echoMode: TextInput.Password
                        placeholderText: AppController.colabAlignment.colabConnected ? qsTr("Connected â€” enter token to replace") : qsTr("Temporary token from Colab")
                    }
                    Text { text: qsTr("Model"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                    Text { Layout.fillWidth: true; text: qsTr("Qwen3 ForcedAligner 0.6B (word / character timing)"); color: Theme.textPrimary; font.pixelSize: Theme.fontSmall; wrapMode: Text.WordWrap }
                    PrimaryButton {
                        Layout.fillWidth: true
                        enabled: !(root.remoteFirstMode && AppController.colabAlignment.colabActive)
                        text: root.remoteFirstMode
                              ? (AppController.colabAlignment.colabActive ? qsTr("Direct Colab alignment active") : (AppController.colabAlignment.colabConnected ? qsTr("Use direct Colab alignment") : qsTr("Connect direct Colab alignment")))
                              : (AppController.colabAlignment.colabActive ? qsTr("Use local alignment") : (AppController.colabAlignment.colabConnected ? qsTr("Use direct Colab alignment") : qsTr("Connect direct Colab alignment")))
                        iconName: root.remoteFirstMode || !AppController.colabAlignment.colabActive ? "cloud" : "close"
                        onClicked: {
                            if (AppController.colabAlignment.colabActive && !root.remoteFirstMode) {
                                AppController.colabAlignment.useLocal()
                            } else if (AppController.colabAlignment.colabConnected) {
                                AppController.colabAlignment.useColab()
                            } else if (AppController.colabAlignment.connectColab(colabUrl.text.trim(), colabToken.text)) {
                                colabToken.text = ""
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: advancedContent.implicitHeight + Theme.paddingMedium * 2
                radius: Theme.radiusSmall
                color: Qt.rgba(1, 1, 1, 0.025)
                border.color: Qt.rgba(1, 1, 1, 0.08)

                ColumnLayout {
                    id: advancedContent
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: Theme.paddingMedium
                    spacing: Theme.paddingSmall
                    CheckBox { id: advancedInput; Layout.fillWidth: true; text: qsTr("Advanced voice activity detection"); palette.windowText: Theme.textPrimary }
                    GridLayout {
                        visible: advancedInput.checked
                        Layout.fillWidth: true
                        columns: 2
                        rowSpacing: Theme.paddingSmall
                        columnSpacing: Theme.paddingSmall
                        Label { text: qsTr("Threshold"); color: Theme.textSecondary }
                        SpinBox { id: vadThreshold; Layout.fillWidth: true; from: 10; to: 90; value: 50; stepSize: 5; property real realValue: value / 100.0 }
                        Label { text: qsTr("Silence (ms)"); color: Theme.textSecondary }
                        SpinBox { id: vadSilence; Layout.fillWidth: true; from: 100; to: 2000; value: 300; stepSize: 50 }
                        Label { text: qsTr("Padding (ms)"); color: Theme.textSecondary }
                        SpinBox { id: vadPadding; Layout.fillWidth: true; from: 0; to: 1000; value: 250; stepSize: 50 }
                        Label { text: qsTr("Max chunk (s)"); color: Theme.textSecondary }
                        SpinBox { id: vadMaxChunk; Layout.fillWidth: true; from: 5; to: 120; value: 30; stepSize: 5 }
                    }
                }
            }

            Item { Layout.fillHeight: true; Layout.minimumHeight: Theme.paddingSmall }
        }
    }
}
