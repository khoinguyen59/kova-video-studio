import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../base"
import "../shared"
import "../base/colabNotebookUrls.js" as ColabNotebookUrls
import LAStudio

// The automatic workflow must be reviewed before it can start.  This dialog
// deliberately reads DubbingController.automaticPreflight rather than keeping
// a copy of URL/token/model state in QML, so every change is checked against
// the exact configuration the controller will execute.
Dialog {
    id: root

    required property var dubbing
    required property string outputPath
    property int currentPage: 0
    readonly property var preflight: root.dubbing ? root.dubbing.automaticPreflight : ({})
    readonly property bool ready: root.preflight.ready === true
    readonly property bool sourceReady: root.dubbing
                                      && root.dubbing.sourceMediaPath.length > 0
                                      && root.dubbing.sourceLanguage.length > 0
                                      && root.dubbing.targetLanguage.length > 0
    property string focusIssueId: ""
    // Child setup dialogs belong to this wizard, preserving its page and
    // current stage card while routes and models are edited.
    property string configuredStageId: ""

    signal backToEntryRequested()
    signal sourceBrowseRequested()
    signal sourceLinkImportRequested(string url)
    signal issueFixRequested(string issueId)
    signal adaptiveLlmSetupRequested()

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(860, parent ? parent.width - Theme.paddingXL * 2 : 860)
    height: Math.min(650, parent ? parent.height - Theme.paddingXL * 2 : 650)
    modal: true
    focus: true
    padding: 0
    title: ""
    // The Automatic setup is part of the mandatory entry flow.  It may only
    // return to that gate explicitly, never leak the workspace via Escape,
    // the close button, or an outside click.
    closePolicy: Popup.NoAutoClose

    function openPreflight() {
        currentPage = 0
        focusIssueId = ""
        open()
    }

    function openStageSetup(stage) {
        configuredStageId = stage.id || ""
        if (stage.setupAction === "node-model") {
            routeSetupDialog.stage = stage
            routeSetupDialog.open()
            return
        }
        if (stage.setupAction === "normalize") {
            normalizeSetupDialog.open()
            return
        }
        if (stage.setupAction === "alignment") {
            alignmentSetupDialog.open()
            return
        }
        if (stage.setupAction === "export") {
            exportSetupDialog.open()
            return
        }
        if (stage.setupAction === "source") currentPage = 0
    }

    function fixIssue(issueId) {
        focusIssueId = issueId
        if (issueId === "source-media" || issueId === "source-language" || issueId === "target-language")
            currentPage = 0
        else if (issueId === "adaptive-llm") {
            adaptiveLlmSetupRequested()
            currentPage = root.dubbing.adaptiveProvider === "colab-direct" ? 2 : 1
        } else if (issueId.indexOf("colab-") === 0)
            currentPage = 2
        else
            currentPage = 1
        issueFixRequested(issueId)
        issueFocusTimer.issueId = issueId
        issueFocusTimer.restart()
    }

    Timer {
        id: issueFocusTimer
        interval: 40
        repeat: false
        property string issueId: ""
        onTriggered: {
            if (issueId === "source-media") sourceBrowseButton.forceActiveFocus()
            else if (issueId === "source-language") {
                sourceLanguageBox.forceActiveFocus()
                if (sourceLanguageBox.contentItem) sourceLanguageBox.contentItem.forceActiveFocus()
            } else if (issueId === "target-language") {
                targetLanguageBox.forceActiveFocus()
                if (targetLanguageBox.contentItem) targetLanguageBox.contentItem.forceActiveFocus()
            }
        }
    }

    function requestAutomaticStart() {
        if (!root.dubbing.approveAutomaticPreflight()) {
            root.currentPage = 4
            return
        }
        if (root.dubbing.startAutomaticWorkflow(root.outputPath)) root.close()
    }

    function advanceFromCurrentPage() {
        // Do not infer an Auto value for a model that has not advertised it.
        // Keep missing required language visible and focused on this page.
        if (currentPage === 0 && !root.dubbing.sourceMediaPath) {
            sourceBrowseButton.forceActiveFocus()
            return
        }
        if (currentPage === 0 && !root.dubbing.sourceLanguage) {
            sourceLanguageBox.forceActiveFocus()
            return
        }
        if (currentPage === 0 && !root.dubbing.targetLanguage) {
            targetLanguageBox.forceActiveFocus()
            return
        }
        currentPage += 1
    }

    background: Rectangle {
        color: Theme.surface
        radius: Theme.radiusMedium
        border.color: Qt.rgba(1, 1, 1, 0.13)
        border.width: 1
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.paddingLarge
            spacing: Theme.paddingMedium

            LineIcon {
                name: "workflow"
                color: Theme.accentLight
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    text: qsTr("Automatic dubbing preflight")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontLarge
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Review the exact media, routes, models, fixed Colab configurations, and worker checks before any job starts.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.surfaceAlt }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.paddingLarge
            Layout.rightMargin: Theme.paddingLarge
            Layout.topMargin: Theme.paddingMedium
            spacing: Theme.paddingSmall

            Repeater {
                model: [qsTr("Source & language"), qsTr("Stages, routes & models"), qsTr("Colab workers"), qsTr("Review"), qsTr("Start")]
                delegate: Rectangle {
                    required property int index
                    required property string modelData
                    Layout.fillWidth: true
                    implicitHeight: 28
                    radius: 14
                    color: index === root.currentPage
                           ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.24)
                           : Qt.rgba(1, 1, 1, 0.04)
                    border.color: index === root.currentPage
                                  ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.55)
                                  : Qt.rgba(1, 1, 1, 0.09)
                    Text {
                        anchors.centerIn: parent
                        text: (index + 1) + ". " + modelData
                        color: index === root.currentPage ? Theme.textPrimary : Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        font.bold: index === root.currentPage
                    }
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.paddingLarge
            currentIndex: root.currentPage

            ScrollView {
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: Theme.paddingMedium
                    Text { text: qsTr("Source and language"); color: Theme.textPrimary; font.pixelSize: Theme.fontLarge; font.bold: true }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("These project languages are the single source of truth for STT, OCR/subtitle alignment, translation and TTS. They are saved with the project.")
                        color: Theme.textSecondary; font.pixelSize: Theme.fontSmall; wrapMode: Text.WordWrap
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: sourceMediaColumn.implicitHeight + Theme.paddingMedium * 2
                        radius: Theme.radiusSmall
                        color: Qt.rgba(1, 1, 1, 0.025)
                        border.color: root.preflight.sourceMediaPath ? Qt.rgba(1, 1, 1, 0.10) : Theme.danger
                        ColumnLayout {
                            id: sourceMediaColumn
                            anchors.fill: parent
                            anchors.margins: Theme.paddingMedium
                            Text { text: qsTr("Source media *"); color: root.preflight.sourceMediaPath ? Theme.textPrimary : Theme.danger; font.bold: true }
                            Text { Layout.fillWidth: true; text: qsTr("Choose a local audio/video file before stages are assessed. Public links must first finish in the local downloader, then be selected as local media."); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall; wrapMode: Text.WordWrap }
                            RowLayout {
                                Layout.fillWidth: true
                                TextField {
                                    objectName: "dubbingPreflightSourcePath"
                                    Layout.fillWidth: true
                                    readOnly: true
                                    text: root.preflight.sourceMediaPath || ""
                                    placeholderText: qsTr("No source media selected")
                                    color: Theme.textPrimary
                                    selectByMouse: true
                                }
                                PrimaryButton {
                                    id: sourceBrowseButton
                                    objectName: "dubbingPreflightSourceBrowseButton"
                                    text: qsTr("Browse local file")
                                    iconName: "folder"
                                    enabled: !root.dubbing.processing
                                    onClicked: root.sourceBrowseRequested()
                                }
                            }
                            Text { visible: !root.preflight.sourceMediaPath; text: qsTr("Choose source media to unlock the next step."); color: Theme.danger; font.pixelSize: Theme.fontSmall }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true; implicitHeight: sourceLanguageColumn.implicitHeight + Theme.paddingMedium * 2
                        radius: Theme.radiusSmall; color: Qt.rgba(1, 1, 1, 0.025); border.color: root.preflight.sourceLanguage ? Qt.rgba(1, 1, 1, 0.10) : Theme.danger
                        ColumnLayout {
                            id: sourceLanguageColumn; anchors.fill: parent; anchors.margins: Theme.paddingMedium
                            Text { text: qsTr("Spoken/source language *"); color: root.preflight.sourceLanguage ? Theme.textPrimary : Theme.danger; font.bold: true }
                            Text { text: qsTr("Used by Transcribe/STT, OCR/subtitle alignment and the source side of Translate."); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall; wrapMode: Text.WordWrap }
                            ComboBox {
                                id: sourceLanguageBox; Layout.fillWidth: true
                                objectName: "dubbingPreflightSourceLanguage"
                                model: [{ code: "en", name: "English (en)" }, { code: "vi", name: "Vietnamese (vi)" }, { code: "zh", name: "Chinese (zh)" }, { code: "ja", name: "Japanese (ja)" }, { code: "ko", name: "Korean (ko)" }]
                                textRole: "name"; valueRole: "code"
                                Component.onCompleted: currentIndex = indexOfValue(root.preflight.sourceLanguage)
                                onActivated: root.dubbing.sourceLanguage = currentValue
                            }
                            Text { visible: !root.preflight.sourceLanguage; text: qsTr("Choose the source language. Auto-detect is not silently selected."); color: Theme.danger; font.pixelSize: Theme.fontSmall }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true; implicitHeight: targetLanguageColumn.implicitHeight + Theme.paddingMedium * 2
                        radius: Theme.radiusSmall; color: Qt.rgba(1, 1, 1, 0.025); border.color: root.preflight.targetLanguage ? Qt.rgba(1, 1, 1, 0.10) : Theme.danger
                        ColumnLayout {
                            id: targetLanguageColumn; anchors.fill: parent; anchors.margins: Theme.paddingMedium
                            Text { text: qsTr("Output/target language *"); color: root.preflight.targetLanguage ? Theme.textPrimary : Theme.danger; font.bold: true }
                            Text { text: qsTr("Used by Translate and TTS. Saved voices remain validated against the selected TTS model family."); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall; wrapMode: Text.WordWrap }
                            ComboBox {
                                id: targetLanguageBox; Layout.fillWidth: true
                                objectName: "dubbingPreflightTargetLanguage"
                                model: [{ code: "vi", name: "Vietnamese (vi)" }, { code: "en", name: "English (en)" }, { code: "zh", name: "Chinese (zh)" }, { code: "ja", name: "Japanese (ja)" }, { code: "ko", name: "Korean (ko)" }]
                                textRole: "name"; valueRole: "code"
                                Component.onCompleted: currentIndex = indexOfValue(root.preflight.targetLanguage)
                                onActivated: root.dubbing.targetLanguage = currentValue
                            }
                            Text { visible: !root.preflight.targetLanguage; text: qsTr("Choose the output language before continuing."); color: Theme.danger; font.pixelSize: Theme.fontSmall }
                        }
                    }
                }
            }

            ScrollView {
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: Theme.paddingMedium
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Routes and models")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontLarge
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Only configured nodes are used. Configure any node before continuing; changing it will require a fresh review.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                    }
                    Repeater {
                        id: stageCards
                        model: root.preflight.stages || []
                        delegate: Rectangle {
                            required property var modelData
                            property alias setupButton: stageSetupButton
                            Layout.fillWidth: true
                            implicitHeight: nodeRow.implicitHeight + Theme.paddingMedium * 2
                            radius: Theme.radiusSmall
                            color: Qt.rgba(1, 1, 1, 0.025)
                            border.color: Qt.rgba(1, 1, 1, 0.09)
                            RowLayout {
                                id: nodeRow
                                anchors.fill: parent
                                anchors.margins: Theme.paddingMedium
                                spacing: Theme.paddingSmall
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text { text: modelData.title || modelData.id; color: Theme.textPrimary; font.bold: true }
                                    Text {
                                        Layout.fillWidth: true
                                        text: qsTr("Route: %1   •   Model: %2")
                                            .arg(modelData.route || qsTr("Not selected"))
                                            .arg(modelData.modelRequired === false
                                                 ? qsTr("No model required")
                                                 : (modelData.modelId || qsTr("Needs model selection")))
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontSmall
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.modelRequired === false
                                              ? qsTr("Variant: Not applicable")
                                              : qsTr("Variant: %1").arg(modelData.variant
                                                  || (modelData.executionProvider === "colab-direct"
                                                      ? qsTr("fixed") : qsTr("runtime-defined")))
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontSmall
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.languageSummary || qsTr("No language required")
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontSmall
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.configurationSummary || qsTr("Configuration needs review")
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontSmall
                                        elide: Text.ElideRight
                                    }
                                }
                                PrimaryButton {
                                    id: stageSetupButton
                                    objectName: "dubbingPreflightConfigure_" + modelData.id
                                    visible: modelData.setupAction && modelData.setupAction !== "none"
                                    text: qsTr("Configure")
                                    quiet: true
                                    enabled: !root.dubbing.processing
                                    onClicked: root.openStageSetup(modelData)
                                }
                                PrimaryButton {
                                    visible: modelData.adaptiveSetupRequired === true
                                    text: qsTr("Configure LLM")
                                    quiet: true
                                    enabled: !root.dubbing.processing
                                    onClicked: root.adaptiveLlmSetupRequested()
                                }
                                Text {
                                    visible: !modelData.setupAction || modelData.setupAction === "none"
                                    text: modelData.setupHint || qsTr("No configuration required")
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSmall
                                    wrapMode: Text.WordWrap
                                    Layout.preferredWidth: 150
                                }
                                Text {
                                    text: modelData.preflightStateLabel || qsTr("Ready")
                                    color: modelData.preflightState === "ready" ? Theme.success : Theme.warning
                                    font.pixelSize: Theme.fontSmall
                                    font.bold: true
                                }
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                spacing: Theme.paddingMedium
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Direct Colab workers")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontLarge
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Only active nodes routed to Direct Colab appear here. Each must be connected and checked against its exact capability, model, and fixed notebook configuration.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width
                        spacing: Theme.paddingSmall
                        Repeater {
                            model: root.preflight.selectedWorkers || []
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: workerRow.implicitHeight + Theme.paddingMedium * 2
                                radius: Theme.radiusSmall
                                color: modelData.verified ? Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.07)
                                                          : Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.08)
                                border.color: modelData.verified ? Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.38)
                                                                  : Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.45)
                                RowLayout {
                                    id: workerRow
                                    anchors.fill: parent
                                    anchors.margins: Theme.paddingMedium
                                    spacing: Theme.paddingSmall
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text { text: modelData.title || modelData.id; color: Theme.textPrimary; font.bold: true }
                                        Text {
                                            Layout.fillWidth: true
                                            text: qsTr("%1 / %2 / %3")
                                                .arg(modelData.capability || "")
                                                .arg(modelData.modelId || "")
                                                .arg(modelData.variant || qsTr("fixed notebook config"))
                                            color: Theme.textSecondary
                                            font.pixelSize: Theme.fontSmall
                                            elide: Text.ElideRight
                                        }
                                    }
                                    Text {
                                        text: modelData.verified ? qsTr("Verified") : qsTr("Needs check")
                                        color: modelData.verified ? Theme.success : Theme.warning
                                        font.pixelSize: Theme.fontSmall
                                        font.bold: true
                                    }
                                }
                            }
                        }
                        Text {
                            visible: (root.preflight.selectedWorkers || []).length === 0
                            text: qsTr("No active Dubbing node is using Direct Colab. API Gateway and Local CPU routes remain independent.")
                            color: Theme.textSecondary
                            wrapMode: Text.WordWrap
                        }
                    }
                }
                PrimaryButton {
                    objectName: "dubbingPreflightColabConfigureButton"
                    text: qsTr("Configure / Check Direct Colab workers")
                    iconName: "cloud"
                    Layout.fillWidth: true
                    visible: (root.preflight.selectedWorkers || []).length > 0
                    onClicked: preflightColabSetupDialog.open()
                }
            }

            ScrollView {
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: Theme.paddingMedium
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: reviewBanner.implicitHeight + Theme.paddingMedium * 2
                        radius: Theme.radiusSmall
                        color: root.ready ? Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.10)
                                           : Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.10)
                        border.color: root.ready ? Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.45)
                                                 : Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.45)
                        ColumnLayout {
                            id: reviewBanner
                            anchors.fill: parent
                            anchors.margins: Theme.paddingMedium
                            Text {
                                text: root.ready ? qsTr("Ready to start") : qsTr("Automatic workflow is blocked")
                                color: root.ready ? Theme.success : Theme.warning
                                font.pixelSize: Theme.fontLarge
                                font.bold: true
                            }
                            Text {
                                Layout.fillWidth: true
                                text: root.ready
                                      ? qsTr("All current requirements passed. Start remains bound to this reviewed configuration only.")
                                      : qsTr("Resolve the items below. The review updates when you change a route, model, media file, or worker connection.")
                                color: Theme.textSecondary
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                    Text {
                        visible: !root.ready
                        text: qsTr("Blocked items")
                        color: Theme.textPrimary
                        font.bold: true
                    }
                    Repeater {
                        id: reviewIssues
                        model: root.preflight.issues || []
                        delegate: Rectangle {
                            required property var modelData
                            property alias fixButton: issueFixButton
                            Layout.fillWidth: true
                            implicitHeight: issueText.implicitHeight + Theme.paddingSmall * 2
                            radius: Theme.radiusSmall
                            color: Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.08)
                            RowLayout {
                                id: issueText
                                anchors.fill: parent
                                anchors.margins: Theme.paddingSmall
                                Text {
                                    Layout.fillWidth: true
                                    text: "• " + (modelData.message || modelData.id)
                                    color: Theme.danger
                                    wrapMode: Text.WordWrap
                                }
                                PrimaryButton {
                                    id: issueFixButton
                                    objectName: "dubbingPreflightFix_" + modelData.id
                                    text: qsTr("Fix")
                                    quiet: true
                                    onClicked: root.fixIssue(modelData.id)
                                }
                            }
                        }
                    }
                    Text {
                        text: qsTr("Reviewed stage configuration")
                        color: Theme.textPrimary
                        font.bold: true
                    }
                    Repeater {
                        model: root.preflight.stages || []
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: reviewNode.implicitHeight + Theme.paddingSmall * 2
                            radius: Theme.radiusSmall
                            color: Qt.rgba(1, 1, 1, 0.025)
                            border.color: Qt.rgba(1, 1, 1, 0.09)
                            ColumnLayout {
                                id: reviewNode
                                anchors.fill: parent
                                anchors.margins: Theme.paddingSmall
                                Text { text: modelData.title; color: Theme.textPrimary; font.bold: true }
                                Text { Layout.fillWidth: true; text: modelData.route || qsTr("Not selected"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                                Text { Layout.fillWidth: true; text: modelData.configurationSummary || qsTr("Configuration needs review"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                                Text { Layout.fillWidth: true; text: modelData.languageSummary || qsTr("No language required"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                                Text { Layout.fillWidth: true; text: modelData.detail || (modelData.state === "blocked" ? qsTr("Blocked") : qsTr("Ready")); color: modelData.state === "blocked" ? Theme.warning : Theme.success; font.pixelSize: Theme.fontSmall }
                            }
                        }
                    }
                }
            }

            ScrollView {
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: Theme.paddingMedium
                    Text { text: qsTr("Start automatic workflow"); color: Theme.textPrimary; font.pixelSize: Theme.fontLarge; font.bold: true }
                    Text { Layout.fillWidth: true; text: root.ready ? qsTr("Review is complete. Starting closes this setup and begins the configured workflow.") : qsTr("Start remains unavailable until every active stage, required language and Direct Colab worker is ready."); color: root.ready ? Theme.success : Theme.warning; wrapMode: Text.WordWrap }
                    Repeater {
                        model: root.preflight.stages || []
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true; implicitHeight: summaryRow.implicitHeight + Theme.paddingSmall * 2
                            radius: Theme.radiusSmall; color: Qt.rgba(1,1,1,0.025); border.color: Qt.rgba(1,1,1,0.09)
                            ColumnLayout { id: summaryRow; anchors.fill: parent; anchors.margins: Theme.paddingSmall
                                Text { text: modelData.title; color: Theme.textPrimary; font.bold: true }
                                Text { Layout.fillWidth: true; text: qsTr("%1 · %2 · %3").arg(modelData.route || qsTr("Not selected")).arg(modelData.configurationSummary || qsTr("Configuration needs review")).arg(modelData.languageSummary || qsTr("No language required")); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall; wrapMode: Text.WordWrap }
                            }
                        }
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.surfaceAlt }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.paddingLarge
            spacing: Theme.paddingSmall
            PrimaryButton {
                objectName: "dubbingPreflightBackButton"
                text: qsTr("Back")
                quiet: true
                visible: root.currentPage > 0
                onClicked: root.currentPage -= 1
            }
            PrimaryButton {
                objectName: "dubbingPreflightBackToModeButton"
                visible: root.currentPage === 0
                text: qsTr("Back to mode selection")
                quiet: true
                onClicked: { root.close(); root.backToEntryRequested() }
            }
            Item { Layout.fillWidth: true }
            PrimaryButton {
                id: nextButton
                objectName: "dubbingPreflightNextButton"
                visible: root.currentPage < 4
                text: qsTr("Next")
                iconName: "chevron-right"
                enabled: root.currentPage !== 0 || root.sourceReady
                onClicked: root.advanceFromCurrentPage()
                AppToolTip { text: qsTr("Choose source media and both languages before continuing"); visible: parent.hovered && !parent.enabled }
            }
            PrimaryButton {
                objectName: "dubbingPreflightStartButton"
                visible: root.currentPage === 4
                text: qsTr("Start Automatic Dubbing")
                iconName: "play"
                enabled: root.ready && !root.dubbing.processing
                onClicked: root.requestAutomaticStart()
                AppToolTip {
                    text: root.ready ? qsTr("Start with the reviewed configuration")
                                     : qsTr("Resolve all blocked items before starting")
                    visible: parent.hovered
                }
            }
        }
    }

    WorkflowNodeModelDialog {
        id: preflightModelDialog
        nodes: root.dubbing.workflowNodes
        nodeConfigurations: root.dubbing.workflowNodeConfigurations
        configurationApplier: function(nodeId, familyId, runtimeId, runtimeVersion, selectedFiles) {
            var accepted = root.dubbing.setWorkflowNodeModel(
                        nodeId, familyId, runtimeId, runtimeVersion, selectedFiles)
            if (accepted)
                accepted = root.dubbing.setWorkflowNodeParameters(
                            nodeId, { executionProvider: "local-dev" })
            return { accepted: accepted,
                     error: accepted ? "" : root.dubbing.lastError }
        }
        colabConfigurationApplier: function(nodeId, familyId, openNotebook) {
            var accepted = root.dubbing.selectWorkflowColabModel(nodeId, familyId)
            if (accepted && openNotebook) {
                var notebook = root.dubbing.colabNotebookForNode(nodeId, familyId)
                if (notebook !== "")
                    Qt.openUrlExternally(ColabNotebookUrls.forNotebookFile(notebook))
            }
            return { accepted: accepted,
                     error: accepted ? "" : root.dubbing.lastError }
        }
    }

    Dialog {
        id: routeSetupDialog
        property var stage: ({})
        property string apiError: ""
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(560, parent ? parent.width - Theme.paddingXL * 2 : 560)
        title: qsTr("Configure %1").arg(stage.title || "")
        standardButtons: Dialog.NoButton
        onOpened: {
            apiError = ""
            var provider = stage.executionProvider || "local-dev"
            for (var index = 0; index < routeBox.model.length; ++index) {
                if (routeBox.model[index].id === provider) {
                    routeBox.currentIndex = index
                    break
                }
            }
            apiUrl.text = AppController.settings.gatewayUrl || ""
            apiKey.text = ""
            apiModel.text = stage.modelId || ""
        }
        contentItem: ColumnLayout {
            spacing: Theme.paddingMedium
            Text {
                Layout.fillWidth: true
                text: qsTr("Choose the execution route before selecting its model. API Gateway and Direct Colab stay independent.")
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
            }
            Text { text: qsTr("Route"); color: Theme.textPrimary; font.bold: true }
            ComboBox {
                id: routeBox
                Layout.fillWidth: true
                textRole: "label"
                valueRole: "id"
                model: routeSetupDialog.stage.actionNodeId === "source-separate"
                       ? [{ id: "local-dev", label: qsTr("Local CPU") },
                          { id: "colab-direct", label: qsTr("Direct Colab GPU") }]
                       : [{ id: "local-dev", label: qsTr("Local CPU") },
                          { id: "api-gateway", label: qsTr("API Gateway") },
                          { id: "colab-direct", label: qsTr("Direct Colab GPU") }]
            }
            Text {
                Layout.fillWidth: true
                visible: routeBox.currentValue === "local-dev"
                text: qsTr("Save opens the compatible local model/runtime picker. Nothing is changed if you cancel that picker.")
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: routeBox.currentValue === "colab-direct"
                spacing: Theme.paddingSmall
                Text { text: qsTr("Exact Colab model"); color: Theme.textPrimary; font.bold: true }
                ComboBox {
                    id: colabModelBox
                    Layout.fillWidth: true
                    textRole: "displayName"
                    valueRole: "modelId"
                    model: root.dubbing.colabModelOptionsForNode(routeSetupDialog.stage.actionNodeId || "")
                    Component.onCompleted: {
                        for (var index = 0; index < model.length; ++index)
                            if (model[index].modelId === routeSetupDialog.stage.modelId) currentIndex = index
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("The matching notebook, worker URL and session token are configured on the next Colab page. Changing model invalidates any prior verification.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: routeBox.currentValue === "api-gateway"
                spacing: Theme.paddingSmall
                Text { text: qsTr("API Gateway URL"); color: Theme.textPrimary; font.bold: true }
                TextField { id: apiUrl; Layout.fillWidth: true; placeholderText: "https://gateway.example/v1"; selectByMouse: true }
                Text { text: qsTr("API key"); color: Theme.textPrimary; font.bold: true }
                TextField {
                    id: apiKey
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    placeholderText: AppController.settings.gatewayApiKeyConfigured
                                     ? qsTr("Saved key available — enter to replace") : qsTr("Enter API key")
                    selectByMouse: true
                }
                Text { text: qsTr("Gateway model ID"); color: Theme.textPrimary; font.bold: true }
                TextField { id: apiModel; Layout.fillWidth: true; placeholderText: qsTr("Model exposed by the API Gateway"); selectByMouse: true }
            }
            Text { Layout.fillWidth: true; visible: routeSetupDialog.apiError !== ""; text: routeSetupDialog.apiError; color: Theme.danger; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                PrimaryButton { text: qsTr("Cancel"); quiet: true; onClicked: routeSetupDialog.close() }
                PrimaryButton {
                    text: qsTr("Save / Apply")
                    onClicked: {
                        var nodeId = routeSetupDialog.stage.actionNodeId || ""
                        if (routeBox.currentValue === "local-dev") {
                            routeSetupDialog.close()
                            preflightModelDialog.openFor(nodeId)
                            return
                        }
                        if (routeBox.currentValue === "colab-direct") {
                            if (colabModelBox.currentIndex < 0
                                    || !root.dubbing.selectWorkflowColabModel(nodeId, colabModelBox.currentValue)) {
                                routeSetupDialog.apiError = qsTr("Select a supported exact Colab model.")
                                return
                            }
                            routeSetupDialog.close()
                            return
                        }
                        if (apiUrl.text.trim() === "" || apiModel.text.trim() === "") {
                            routeSetupDialog.apiError = qsTr("Enter the API Gateway URL and model ID.")
                            return
                        }
                        AppController.settings.gatewayUrl = apiUrl.text.trim()
                        if (apiKey.text.trim() !== "") AppController.settings.setGatewayApiKey(apiKey.text.trim())
                        if (!AppController.settings.gatewayApiKeyConfigured) {
                            routeSetupDialog.apiError = qsTr("Enter an API key, or configure one in Settings first.")
                            return
                        }
                        if (!root.dubbing.setWorkflowNodeParameters(nodeId, {
                            executionProvider: "api-gateway", modelId: apiModel.text.trim()
                        })) return
                        routeSetupDialog.close()
                    }
                }
            }
        }
    }

    Dialog {
        id: normalizeSetupDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(520, parent ? parent.width - Theme.paddingXL * 2 : 520)
        title: qsTr("Normalize configuration")
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: ColumnLayout {
            spacing: Theme.paddingMedium
            Text { Layout.fillWidth: true; text: qsTr("Automatic local preprocessing is fixed by the production ingest service. It probes the selected source and produces master and analysis WAV inputs. No AI model or GPU worker is required."); color: Theme.textSecondary; wrapMode: Text.WordWrap }
            Text {
                Layout.fillWidth: true
                text: {
                    var stage = (root.preflight.stages || []).find(function(item) { return item.id === "normalize" })
                    return stage ? stage.configurationSummary : ""
                }
                color: Theme.textPrimary
                wrapMode: Text.WordWrap
            }
        }
    }

    Dialog {
        id: alignmentSetupDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(520, parent ? parent.width - Theme.paddingXL * 2 : 520)
        title: qsTr("Alignment configuration")
        standardButtons: Dialog.NoButton
        onOpened: {
            timingMode.currentIndex = timingMode.indexOfValue(root.dubbing.timingConfiguration.mode || "keep")
            timingGap.text = String(root.dubbing.timingConfiguration.minimumGapMs || 80)
        }
        contentItem: ColumnLayout {
            spacing: Theme.paddingMedium
            Text { Layout.fillWidth: true; text: qsTr("Configure timing resolution and subtitle output. Target-language text review remains part of Translate; this presentation stage owns timing and subtitle presentation without exposing internal timing nodes."); color: Theme.textSecondary; wrapMode: Text.WordWrap }
            ComboBox {
                id: timingMode
                Layout.fillWidth: true
                textRole: "label"
                valueRole: "id"
                model: [{ id: "keep", label: qsTr("Keep original timing") }, { id: "ripple", label: qsTr("Ripple forward") }, { id: "manual", label: qsTr("Manual conflict review") }]
            }
            TextField { id: timingGap; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly; placeholderText: qsTr("Minimum gap (ms)") }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                PrimaryButton { text: qsTr("Cancel"); quiet: true; onClicked: alignmentSetupDialog.close() }
                PrimaryButton { text: qsTr("Save / Apply"); onClicked: { if (root.dubbing.applyTimingResolution(timingMode.currentValue, Number(timingGap.text))) alignmentSetupDialog.close() } }
            }
        }
    }

    Dialog {
        id: exportSetupDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(520, parent ? parent.width - Theme.paddingXL * 2 : 520)
        title: qsTr("Export and output configuration")
        standardButtons: Dialog.NoButton
        onOpened: burnInBox.checked = root.dubbing.subtitleConfiguration.burnIn === true
        contentItem: ColumnLayout {
            spacing: Theme.paddingMedium
            Text { Layout.fillWidth: true; text: qsTr("Mix/render and output options are configured here because they belong to Export/Output. The internal mix node remains part of the production workflow."); color: Theme.textSecondary; wrapMode: Text.WordWrap }
            CheckBox { id: burnInBox; text: qsTr("Burn subtitles into exported video") }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                PrimaryButton { text: qsTr("Cancel"); quiet: true; onClicked: exportSetupDialog.close() }
                PrimaryButton { text: qsTr("Save / Apply"); onClicked: { if (root.dubbing.setSubtitleBurnIn(burnInBox.checked)) exportSetupDialog.close() } }
            }
        }
    }

    DubbingColabSetupDialog {
        id: preflightColabSetupDialog
        dubbing: root.dubbing
        stageIds: (root.preflight.selectedWorkers || []).map(function(worker) { return worker.id })
    }

    // Production-shell offscreen regression helpers. They activate the real
    // controls; file-selection result injection happens only at the picker
    // boundary owned by DubbingPage.
    function qmlSmokeClickSourceBrowse() { sourceBrowseButton.click() }
    function qmlSmokeSourceLanguageFocused() {
        return sourceLanguageBox.activeFocus
                || (sourceLanguageBox.contentItem && sourceLanguageBox.contentItem.activeFocus)
    }
    function qmlSmokeClickNext() {
        if (!nextButton.enabled) return false
        nextButton.click()
        return true
    }
    function qmlSmokeClickStageSetup(nodeId) {
        for (var index = 0; index < stageCards.count; ++index) {
            var card = stageCards.itemAt(index)
            if (card && card.modelData.id === nodeId && card.setupButton.visible) {
                card.setupButton.click()
                return true
            }
        }
        return false
    }
    function qmlSmokeStageSetupVisible() {
        return routeSetupDialog.visible || normalizeSetupDialog.visible
                || alignmentSetupDialog.visible || exportSetupDialog.visible
                || preflightModelDialog.visible
    }
    function qmlSmokeDismissStageSetup() {
        routeSetupDialog.close()
        normalizeSetupDialog.close()
        alignmentSetupDialog.close()
        exportSetupDialog.close()
        preflightModelDialog.close()
    }
    function qmlSmokeClickFix(issueId) {
        for (var index = 0; index < reviewIssues.count; ++index) {
            var card = reviewIssues.itemAt(index)
            if (card && card.modelData.id === issueId) {
                card.fixButton.click()
                return true
            }
        }
        return false
    }
    function qmlSmokeSelectLanguages() {
        sourceLanguageBox.currentIndex = sourceLanguageBox.indexOfValue("zh")
        sourceLanguageBox.activated(sourceLanguageBox.currentIndex)
        targetLanguageBox.currentIndex = targetLanguageBox.indexOfValue("vi")
        targetLanguageBox.activated(targetLanguageBox.currentIndex)
    }
}
