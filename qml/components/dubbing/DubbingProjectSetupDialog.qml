import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../base"
import "../shared"
import LAStudio

// Project-wide choices belong at the point the operator chooses Automatic or
// step-by-step.  They are intentionally not a permanent fourth workspace
// panel below the timeline.
Dialog {
    id: root

    required property var dubbing
    required property var languageCatalog
    property string selectedMode: "step"
    property bool continueWorkflow: false
    property string selectedSourceLanguage: ""
    property string selectedTargetLanguage: ""
    property string selectedQuality: "adaptive"

    signal configurationAccepted(string mode, bool continueWorkflow)
    signal configurationCancelled(bool continueWorkflow)

    parent: Overlay.overlay
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    width: Math.min(640, parent ? parent.width - Theme.paddingXL * 2 : 640)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0

    function languageIndex(value) {
        for (var i = 0; i < languageCatalog.length; ++i)
            if (languageCatalog[i].value === value) return i
        return 0
    }

    function openFor(mode, startAfterApply) {
        selectedMode = mode === "automatic" ? "automatic" : "step"
        continueWorkflow = startAfterApply === true
        selectedSourceLanguage = dubbing.sourceLanguage || "zh"
        selectedTargetLanguage = dubbing.targetLanguage || "vi"
        selectedQuality = dubbing.dubbingQuality || "adaptive"
        sourceLanguageBox.currentIndex = languageIndex(selectedSourceLanguage)
        targetLanguageBox.currentIndex = languageIndex(selectedTargetLanguage)
        open()
    }

    function applyConfiguration() {
        dubbing.sourceLanguage = selectedSourceLanguage
        dubbing.targetLanguage = selectedTargetLanguage
        dubbing.dubbingQuality = selectedQuality
        var mode = selectedMode
        var startAfterApply = continueWorkflow
        close()
        configurationAccepted(mode, startAfterApply)
    }

    // Offscreen route smoke must exercise the same visible primary action as
    // an operator: choosing a Dubbing mode first opens this project-level
    // setup dialog, then Continue enters the task-specific preflight.
    function qmlSmokeClickContinue() {
        if (!continueButton.visible || !continueButton.enabled)
            return false
        continueButton.click()
        return true
    }

    onRejected: configurationCancelled(continueWorkflow)

    background: Rectangle {
        color: Theme.surface
        radius: Theme.radiusMedium
        border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.58)
        border.width: 1
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.paddingLarge
            spacing: Theme.paddingMedium
            LineIcon { name: "sliders"; color: Theme.accentLight; Layout.preferredWidth: 24; Layout.preferredHeight: 24 }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    text: root.selectedMode === "automatic"
                          ? qsTr("Automatic Dubbing setup") : qsTr("Step-by-step Dubbing setup")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontLarge
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Set project languages and the execution policy once. Routes, models, and Direct Colab workers stay in the next task-specific setup.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.surfaceAlt }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.paddingLarge
            spacing: Theme.paddingMedium

            Text { text: qsTr("Project languages"); color: Theme.textPrimary; font.bold: true }
            Text {
                Layout.fillWidth: true
                text: qsTr("These values are shared by STT, subtitle alignment, translation, and TTS. They can be changed later from Project settings.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                AppComboBox {
                    id: sourceLanguageBox
                    Layout.fillWidth: true
                    model: root.languageCatalog
                    textRole: "text"
                    secondaryTextRole: "detail"
                    searchable: model.length > 6
                    onActivated: function(index) {
                        if (index >= 0 && index < model.length)
                            root.selectedSourceLanguage = model[index].value
                    }
                }
                LineIcon { name: "chevron-right"; color: Theme.textSecondary; Layout.preferredWidth: 18; Layout.preferredHeight: 18 }
                AppComboBox {
                    id: targetLanguageBox
                    Layout.fillWidth: true
                    model: root.languageCatalog
                    textRole: "text"
                    secondaryTextRole: "detail"
                    searchable: model.length > 6
                    onActivated: function(index) {
                        if (index >= 0 && index < model.length)
                            root.selectedTargetLanguage = model[index].value
                    }
                }
            }

            Text { text: qsTr("Execution quality"); color: Theme.textPrimary; font.bold: true; Layout.topMargin: Theme.paddingSmall }
            Text {
                Layout.fillWidth: true
                text: qsTr("This selects timing and translation-rewrite policy only. It does not choose a model or silently start local processing.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.paddingSmall
                Repeater {
                    model: [
                        { value: "fast", label: qsTr("Fast"), detail: qsTr("No adaptive rewrite") },
                        { value: "adaptive", label: qsTr("Adaptive"), detail: qsTr("Use configured rewrite when timing needs it") },
                        { value: "custom", label: qsTr("Custom"), detail: qsTr("Choose rewrite behavior later") }
                    ]
                    delegate: Button {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 58
                        onClicked: root.selectedQuality = modelData.value
                        background: Rectangle {
                            radius: Theme.radiusSmall
                            color: root.selectedQuality === modelData.value
                                   ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)
                                   : Qt.rgba(1, 1, 1, 0.025)
                            border.color: root.selectedQuality === modelData.value
                                          ? Theme.accent : Qt.rgba(1, 1, 1, 0.10)
                            border.width: 1
                        }
                        contentItem: Column {
                            anchors.centerIn: parent
                            width: parent.width - Theme.paddingMedium * 2
                            spacing: 2
                            Text { width: parent.width; text: modelData.label; color: Theme.textPrimary; font.bold: true; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
                            Text { width: parent.width; text: modelData.detail; color: Theme.textSecondary; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
                        }
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.surfaceAlt }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.paddingLarge
            PrimaryButton {
                text: qsTr("Back")
                quiet: true
                iconName: "arrow-left"
                onClicked: {
                    var startAfterApply = root.continueWorkflow
                    root.close()
                    root.configurationCancelled(startAfterApply)
                }
            }
            Item { Layout.fillWidth: true }
            PrimaryButton {
                id: continueButton
                text: root.selectedMode === "automatic" ? qsTr("Continue to preflight") : qsTr("Open first step")
                iconName: "chevron-right"
                onClicked: root.applyConfiguration()
            }
        }
    }
}
