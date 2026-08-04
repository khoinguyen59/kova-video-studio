import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "../components/base"
import "../components/alignment"
import "../components/dubbing"
import "../components/shared"
import LAStudio

Item {
    id: root
    anchors.fill: parent

    // The loader is recreated on every route visit.  Gate the workspace before
    // any model reset, workflow setup or stage action can happen.
    Component.onCompleted: {
        dubbing.beginDubbingEntry()
        dubbingEntryGate.openGate()
    }

    property var dubbing: AppController.dubbing
    property int selectedSegment: -1
    property bool isVideoSource: dubbing.sourceMediaPath.length > 0 && /\.(mp4|mkv|mov|webm|avi)$/i.test(dubbing.sourceMediaPath)
    property string reviewStepId: "import"
    readonly property string displayedStepId: (dubbing.processing || dubbing.lastError !== "")
                                              ? dubbing.currentStepId : reviewStepId
    property string observedCompletedStep: ""
    property string playingSeparationStem: ""
    property string playingVoiceClipPath: ""
    property bool isHistoryOpen: true
    property bool isNodeInspectorOpen: true
    // The QML smoke route exercises the transcript selector, then two dialogs
    // whose geometry is only valid on the following event-loop turn.  Keep the
    // phases explicit so the test observes the real rendered state instead of
    // treating a deferred layout as a failed configuration contract.
    property int qmlSmokeTranscriptSourcePhase: 0
    property string qmlSmokeTranscriptSourceFailure: ""
    property bool qmlSmokeMediaPickerRequested: false
    property string qmlSmokeMediaPath: ""
    property int qmlSmokeAutomaticPhase: 0
    property int qmlSmokeAutomaticStageIndex: 0
    property string pendingHistoryDeleteId: ""
    readonly property var languageCatalog: AppController.catalog.languageSet("default")

    StudioPageController {
        id: translationRecommendationController
        capabilityId: "translation"
        autoLoadOnSync: false
    }

    StudioPageController {
        id: adaptiveLlmController
        capabilityId: "llm-chat"
        autoLoadOnSync: false
    }

    Connections {
        target: dubbing
        function onWorkflowChanged() {
            if (dubbing.processing) {
                root.reviewStepId = dubbing.currentStepId
            } else if (dubbing.lastCompletedStepId !== "" && dubbing.lastCompletedStepId !== root.observedCompletedStep) {
                root.observedCompletedStep = dubbing.lastCompletedStepId
                root.reviewStepId = dubbing.lastCompletedStepId
            }
        }
        function onWorkflowSetupRequired(nodeId, setupKind, message) {
            root.reviewStepId = nodeId === "adaptive-llm" ? "translate" : nodeId
            root.isNodeInspectorOpen = true
            if (setupKind === "rewrite-model")
                qualityDialog.openForMode("custom")
            else
                nodeModelDialog.openFor(nodeId)
        }
    }

    Connections {
        target: AppController.player
        function onPlayingChanged() {
            if (!AppController.player.playing) {
                root.playingSeparationStem = ""
                root.playingVoiceClipPath = ""
            }
        }
    }

    function defaultExportPath() {
        var isVideo = /\.(mp4|mkv|mov|webm|avi)$/i.test(dubbing.sourceMediaPath)
        return dubbing.projectPath.replace(/\.json$/i, isVideo ? "-dubbed.mp4" : "-dubbed.wav")
    }

    function openWorkflowCanvas() {
        dubbing.prepareWorkflow()
        workflowDialog.open()
    }

    function stepTitle(stepId) {
        if (stepId === "import" || stepId === "media-input") return qsTr("Import/Download")
        if (stepId === "ingest") return qsTr("Normalize")
        if (stepId === "source-separate") return qsTr("Isolator")
        if (stepId === "transcribe") return qsTr("Transcribe/STT")
        if (stepId === "review-transcript" || stepId === "alignment-subtitle") return qsTr("Alignment/Subtitle")
        if (stepId === "translate") return qsTr("Translate")
        if (stepId === "synthesize") return qsTr("TTS")
        if (stepId === "fit-timing" || stepId === "review-conflicts" || stepId === "mix" || stepId === "timing-mix") return qsTr("Timing/Mix")
        if (stepId === "export") return qsTr("Export/Output")
        return qsTr("Completed")
    }

    function acceptSelectedSourceMedia(urlOrPath) {
        var path = AppController.files.urlToLocalPath(String(urlOrPath))
        return dubbing.importMedia(path)
    }

    function openAutomaticStageSetup(action, nodeId) {
        if (action === "node-model") {
            nodeModelDialog.openFor(nodeId)
            return
        }
        if (action === "subtitle") {
            subtitleEditorDialog.open()
            return
        }
        if (action === "export") {
            exportOptionsDialog.open()
            return
        }
        if (action === "source")
            automaticPreflightDialog.currentPage = 0
    }

    function stageIdForNode(nodeId) {
        if (nodeId === "media-input" || nodeId === "import") return "import"
        if (nodeId === "ingest") return "normalize"
        if (nodeId === "source-separate") return "isolator"
        if (nodeId === "transcribe") return "transcribe"
        if (nodeId === "review-transcript" || nodeId === "alignment-subtitle") return "alignment-subtitle"
        if (nodeId === "translate" || nodeId === "review-translation") return "translate"
        if (nodeId === "assign-voices" || nodeId === "synthesize") return "tts"
        if (nodeId === "fit-timing" || nodeId === "review-conflicts" || nodeId === "mix" || nodeId === "timing-mix") return "timing-mix"
        if (nodeId === "export") return "export"
        return nodeId
    }

    function actionNodeForStage(stageId) {
        var stages = dubbing.workflowStages || []
        for (var i = 0; i < stages.length; ++i)
            if (stages[i].id === stageId) return stages[i].actionNodeId || stageId
        return stageId
    }

    function workflowStage(stageId) {
        var stages = dubbing.workflowStages || []
        for (var i = 0; i < stages.length; ++i)
            if (stages[i].id === stageId) return stages[i]
        return null
    }

    function workflowNode(nodeId) {
        var nodes = dubbing.workflowNodes || []
        for (var i = 0; i < nodes.length; ++i)
            if (nodes[i].id === nodeId) return nodes[i]
        return null
    }

    function nextNodeId(nodeId) {
        var next = {"import": "ingest", "ingest": "source-separate", "source-separate": "transcribe", "transcribe": "alignment-subtitle", "review-transcript": "translate", "translate": "synthesize", "synthesize": "mix", "mix": "export"}
        return next[nodeId] || ""
    }

    function nextNodeReady(nodeId) {
        return root.nextNodeId(nodeId) !== "" && root.stepComplete(nodeId)
    }

    function runNextNode(nodeId) {
        var next = nextNodeId(nodeId)
        if (next === "") return
        if (next === "alignment-subtitle") {
            root.reviewStepId = "review-transcript"
            subtitleEditorDialog.open()
            return
        }
        root.reviewStepId = next
        // "Next" means execute the next workflow node, not only highlight it.
        // rerunStep also provides controller-side diagnostics for rejected runs.
        dubbing.rerunStep(next, root.defaultExportPath())
    }

    function stepComplete(stepId) {
        if (stepId === "import") return dubbing.sourceMediaPath.length > 0
        if (stepId === "ingest") return dubbing.normalizedAudioPath.length > 0
        if (stepId === "source-separate") return dubbing.vocalsPath.length > 0 && dubbing.backgroundPath.length > 0
        if (stepId === "transcribe" || stepId === "review-transcript" || stepId === "alignment-subtitle") return dubbing.segments.length > 0
        if (stepId === "translate") {
            if (dubbing.segments.length === 0) return false
            for (var i = 0; i < dubbing.segments.length; ++i)
                if (!(dubbing.segments[i].targetText || "").trim()) return false
            return true
        }
        if (stepId === "synthesize") {
            if (dubbing.segments.length === 0) return false
            for (var j = 0; j < dubbing.segments.length; ++j)
                if (!(dubbing.segments[j].clipPath || "")) return false
            return true
        }
        if (stepId === "mix" || stepId === "timing-mix") return dubbing.previewPath.length > 0
        if (stepId === "export") return dubbing.exportPath.length > 0
        return false
    }

    function canRerunStep(stepId) {
        return stepId !== "import" && stepId !== "completed" && root.stepComplete(stepId)
    }

    function canRunStep(stepId) {
        return ["ingest", "source-separate", "transcribe", "translate",
                "synthesize", "mix", "export"].indexOf(stepId) >= 0
            && !root.stepComplete(stepId)
    }

    function stepRunReady(stepId) {
        var node = root.workflowNode(stepId)
        if (!node || node.state === "missing" || node.state === "blocked") return false
        if (stepId === "transcribe"
                && (dubbing.transcriptConfiguration.transcriptSource || "stt") === "ocr")
            return node.state === "ready" || node.state === "completed"
        if (stepId === "synthesize" && !dubbing.ttsVoiceSelectionValid) return false
        if (node.configurable === true && node.selectedFamilyId
                && node.providerState !== "ready") return false
        return true
    }

    function beginQmlSmokeTranscriptSourceCheck() {
        qmlSmokeTranscriptSourcePhase = 0
        qmlSmokeTranscriptSourceFailure = ""
    }

    // Return 0 while QML is settling, 1 for a verified route, and -1 for a
    // concrete contract failure.  Main.qml deliberately preserves this
    // tri-state result instead of converting a pending layout into `false`.
    function qmlSmokeTranscriptSourceCheck() {
        if (qmlSmokeTranscriptSourcePhase === 0) {
            reviewStepId = "transcribe"
            qmlSmokeTranscriptSourcePhase = 1
            return 0
        }
        if (qmlSmokeTranscriptSourcePhase === 1) {
            if (!dubbingTranscriptSourcePanel.visible) {
                qmlSmokeTranscriptSourceFailure = "transcript source panel is not visible (displayed="
                        + displayedStepId + ", review=" + reviewStepId
                        + ", processing=" + dubbing.processing
                        + ", error=" + (dubbing.lastError || "none") + ")"
                return -1
            }
            if (!dubbingTranscriptSourceMode.visible) {
                qmlSmokeTranscriptSourceFailure = "transcript source selector is not visible"
                return -1
            }
            if (dubbingTranscriptSourceMode.count !== 3) {
                qmlSmokeTranscriptSourceFailure = "transcript source selector count is "
                        + dubbingTranscriptSourceMode.count + ", expected 3"
                return -1
            }
            if (dubbingTranscriptSourcePanel.width <= 0
                    || dubbingTranscriptSourceMode.width <= 0) {
                qmlSmokeTranscriptSourceFailure = "transcript source layout has non-positive width"
                return -1
            }
            if (dubbingWorkspaceScroller.contentWidth < dubbingWorkspaceRow.width) {
                qmlSmokeTranscriptSourceFailure = "workspace content width is smaller than its row"
                return -1
            }
            if (dubbingWorkspaceRow.width > dubbingWorkspaceScroller.width
                    && !dubbingWorkspaceHorizontalScrollBar.visible) {
                qmlSmokeTranscriptSourceFailure = "horizontal workspace overflow has no visible scrollbar"
                return -1
            }
            subtitleEditorDialog.open()
            exportOptionsDialog.beginQmlSmokeExportRoutesCheck()
            exportOptionsDialog.open()
            reviewStepId = "synthesize"
            qmlSmokeTranscriptSourcePhase = 2
            return 0
        }
        if (dubbingTranscriptSourceMode.model[0].id !== "stt"
                || dubbingTranscriptSourceMode.model[1].id !== "ocr"
                || dubbingTranscriptSourceMode.model[2].id !== "stt+ocr") {
            qmlSmokeTranscriptSourceFailure = "transcript source model order changed"
            return -1
        }
        if (!sourceMediaPanel.qmlSmokeMediaControlsCheck()) {
            qmlSmokeTranscriptSourceFailure = "source-media controls smoke contract failed"
            return -1
        }
        if (!subtitleEditorDialog.qmlSmokeLayoutCheck()) {
            qmlSmokeTranscriptSourceFailure = "subtitle editor dialog smoke contract failed"
            return -1
        }
        if (!dubbingVoiceClipReview.qmlSmokeTimingResolutionCheck()) {
            qmlSmokeTranscriptSourceFailure = "voice clip timing smoke contract failed"
            return -1
        }
        var exportRoutesCheck = exportOptionsDialog.qmlSmokeExportRoutesCheck()
        if (exportRoutesCheck === 0)
            return 0
        if (exportRoutesCheck < 0) {
            qmlSmokeTranscriptSourceFailure = "export options dialog smoke contract failed: "
                    + exportOptionsDialog.qmlSmokeExportRoutesFailure
            return -1
        }
        return 1
    }

    function beginQmlSmokeAutomaticPreflightCheck() {
        qmlSmokeAutomaticPhase = 0
        qmlSmokeAutomaticStageIndex = 0
        qmlSmokeMediaPickerRequested = false
        qmlSmokeTranscriptSourceFailure = ""
    }

    // Production-shell offscreen interaction contract.  Every transition here
    // is initiated by the actual QML control's click() method; only the native
    // file-picker result is injected at its explicit picker boundary.
    function qmlSmokeAutomaticPreflightCheck() {
        var configuredStages = ["media-input", "source-separate", "transcribe",
                                "review-transcript", "translate", "synthesize", "export"]
        if (qmlSmokeAutomaticPhase === 0) {
            if (!dubbingEntryGate.visible) {
                qmlSmokeTranscriptSourceFailure = "Dubbing entry gate did not block the workspace"
                return -1
            }
            subtitleEditorDialog.close()
            exportOptionsDialog.close()
            ApplicationWindow.window.recordQmlSmokeDubbing("dubbingEntryAutomaticButton", "click",
                                                            "entry-gate", "source-language")
            dubbingEntryGate.qmlSmokeClickAutomatic()
            qmlSmokeAutomaticPhase = 1
            return 0
        }
        if (qmlSmokeAutomaticPhase === 1) {
            if (!automaticPreflightDialog.visible || automaticPreflightDialog.currentPage !== 0) {
                qmlSmokeTranscriptSourceFailure = "Automatic did not open Source & language preflight"
                return -1
            }
            // Missing source is an intentional Review failure. The real Fix
            // control must bring the operator back to the source card before
            // the Browse control can be used.
            automaticPreflightDialog.currentPage = 3
            if (!automaticPreflightDialog.qmlSmokeClickFix("source-media")) {
                qmlSmokeTranscriptSourceFailure = "Review did not expose Fix for missing source media"
                return -1
            }
            ApplicationWindow.window.recordQmlSmokeDubbing("dubbingPreflightFix_source-media", "click",
                                                            "review-source-media-error", "source-page")
            qmlSmokeAutomaticPhase = 11
            return 0
        }
        if (qmlSmokeAutomaticPhase === 11) {
            if (automaticPreflightDialog.currentPage !== 0) {
                qmlSmokeTranscriptSourceFailure = "Review Fix did not navigate to Source & language"
                return -1
            }
            automaticPreflightDialog.qmlSmokeClickSourceBrowse()
            ApplicationWindow.window.recordQmlSmokeDubbing("dubbingPreflightSourceBrowseButton", "click",
                                                            "source-empty", "file-picker-requested")
            qmlSmokeAutomaticPhase = 2
            return 0
        }
        if (qmlSmokeAutomaticPhase === 2) {
            if (!qmlSmokeMediaPickerRequested) {
                qmlSmokeTranscriptSourceFailure = "Source Browse did not request the production file picker"
                return -1
            }
            mediaFileDialog.close()
            if (qmlSmokeMediaPath === "" || !acceptSelectedSourceMedia(qmlSmokeMediaPath)) {
                qmlSmokeTranscriptSourceFailure = "The production file-picker boundary did not persist source media"
                return -1
            }
            ApplicationWindow.window.recordQmlSmokeDubbing("file-picker-boundary", "accept",
                                                            "source-empty", "source-persisted")
            automaticPreflightDialog.qmlSmokeSelectLanguages()
            qmlSmokeAutomaticPhase = 3
            return 0
        }
        if (qmlSmokeAutomaticPhase === 3) {
            if (dubbing.sourceMediaPath === "" || dubbing.sourceLanguage !== "zh" || dubbing.targetLanguage !== "vi") {
                qmlSmokeTranscriptSourceFailure = "Source media or language selection was not persisted into DubbingController"
                return -1
            }
            if (!automaticPreflightDialog.qmlSmokeClickNext()) {
                qmlSmokeTranscriptSourceFailure = "Source preflight Next remained disabled after media and languages were persisted"
                return -1
            }
            ApplicationWindow.window.recordQmlSmokeDubbing("dubbingPreflightNextButton", "click",
                                                            "source-language", "stages")
            qmlSmokeAutomaticPhase = 4
            return 0
        }
        if (qmlSmokeAutomaticPhase === 4) {
            if (automaticPreflightDialog.currentPage !== 1) {
                qmlSmokeTranscriptSourceFailure = "Source preflight Next did not navigate to stages"
                return -1
            }
            if (qmlSmokeAutomaticStageIndex >= configuredStages.length) {
                automaticPreflightDialog.qmlSmokeClickNext()
                ApplicationWindow.window.recordQmlSmokeDubbing("dubbingPreflightNextButton", "click",
                                                                "stages", "colab-workers")
                qmlSmokeAutomaticPhase = 6
                return 0
            }
            var stageId = configuredStages[qmlSmokeAutomaticStageIndex]
            if (!automaticPreflightDialog.qmlSmokeClickStageSetup(stageId)) {
                qmlSmokeTranscriptSourceFailure = "Visible Configure has no actionable QML control for " + stageId
                return -1
            }
            ApplicationWindow.window.recordQmlSmokeDubbing("dubbingPreflightConfigure_" + stageId,
                                                            "click", "stages", "setup-open-requested")
            qmlSmokeAutomaticPhase = 5
            return 0
        }
        if (qmlSmokeAutomaticPhase === 5) {
            var stageId = configuredStages[qmlSmokeAutomaticStageIndex]
            if (stageId === "media-input") {
                if (automaticPreflightDialog.currentPage !== 0) {
                    qmlSmokeTranscriptSourceFailure = "Import/Download Configure did not return to Source & language"
                    return -1
                }
                automaticPreflightDialog.currentPage = 1
            } else if (stageId === "review-transcript") {
                if (!subtitleEditorDialog.visible) {
                    qmlSmokeTranscriptSourceFailure = "Alignment/Subtitle Configure did not open the subtitle editor"
                    return -1
                }
                subtitleEditorDialog.close()
            } else if (stageId === "export") {
                if (!exportOptionsDialog.visible) {
                    qmlSmokeTranscriptSourceFailure = "Export Configure did not open export options"
                    return -1
                }
                exportOptionsDialog.close()
            } else {
                if (!nodeModelDialog.visible) {
                    qmlSmokeTranscriptSourceFailure = "Model Configure did not open the production model dialog for " + stageId
                    return -1
                }
                nodeModelDialog.close()
            }
            ++qmlSmokeAutomaticStageIndex
            qmlSmokeAutomaticPhase = 4
            return 0
        }
        if (qmlSmokeAutomaticPhase === 6) {
            if (automaticPreflightDialog.currentPage !== 2) {
                qmlSmokeTranscriptSourceFailure = "Stages Next did not navigate to Colab workers"
                return -1
            }
            // Standard Local routes have no Direct Colab worker and must not
            // block navigation. The next real control should reach Review.
            automaticPreflightDialog.qmlSmokeClickNext()
            ApplicationWindow.window.recordQmlSmokeDubbing("dubbingPreflightNextButton", "click",
                                                            "colab-workers-no-direct-worker", "review")
            qmlSmokeAutomaticPhase = 7
            return 0
        }
        if (qmlSmokeAutomaticPhase === 7) {
            if (automaticPreflightDialog.currentPage !== 3) {
                qmlSmokeTranscriptSourceFailure = "No-worker Colab page incorrectly blocked progress"
                return -1
            }
            ApplicationWindow.window.recordQmlSmokeDubbing("dubbingPreflightReview", "verify",
                                                            "colab-skipped", "review-visible")
            // The production route transition below must not inherit a modal
            // preflight overlay; this mirrors leaving the review without
            // starting a model workload in an offscreen smoke.
            automaticPreflightDialog.close()
            return 1
        }
        return -1
    }

    function runStep(stepId) {
        dubbing.rerunStep(stepId, root.defaultExportPath())
    }

    function generatedClipCount() {
        var count = 0
        for (var i = 0; i < dubbing.segments.length; ++i)
            if ((dubbing.segments[i].clipPath || "") !== "") ++count
        return count
    }

    function languageDisplayName(code) {
        for (var i = 0; i < root.languageCatalog.length; ++i) {
            if (root.languageCatalog[i].value === code)
                return root.languageCatalog[i].text || code
        }
        return code
    }

    component Field: TextField {
        color: Theme.textPrimary
        placeholderTextColor: Theme.textSecondary
        font.pixelSize: Theme.fontSmall
        selectByMouse: true
        leftPadding: Theme.paddingMedium
        rightPadding: Theme.paddingMedium
        background: Rectangle {
            radius: Theme.radiusSmall
            color: Qt.rgba(1, 1, 1, 0.035)
            border.color: parent.activeFocus ? Theme.accent : Qt.rgba(1, 1, 1, 0.09)
            border.width: parent.activeFocus ? 2 : 1
        }
    }

    component SegmentTextArea: AppTextArea {
        color: Theme.textPrimary
        placeholderTextColor: Theme.textSecondary
        font.pixelSize: Theme.fontSmall
        selectByMouse: true
        wrapMode: Text.Wrap
        padding: Theme.paddingSmall
        Layout.fillWidth: true
        Layout.minimumHeight: 30
        Layout.preferredHeight: Math.max(30, contentHeight + padding * 2)
        implicitHeight: Math.max(30, contentHeight + padding * 2)
    }

    component Panel: Rectangle {
        color: Theme.surface
        radius: Theme.radiusMedium
        border.color: Qt.rgba(1, 1, 1, 0.08)
        border.width: 1
    }

    // Workflow node settings are rendered by DubbingNodeSettingsPanel.

    component TranslationSettingsPanel: Rectangle {
        id: translationPanel
        readonly property var node: root.workflowNode("translate")
        readonly property int recommendationRevision: translationRecommendationController.familiesModel.revision
        readonly property var recommendation: recommendationRevision >= 0
            ? translationRecommendationController.familiesModel.recommendedConfiguration() : ({})
        readonly property bool configured: node && node.selectedFamilyId
        readonly property string modelName: configured
            ? (node.providerName || node.selectedFamilyId)
            : (recommendation.modelName || qsTr("No compatible model"))
        readonly property string runtimeName: configured
            ? (node.selectedRuntimeId || qsTr("Runtime not selected"))
            : (recommendation.runtimeName || recommendation.runtimeId || qsTr("Runtime unavailable"))
        readonly property bool ready: configured
            ? node.providerState === "ready"
            : recommendation.ready === true
        Layout.fillWidth: true
        Layout.preferredHeight: 72
        radius: Theme.radiusSmall
        color: Theme.surfaceAlt
        border.color: Qt.rgba(1, 1, 1, 0.08)
        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.paddingMedium
            spacing: Theme.paddingMedium
            Rectangle {
                Layout.preferredWidth: 34
                Layout.preferredHeight: 34
                radius: Theme.radiusSmall
                color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.14)
                LineIcon { anchors.centerIn: parent; name: "translate"; color: Theme.accentLight; width: 16; height: 16 }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.paddingSmall
                    Text { text: qsTr("Translation model"); color: Theme.textPrimary; font.pixelSize: Theme.fontSmall; font.bold: true }
                    Rectangle {
                        implicitWidth: recommendationLabel.implicitWidth + Theme.paddingSmall * 2
                        implicitHeight: 20
                        radius: Theme.radiusSmall
                        color: Qt.rgba(translationPanel.ready ? Theme.success.r : Theme.warning.r,
                                       translationPanel.ready ? Theme.success.g : Theme.warning.g,
                                       translationPanel.ready ? Theme.success.b : Theme.warning.b, 0.12)
                        Text {
                            id: recommendationLabel
                            anchors.centerIn: parent
                            text: translationPanel.configured
                                ? (translationPanel.ready ? qsTr("Ready") : qsTr("Setup required"))
                                : qsTr("Recommended")
                            color: translationPanel.ready ? Theme.success : Theme.warning
                            font.pixelSize: 10
                            font.bold: true
                        }
                    }
                    Item { Layout.fillWidth: true }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("%1  ·  %2").arg(translationPanel.modelName).arg(translationPanel.runtimeName)
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    visible: !translationPanel.configured && (translationPanel.recommendation.reason || "") !== ""
                    text: translationPanel.recommendation.reason || ""
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
            }
            PrimaryButton {
                text: qsTr("Choose model")
                iconName: "settings"
                quiet: true
                enabled: !dubbing.processing
                onClicked: nodeModelDialog.openFor("translate")
            }
            PrimaryButton {
                visible: root.canRunStep("translate")
                text: qsTr("Run")
                iconName: "play"
                enabled: !dubbing.processing && translationPanel.ready
                    && root.stepRunReady("translate")
                Layout.preferredWidth: 104
                onClicked: root.runStep("translate")
            }
            PrimaryButton {
                visible: root.canRerunStep("translate")
                text: qsTr("Run Again")
                iconName: "run-again"
                quiet: true
                enabled: !dubbing.processing && translationPanel.ready
                    && root.stepRunReady("translate")
                Layout.preferredWidth: 104
                onClicked: root.runStep("translate")
            }
            PrimaryButton {
                visible: root.nextNodeReady("translate")
                text: qsTr("Next")
                iconName: "chevron-right"
                enabled: !dubbing.processing
                onClicked: root.runNextNode("translate")
            }
        }
    }

    Rectangle { anchors.fill: parent; color: Theme.background }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        DubbingWorkflowHeader {
            dubbing: root.dubbing
            steps: [
                { stepId: "import", title: qsTr("Import/Download"), iconName: "folder", complete: (root.workflowStage("import") || {}).state === "completed", active: root.stageIdForNode(root.dubbing.currentStepId) === "import" },
                { stepId: "normalize", title: qsTr("Normalize"), iconName: "activity", complete: (root.workflowStage("normalize") || {}).state === "completed", active: root.stageIdForNode(root.dubbing.currentStepId) === "normalize" },
                { stepId: "isolator", title: qsTr("Isolator"), iconName: "waves", complete: (root.workflowStage("isolator") || {}).state === "completed", active: root.stageIdForNode(root.dubbing.currentStepId) === "isolator" },
                { stepId: "transcribe", title: qsTr("Transcribe/STT"), iconName: "mic", complete: (root.workflowStage("transcribe") || {}).state === "completed", active: root.stageIdForNode(root.dubbing.currentStepId) === "transcribe" },
                { stepId: "alignment-subtitle", title: qsTr("Alignment/Subtitle"), iconName: "alignment", complete: (root.workflowStage("alignment-subtitle") || {}).state === "completed", active: root.stageIdForNode(root.dubbing.currentStepId) === "alignment-subtitle" },
                { stepId: "translate", title: qsTr("Translate"), iconName: "translate", complete: (root.workflowStage("translate") || {}).state === "completed", active: root.stageIdForNode(root.dubbing.currentStepId) === "translate" },
                { stepId: "tts", title: qsTr("TTS"), iconName: "volume", complete: (root.workflowStage("tts") || {}).state === "completed", active: root.stageIdForNode(root.dubbing.currentStepId) === "tts" },
                { stepId: "timing-mix", title: qsTr("Timing/Mix"), iconName: "clock", complete: (root.workflowStage("timing-mix") || {}).state === "completed", active: root.stageIdForNode(root.dubbing.currentStepId) === "timing-mix" },
                { stepId: "export", title: qsTr("Export/Output"), iconName: "download", complete: (root.workflowStage("export") || {}).state === "completed", active: root.stageIdForNode(root.dubbing.currentStepId) === "export" }
            ]
            statusText: root.dubbing.processing
                        ? (root.dubbing.progressAvailable
                           ? qsTr("%1 · %2%").arg(root.stepTitle(root.dubbing.currentStepId)).arg(root.dubbing.progress)
                           : qsTr("%1 · Working").arg(root.stepTitle(root.dubbing.currentStepId)))
                        : (root.dubbing.workflowMode === "step" ? qsTr("Ready for node run") : qsTr("Ready"))
            defaultExportPath: root.defaultExportPath()
            historyOpen: root.isHistoryOpen
            settingsOpen: root.isNodeInspectorOpen
            onStepSelected: function(stepId) {
                if (!root.dubbing.processing) root.reviewStepId = root.actionNodeForStage(stepId)
            }
            onHistoryToggled: root.isHistoryOpen = !root.isHistoryOpen
            onSettingsToggled: root.isNodeInspectorOpen = !root.isNodeInspectorOpen
            onGenerateRequested: {
                automaticPreflightDialog.openPreflight()
            }
            onPauseRequested: root.dubbing.pauseAutomaticWorkflow()
            onStopRequested: root.dubbing.cancelProcessing()
            onWorkflowRequested: root.openWorkflowCanvas()
            onColabSetupRequested: dubbingColabSetupDialog.open()
            onSaveRequested: root.dubbing.saveProject()
            onExportRequested: exportOptionsDialog.open()
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 72 : 0
            visible: dubbing.automaticEvents.length > 0
            color: Theme.surface
            border.color: Qt.rgba(1, 1, 1, 0.08)
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.paddingLarge
                anchors.rightMargin: Theme.paddingLarge
                spacing: Theme.paddingMedium
                LineIcon {
                    name: dubbing.settingsLocked ? "activity" : (dubbing.workflowMode === "paused" ? "pause" : "workflow")
                    color: dubbing.settingsLocked ? Theme.warning : Theme.accentLight
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                }
                ColumnLayout {
                    Layout.preferredWidth: 310
                    spacing: 2
                    Text {
                        text: dubbing.settingsLocked ? qsTr("AUTOMATIC DUBBING") : qsTr("DUBBING STATUS")
                        color: Theme.textSecondary
                        font.pixelSize: 10
                        font.bold: true
                        font.letterSpacing: 1.0
                    }
                    Text {
                        Layout.fillWidth: true
                        text: dubbing.automaticStatusText
                        color: dubbing.settingsLocked ? Theme.warning : Theme.textPrimary
                        font.pixelSize: Theme.fontSmall
                        font.bold: true
                        elide: Text.ElideRight
                    }
                }
                Rectangle { Layout.fillHeight: true; Layout.preferredWidth: 1; color: Qt.rgba(1, 1, 1, 0.08) }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    orientation: ListView.Horizontal
                    spacing: Theme.paddingSmall
                    clip: true
                    model: dubbing.automaticEvents
                    onCountChanged: positionViewAtEnd()
                    delegate: Rectangle {
                        required property var modelData
                        width: Math.min(260, eventText.implicitWidth + Theme.paddingMedium * 2)
                        height: 42
                        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                        radius: Theme.radiusSmall
                        color: Qt.rgba(1, 1, 1, 0.035)
                        border.color: modelData.state === "failed" ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.45)
                                      : (modelData.state === "completed" ? Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.35)
                                                                         : Qt.rgba(1, 1, 1, 0.08))
                        Text {
                            id: eventText
                            anchors.fill: parent
                            anchors.margins: Theme.paddingSmall
                            text: (modelData.timestamp || "") + "  " + (modelData.message || "")
                            color: modelData.state === "failed" ? Theme.danger
                                   : (modelData.state === "completed" ? Theme.success : Theme.textSecondary)
                            font.pixelSize: 10
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        Flickable {
            id: dubbingWorkspaceScroller
            objectName: "dubbingWorkspaceScroller"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.paddingMedium
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalFlick
            contentWidth: Math.max(width, dubbingWorkspaceRow.implicitWidth + Theme.paddingMedium * 2)
            contentHeight: height

            ScrollBar.horizontal: ScrollBar {
                id: dubbingWorkspaceHorizontalScrollBar
                objectName: "dubbingWorkspaceHorizontalScrollBar"
                policy: dubbingWorkspaceScroller.contentWidth > dubbingWorkspaceScroller.width
                        ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            }

            RowLayout {
                id: dubbingWorkspaceRow
                x: Theme.paddingMedium
                y: 0
                width: Math.max(dubbingWorkspaceScroller.width - Theme.paddingMedium * 2,
                                implicitWidth)
                height: dubbingWorkspaceScroller.height
                enabled: !dubbing.settingsLocked
                spacing: Theme.paddingMedium

            DubbingHistoryPanel {
                id: historyPanel
                dubbing: root.dubbing
                expanded: root.isHistoryOpen
                onClearRequested: clearHistoryDialog.open()
                onDeleteRequested: function(historyId) {
                    root.pendingHistoryDeleteId = historyId
                    deleteHistoryDialog.open()
                }
                onProjectOpened: root.isHistoryOpen = false
                onExpandedChanged: root.isHistoryOpen = expanded
            }

            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: 500; spacing: Theme.paddingMedium

                DubbingSourceMediaPanel {
                    id: sourceMediaPanel
                    dubbing: root.dubbing
                    selectedSegment: root.selectedSegment
                    onBrowseRequested: mediaFileDialog.open()
                    onSubtitleEditorRequested: subtitleEditorDialog.open()
                    onLinkImportRequested: function(url) { root.dubbing.importMediaFromLink(url) }
                    onCancelLinkImportRequested: root.dubbing.cancelMediaLinkImport()
                    onSegmentSelected: root.selectedSegment = index
                    onSelectedSegmentChanged: root.selectedSegment = selectedSegment
                }
                Panel {
                    Layout.fillWidth: true; Layout.preferredHeight: 146
                    ColumnLayout { anchors.fill: parent; anchors.margins: Theme.paddingMedium; spacing: Theme.paddingSmall
                        RowLayout { Layout.fillWidth: true
                            Text { text: qsTr("TIMELINE"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall; font.bold: true; font.letterSpacing: 1.1; Layout.fillWidth: true }
                            Text { text: qsTr("%1 segments").arg(dubbing.segments.length); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                        }
                        WaveformView { Layout.fillWidth: true; Layout.fillHeight: true; framed: true; showPlaceholder: true; placeholderText: dubbing.sourceMediaPath.length > 0 ? qsTr("Waveform preview becomes available after audio analysis") : qsTr("Import media to begin") }
                        RowLayout { Layout.fillWidth: true
                            Text { text: qsTr("00:00"); color: Theme.textSecondary; font.pixelSize: 10 }
                            Item { Layout.fillWidth: true }
                            Text { text: dubbing.processing
                                         ? (dubbing.progressAvailable ? qsTr("Processing %1%").arg(dubbing.progress) : qsTr("Processing"))
                                         : qsTr("Edit transcript on the right"); color: Theme.textSecondary; font.pixelSize: 10 }
                        }
                    }
                }
            }

            Panel {
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: 670
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: Theme.paddingMedium; spacing: Theme.paddingSmall
                    visible: root.displayedStepId === "transcribe" || root.displayedStepId === "translate"
                    DubbingNodeSettingsPanel {
                        dubbing: root.dubbing
                        nodeId: root.displayedStepId
                        node: root.workflowNode(nodeId)
                        nodeTitle: root.stepTitle(nodeId)
                        canRun: root.canRunStep(nodeId)
                        canRerun: root.canRerunStep(nodeId)
                        runReady: root.stepRunReady(nodeId)
                        nextNodeId: root.nextNodeId(nodeId)
                        nextReady: root.nextNodeReady(nodeId)
                        visible: true
                        onConfigureRequested: nodeModelDialog.openFor(nodeId)
                        onLoadRequested: dubbing.loadWorkflowNodeModel(nodeId)
                        onUnloadRequested: dubbing.unloadWorkflowNodeModel(nodeId)
                        onReloadRequested: dubbing.reloadWorkflowNodeModel(nodeId)
                        onRunRequested: root.runStep(nodeId)
                        onNextRequested: root.runNextNode(nodeId)
                        onFixRequested: translationFixDialog.openForAll()
                    }
                    Rectangle {
                        id: dubbingTranscriptSourcePanel
                        objectName: "dubbingTranscriptSourcePanel"
                        visible: root.displayedStepId === "transcribe"
                        Layout.fillWidth: true
                        implicitHeight: transcriptSourceLayout.implicitHeight + Theme.paddingMedium * 2
                        radius: Theme.radiusSmall
                        color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.08)
                        border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.28)
                        border.width: 1
                        ColumnLayout {
                            id: transcriptSourceLayout
                            anchors.fill: parent
                            anchors.margins: Theme.paddingMedium
                            spacing: Theme.paddingSmall
                            Text {
                                text: qsTr("Transcript source")
                                color: Theme.textPrimary
                                font.bold: true
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                ComboBox {
                                    id: dubbingTranscriptSourceMode
                                    objectName: "dubbingTranscriptSourceMode"
                                    Layout.fillWidth: true
                                    textRole: "label"
                                    valueRole: "id"
                                    model: [
                                        { id: "stt", label: qsTr("Chỉ STT") },
                                        { id: "ocr", label: qsTr("Chỉ OCR") },
                                        { id: "stt+ocr", label: qsTr("STT + OCR") }
                                    ]
                                    currentIndex: {
                                        var source = dubbing.transcriptConfiguration.transcriptSource || "stt"
                                        for (var i = 0; i < model.length; ++i)
                                            if (model[i].id === source) return i
                                        return 0
                                    }
                                    enabled: !dubbing.processing
                                    onActivated: function(index) {
                                        dubbing.setWorkflowNodeParameters("transcribe", {
                                            transcriptSource: model[index].id
                                        })
                                    }
                                }
                                Text {
                                    text: (dubbing.transcriptConfiguration.transcriptSource || "stt") === "ocr"
                                          ? qsTr("Uses Subtitle OCR video, selected Local CPU/Colab route, language and ROI.")
                                          : (dubbing.transcriptConfiguration.transcriptSource || "stt") === "stt+ocr"
                                            ? qsTr("Both sources must succeed; conflicts remain for review.")
                                            : qsTr("Uses the existing audio STT route only.")
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSmall
                                    wrapMode: Text.WordWrap
                                    Layout.preferredWidth: 260
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: qsTr("Conflict policy")
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontSmall
                                }
                                ComboBox {
                                    id: dubbingFusionPolicyMode
                                    Layout.preferredWidth: 210
                                    textRole: "label"
                                    valueRole: "id"
                                    model: [
                                        { id: "ask", label: qsTr("Hỏi khi xung đột") },
                                        { id: "prefer-stt", label: qsTr("Ưu tiên STT") },
                                        { id: "prefer-ocr", label: qsTr("Ưu tiên OCR") },
                                        { id: "ai-suggest", label: qsTr("AI gợi ý") }
                                    ]
                                    currentIndex: {
                                        var policy = dubbing.transcriptConfiguration.fusionPolicy || "ask"
                                        for (var i = 0; i < model.length; ++i)
                                            if (model[i].id === policy) return i
                                        return 0
                                    }
                                    enabled: !dubbing.processing
                                    onActivated: function(index) {
                                        dubbing.setTranscriptFusionPolicy(model[index].id)
                                    }
                                }
                                Text {
                                    readonly property var aiAvailability: dubbing.transcriptConflictAiAvailability()
                                    Layout.fillWidth: true
                                    visible: (dubbing.transcriptConfiguration.fusionPolicy || "ask") === "ai-suggest"
                                    text: aiAvailability.available
                                          ? qsTr("AI only prepares a source-language suggestion; review is still required.")
                                          : (aiAvailability.reason || qsTr("Configure Translation Fix LLM to use AI suggestion."))
                                    color: aiAvailability.available ? Theme.textSecondary : Theme.warning
                                    font.pixelSize: 10
                                    wrapMode: Text.WordWrap
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                visible: dubbing.unresolvedTranscriptConflictCount > 0
                                spacing: Theme.paddingSmall
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("%1 unresolved STT/OCR conflict(s) block Translate until reviewed.")
                                          .arg(dubbing.unresolvedTranscriptConflictCount)
                                    color: Theme.warning
                                    font.pixelSize: 10
                                    wrapMode: Text.WordWrap
                                }
                                PrimaryButton {
                                    text: qsTr("Use STT for all")
                                    quiet: true
                                    enabled: !dubbing.processing
                                    onClicked: dubbing.resolveAllTranscriptConflicts("stt")
                                }
                                PrimaryButton {
                                    text: qsTr("Use OCR for all")
                                    quiet: true
                                    enabled: !dubbing.processing
                                    onClicked: dubbing.resolveAllTranscriptConflicts("ocr")
                                }
                                PrimaryButton {
                                    readonly property var aiAvailability: dubbing.transcriptConflictAiAvailability()
                                    text: qsTr("Request AI")
                                    quiet: true
                                    enabled: !dubbing.processing && aiAvailability.available
                                    toolTip: aiAvailability.available ? qsTr("Prepare suggestions only")
                                                                    : (aiAvailability.reason || "")
                                    onClicked: dubbing.requestTranscriptConflictAiSuggestion(-1)
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: (dubbing.transcriptConfiguration.transcriptSource || "stt") !== "stt"
                                text: qsTr("OCR setup is saved with this Dubbing project when you run: %1 · sample %2 ms · confidence %3")
                                      .arg(dubbing.transcriptConfiguration.ocrLanguage || qsTr("current Subtitle OCR language"))
                                      .arg(dubbing.transcriptConfiguration.ocrSampleIntervalMs || "—")
                                      .arg(dubbing.transcriptConfiguration.ocrMinimumConfidence === undefined
                                           ? "—" : Number(dubbing.transcriptConfiguration.ocrMinimumConfidence).toFixed(2))
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSmall
                                wrapMode: Text.WordWrap
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: (dubbing.transcriptConfiguration.transcriptSource || "stt") !== "stt"
                                text: dubbing.transcriptConfiguration.ocrExecutionRoute === "colab-gpu"
                                      ? qsTr("OCR route: Colab GPU · %1 · configure and check it in Subtitle OCR before this Dubbing run.")
                                            .arg(dubbing.transcriptConfiguration.ocrColabModelId || "pp-ocrv5-multilingual-3.1")
                                      : qsTr("OCR route: Local CPU · %1 · uses the same versioned Subtitle OCR engine and cache key.")
                                            .arg(dubbing.transcriptConfiguration.ocrLocalEngineId === "tesseract-baseline"
                                                 ? "Tesseract 5.5.1 baseline"
                                                 : "PaddleOCR PP-OCRv6 tiny 3.7.0")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSmall
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                    RowLayout { Layout.fillWidth: true
                        ColumnLayout { Layout.fillWidth: true; spacing: 1
                            Text { text: root.stepTitle(root.displayedStepId).toUpperCase(); color: Theme.textPrimary; font.pixelSize: Theme.fontLarge; font.bold: true }
                            Text { text: qsTr("Review and edit every segment before continuing."); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                        }
                    }
                    RowLayout { Layout.fillWidth: true; spacing: Theme.paddingSmall
                        Field { Layout.fillWidth: true; placeholderText: qsTr("Search segments...") }
                        Text { text: qsTr("%1 / %1").arg(dubbing.segments.length); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 30; color: Qt.rgba(1, 1, 1, 0.035); radius: Theme.radiusSmall
                        RowLayout { anchors.fill: parent; anchors.leftMargin: Theme.paddingSmall; anchors.rightMargin: Theme.paddingSmall; spacing: Theme.paddingSmall
                            Text { text: qsTr("TIME"); Layout.preferredWidth: 88; color: Theme.textSecondary; font.pixelSize: 10; font.bold: true }
                            Text { text: qsTr("SOURCE / TARGET TEXT"); Layout.fillWidth: true; color: Theme.textSecondary; font.pixelSize: 10; font.bold: true }
                            Text { text: qsTr("STATE"); Layout.preferredWidth: 64; color: Theme.textSecondary; font.pixelSize: 10; font.bold: true }
                            Item { Layout.preferredWidth: 84 }
                        }
                    }
                    ListView {
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 5; model: dubbing.segments
                        delegate: Rectangle {
                            id: segmentDelegate
                            property bool needsTranslationFix:
                                root.displayedStepId === "translate"
                                && (modelData.targetText || "") !== ""
                                && modelData.durationBudget !== undefined
                                && dubbing.translationSegmentNeedsFix(index)

                            width: ListView.view.width
                            height: Math.max(98, segmentTextColumn.implicitHeight + Theme.paddingSmall * 2)
                            radius: Theme.radiusSmall
                            color: root.selectedSegment === index ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.12) : Qt.rgba(1, 1, 1, 0.025)
                            border.color: root.selectedSegment === index ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.55) : Qt.rgba(1, 1, 1, 0.06); border.width: 1
                            MouseArea {
                                anchors.fill: parent; z: -1
                                onClicked: {
                                    root.selectedSegment = index
                                    sourceMediaPanel.seekToSegment(index)
                                }
                            }
                            RowLayout { anchors.fill: parent; anchors.margins: Theme.paddingSmall; spacing: Theme.paddingSmall
                                Text { text: "%1–%2".arg(modelData.startMs).arg(modelData.endMs); color: Theme.textSecondary; font.pixelSize: 10; Layout.preferredWidth: 88; elide: Text.ElideRight }
                                ColumnLayout { id: segmentTextColumn; Layout.fillWidth: true; spacing: 3
                                    SegmentTextArea {
                                        text: modelData.sourceText || ""
                                        placeholderText: qsTr("Source transcript")
                                        onActiveFocusChanged: if (!activeFocus) dubbing.updateSegment(index, { sourceText: text })
                                    }
                                    SegmentTextArea {
                                        text: modelData.targetText || ""
                                        placeholderText: qsTr("Target translation")
                                        onActiveFocusChanged: if (!activeFocus) dubbing.updateSegment(index, { targetText: text })
                                    }
                                    Rectangle {
                                        objectName: "dubbingTranscriptConflict-" + index
                                        visible: modelData.fusionStatus === "conflict"
                                        Layout.fillWidth: true
                                        implicitHeight: fusionConflictLayout.implicitHeight + Theme.paddingSmall * 2
                                        radius: Theme.radiusSmall
                                        color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.12)
                                        border.color: Theme.warning
                                        border.width: 1
                                        ColumnLayout {
                                            id: fusionConflictLayout
                                            anchors.fill: parent
                                            anchors.margins: Theme.paddingSmall
                                            spacing: 2
                                            Text {
                                                Layout.fillWidth: true
                                                text: qsTr("STT/OCR conflict — choose the reviewed source; no automatic decision was made.")
                                                color: Theme.warning
                                                font.pixelSize: Theme.fontSmall
                                                font.bold: true
                                                wrapMode: Text.WordWrap
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                text: qsTr("STT (%1): %2").arg(Number(modelData.sttConfidence || 0).toFixed(2)).arg(modelData.fusionSttText || "")
                                                color: Theme.textSecondary
                                                wrapMode: Text.WordWrap
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                text: qsTr("OCR (%1): %2").arg(Number(modelData.ocrConfidence || 0).toFixed(2)).arg(modelData.fusionOcrText || "")
                                                color: Theme.textSecondary
                                                wrapMode: Text.WordWrap
                                            }
                                            RowLayout {
                                                PrimaryButton {
                                                    text: qsTr("Seek")
                                                    quiet: true
                                                    enabled: !dubbing.processing
                                                    onClicked: {
                                                        root.selectedSegment = index
                                                        sourceMediaPanel.seekToSegment(index)
                                                    }
                                                }
                                                PrimaryButton {
                                                    text: qsTr("Preview crop")
                                                    quiet: true
                                                    visible: (dubbing.transcriptConfiguration.transcriptSource || "stt") !== "stt"
                                                    enabled: !dubbing.processing
                                                    onClicked: dubbing.previewDubbingOcrCrop(modelData.startMs || 0)
                                                }
                                                PrimaryButton {
                                                    objectName: "dubbingUseSttConflict-" + index
                                                    text: qsTr("Use STT")
                                                    quiet: true
                                                    enabled: !dubbing.processing
                                                    onClicked: dubbing.resolveTranscriptConflict(index, "stt")
                                                }
                                                PrimaryButton {
                                                    objectName: "dubbingUseOcrConflict-" + index
                                                    text: qsTr("Use OCR")
                                                    quiet: true
                                                    enabled: !dubbing.processing
                                                    onClicked: dubbing.resolveTranscriptConflict(index, "ocr")
                                                }
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                visible: (modelData.fusionAiSuggestion || "") !== ""
                                                text: qsTr("AI suggestion (%1): %2")
                                                      .arg(modelData.fusionAiSuggestionLanguage || "source")
                                                      .arg(modelData.fusionAiSuggestion || "")
                                                color: Theme.textSecondary
                                                font.pixelSize: Theme.fontSmall
                                                wrapMode: Text.WordWrap
                                            }
                                            RowLayout {
                                                Layout.fillWidth: true
                                                visible: (modelData.fusionAiSuggestionStatus || "") === "pending"
                                                PrimaryButton {
                                                    text: qsTr("Accept AI")
                                                    quiet: true
                                                    enabled: !dubbing.processing
                                                    onClicked: dubbing.acceptTranscriptConflictAiSuggestion(index)
                                                }
                                                PrimaryButton {
                                                    text: qsTr("Reject AI")
                                                    quiet: true
                                                    enabled: !dubbing.processing
                                                    onClicked: dubbing.rejectTranscriptConflictAiSuggestion(index)
                                                }
                                                Item { Layout.fillWidth: true }
                                            }
                                        }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        visible: modelData.durationBudget !== undefined
                                        text: modelData.durationBudget
                                              ? qsTr("Budget %1–%2 phonemes · current %3 · %4")
                                                    .arg(modelData.durationBudget.minUnits || 0)
                                                    .arg(modelData.durationBudget.maxUnits || 0)
                                                    .arg(modelData.durationUnits !== undefined
                                                         ? modelData.durationUnits : "—")
                                                    .arg(modelData.durationStatus || qsTr("pending"))
                                              : ""
                                        color: modelData.durationStatus === "within-budget" ? Theme.success : Theme.warning
                                        font.pixelSize: 9
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        visible: (modelData.translationDiagnostic || "") !== ""
                                        text: modelData.translationDiagnostic || ""
                                        color: Theme.warning
                                        font.pixelSize: 9
                                        wrapMode: Text.WordWrap
                                    }
                                }
                                Text { text: modelData.state || qsTr("Ready"); color: modelData.state === "stale" ? Theme.warning : Theme.textSecondary; font.pixelSize: 10; Layout.preferredWidth: 64; horizontalAlignment: Text.AlignRight }
                                RowLayout {
                                    Layout.preferredWidth: 84
                                    Layout.minimumWidth: 84
                                    Layout.alignment: Qt.AlignVCenter
                                    spacing: Theme.paddingSmall
                                    Item {
                                        visible: root.displayedStepId === "translate"
                                        Layout.preferredWidth: 38
                                        Layout.minimumWidth: 38
                                        Layout.preferredHeight: 38
                                        Layout.alignment: Qt.AlignVCenter
                                        PrimaryButton {
                                            anchors.fill: parent
                                            visible: (modelData.targetText || "") !== ""
                                                     && modelData.durationBudget !== undefined
                                            text: ""
                                            iconName: "spark"
                                            iconOnly: true
                                            quiet: true
                                            enabled: !dubbing.processing
                                                     && segmentDelegate.needsTranslationFix
                                            toolTip: segmentDelegate.needsTranslationFix
                                                     ? qsTr("Rewrite only this segment")
                                                     : qsTr("This segment is already within its phoneme budget")
                                            onClicked: translationFixDialog.openForSegment(index)
                                        }
                                    }
                                    PrimaryButton {
                                        text: ""
                                        iconName: "trash"
                                        iconOnly: true
                                        quiet: true
                                        textColor: Theme.danger
                                        toolTip: qsTr("Remove segment")
                                        onClicked: dubbing.removeSegment(index)
                                    }
                                }
                            }
                        }
                        Column { anchors.centerIn: parent; visible: dubbing.segments.length === 0; spacing: Theme.paddingSmall
                            LineIcon { anchors.horizontalCenter: parent.horizontalCenter; name: "mic"; color: Theme.accentLight; width: 32; height: 32 }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("Your transcript will appear here"); color: Theme.textPrimary; font.pixelSize: Theme.fontMedium; font.bold: true }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("Import media, then run transcription."); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                        }
                    }
                }
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: Theme.paddingLarge; spacing: Theme.paddingMedium
                    visible: root.displayedStepId !== "transcribe" && root.displayedStepId !== "translate"
                    DubbingNodeSettingsPanel {
                        dubbing: root.dubbing
                        nodeId: root.displayedStepId
                        node: root.workflowNode(nodeId)
                        nodeTitle: root.stepTitle(nodeId)
                        canRun: root.canRunStep(nodeId)
                        canRerun: root.canRerunStep(nodeId)
                        runReady: root.stepRunReady(nodeId)
                        nextNodeId: root.nextNodeId(nodeId)
                        nextReady: root.nextNodeReady(nodeId)
                        visible: ["import", "ingest", "source-separate", "synthesize", "mix", "export"].indexOf(root.displayedStepId) >= 0
                        onConfigureRequested: nodeModelDialog.openFor(nodeId)
                        onLoadRequested: dubbing.loadWorkflowNodeModel(nodeId)
                        onUnloadRequested: dubbing.unloadWorkflowNodeModel(nodeId)
                        onReloadRequested: dubbing.reloadWorkflowNodeModel(nodeId)
                        onRunRequested: root.runStep(nodeId)
                        onNextRequested: root.runNextNode(nodeId)
                    }
                    Panel {
                        visible: root.displayedStepId === "review-transcript"
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? alignmentSubtitleActions.implicitHeight + Theme.paddingLarge * 2 : 0
                        ColumnLayout {
                            id: alignmentSubtitleActions
                            anchors.fill: parent
                            anchors.margins: Theme.paddingLarge
                            spacing: Theme.paddingSmall
                            Text {
                                text: qsTr("ALIGNMENT / SUBTITLE")
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontLarge
                                font.bold: true
                            }
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("This production stage uses the timed transcript created by STT and/or Subtitle OCR. Review or import subtitle cues before translating; Alignment Studio can refine timestamp alignment when needed.")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSmall
                                wrapMode: Text.WordWrap
                            }
                            Text {
                                Layout.fillWidth: true
                                text: dubbing.segments.length > 0
                                      ? qsTr("%1 timed subtitle/transcript segments are available for review.").arg(dubbing.segments.length)
                                      : qsTr("Run Transcribe/STT before opening transcript or subtitle review.")
                                color: dubbing.segments.length > 0 ? Theme.success : Theme.warning
                                font.pixelSize: Theme.fontSmall
                                wrapMode: Text.WordWrap
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                PrimaryButton {
                                    text: qsTr("Open subtitle editor")
                                    iconName: "edit"
                                    enabled: !dubbing.processing && dubbing.segments.length > 0
                                    onClicked: subtitleEditorDialog.open()
                                }
                                PrimaryButton {
                                    text: qsTr("Open Alignment Studio")
                                    iconName: "alignment"
                                    quiet: true
                                    enabled: !dubbing.processing && dubbing.normalizedAudioPath !== "" && dubbing.segments.length > 0
                                    onClicked: AppController.workflows.openStudioRoute("studio-alignment")
                                }
                                Item { Layout.fillWidth: true }
                                PrimaryButton {
                                    text: qsTr("Continue to Translate")
                                    iconName: "chevron-right"
                                    enabled: !dubbing.processing && dubbing.segments.length > 0
                                    onClicked: root.runNextNode("review-transcript")
                                }
                            }
                        }
                    }
                    RowLayout { Layout.fillWidth: true
                        ColumnLayout { Layout.fillWidth: true; spacing: 1
                            Text { text: root.stepTitle(root.displayedStepId).toUpperCase(); color: Theme.textPrimary; font.pixelSize: Theme.fontLarge; font.bold: true }
                            Text { text: root.displayedStepId === "import" ? qsTr("Import only selects the source; no processing starts automatically.") : qsTr("Review this step output before continuing."); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                        }
                    }
                    Item { Layout.fillHeight: true; visible: root.displayedStepId !== "synthesize" }
                    VoiceSeparationOutput {
                        visible: root.displayedStepId === "source-separate"
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? implicitHeight : 0
                        compact: true
                        showActions: true
                        showPlaybackControls: true
                        showExportButton: false
                        showWaveforms: false
                        vocalsPath: dubbing.vocalsPath
                        backgroundPath: dubbing.backgroundPath
                        playingStem: root.playingSeparationStem
                        onPlayRequested: function(kind, path) {
                            if (root.playingSeparationStem === kind && AppController.player.playing) {
                                AppController.player.stop()
                            } else {
                                root.playingVoiceClipPath = ""
                                AppController.player.playFile(path)
                                root.playingSeparationStem = kind
                            }
                        }
                    }
                    DubbingVoiceClipReview {
                        id: dubbingVoiceClipReview
                        visible: root.displayedStepId === "synthesize"
                        dubbing: root.dubbing
                        sourceMediaPanel: sourceMediaPanel
                        playingVoiceClipPath: root.playingVoiceClipPath
                        generatedClipCount: root.generatedClipCount()
                        synthesisComplete: root.stepComplete("synthesize")
                        onVoiceClipPlaybackRequested: root.playingVoiceClipPath = path
                        onSeparationPlaybackStopped: root.playingSeparationStem = ""
                    }
                    ColumnLayout {
                        visible: root.displayedStepId !== "source-separate"
                                 && root.displayedStepId !== "synthesize"
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: Theme.paddingMedium
                        LineIcon { Layout.alignment: Qt.AlignHCenter; name: root.displayedStepId === "synthesize" ? "volume" : "folder"; color: Theme.accentLight; Layout.preferredWidth: 40; Layout.preferredHeight: 40 }
                        Text { Layout.alignment: Qt.AlignHCenter; text: root.stepComplete(root.displayedStepId) ? qsTr("Step output is ready") : qsTr("No output for this step yet"); color: root.stepComplete(root.displayedStepId) ? Theme.success : Theme.textPrimary; font.pixelSize: Theme.fontMedium; font.bold: true }
                        Text {
                            Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideMiddle
                            color: Theme.textSecondary; font.pixelSize: Theme.fontSmall
                            text: root.displayedStepId === "import" ? dubbing.sourceMediaPath
                                : root.displayedStepId === "ingest" ? (dubbing.normalizedAudioPath || qsTr("Run Normalize to create the working audio."))
                                : root.displayedStepId === "synthesize" ? qsTr("%1 segment clips available").arg(dubbing.segments.length)
                                : root.displayedStepId === "export" ? (dubbing.exportPath || dubbing.previewPath || qsTr("Run Mix and Export to create final media."))
                                : qsTr("Select a step in the topbar to inspect its output.")
                        }
                    }
                    Item {
                        Layout.fillHeight: true
                        visible: root.displayedStepId !== "synthesize"
                    }
                }
            }

                DubbingNodeInspector {
                    dubbing: root.dubbing
                    nodeId: root.displayedStepId
                    node: root.workflowNode(root.displayedStepId)
                    nodeTitle: root.stepTitle(root.displayedStepId)
                    visible: root.isNodeInspectorOpen
                             && node && node.configurable === true
                    onCloseRequested: root.isNodeInspectorOpen = false
                    onRewriteSetupRequested: qualityDialog.openForMode("custom")
                }
            }
        }

        DubbingProjectStatusPanel {
            dubbing: root.dubbing
            enabled: !root.dubbing.settingsLocked
            languageCatalog: root.languageCatalog
            currentStepTitle: root.stepTitle(root.dubbing.currentStepId)
            onAdaptiveSetupRequested: qualityDialog.openForMode("adaptive")
            onCustomSetupRequested: qualityDialog.openForMode("custom")
        }
    }

    FileDialog {
        id: mediaFileDialog
        title: qsTr("Select media file")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Media files (*.wav *.mp3 *.mp4 *.mkv *.mov *.webm)"), qsTr("All files (*)")]
        onAccepted: {
            root.acceptSelectedSourceMedia(selectedFile.toString())
        }
    }

    DubbingExportDialog {
        id: exportOptionsDialog
        parent: Overlay.overlay
        projectName: dubbing.projectPath
        videoSource: root.isVideoSource
        busy: dubbing.processing
        segmentCount: dubbing.segments.length
        generatedClipCount: root.generatedClipCount()
        sourceLanguageCode: dubbing.sourceLanguage
        sourceLanguageName: root.languageDisplayName(dubbing.sourceLanguage)
        targetLanguageCode: dubbing.targetLanguage
        targetLanguageName: root.languageDisplayName(dubbing.targetLanguage)
        capCutDraftPath: dubbing.capCutDraftPath
        capCutDraftWarning: dubbing.capCutDraftWarning
        onVideoExportRequested: videoExportFileDialog.open()
        onAudioExportRequested: function(stem) {
            root.pendingAudioExportStem = stem
            audioExportFileDialog.open()
        }
        onSubtitleExportRequested: function(format, useTargetText, languageCode) {
            root.pendingSubtitleFormat = format
            root.pendingSubtitleUsesTarget = useTargetText
            root.pendingSubtitleLanguageCode = languageCode
            subtitleExportFileDialog.open()
        }
        onPackageExportRequested: packageExportFolderDialog.open()
        onCapCutDraftExportRequested: capCutDraftFolderDialog.open()
    }

    DubbingSubtitleEditor {
        id: subtitleEditorDialog
        dubbing: root.dubbing
    }

    DubbingColabSetupDialog {
        id: dubbingColabSetupDialog
        dubbing: root.dubbing
    }

    DubbingEntryGateDialog {
        id: dubbingEntryGate
        dubbing: root.dubbing
        onAutomaticRequested: {
            if (!root.dubbing.chooseDubbingEntryMode("automatic")) return
            close()
            automaticPreflightDialog.openPreflight()
        }
        onStepByStepRequested: {
            if (!root.dubbing.chooseDubbingEntryMode("step")) return
            close()
            root.dubbing.startStepByStep()
        }
        onLeaveDubbingRequested: {
            close()
            AppController.workflows.openStudioRoute("welcome")
        }
    }

    DubbingAutomaticPreflightDialog {
        id: automaticPreflightDialog
        dubbing: root.dubbing
        outputPath: root.defaultExportPath()
        onBackToEntryRequested: dubbingEntryGate.openGate()
        onSourceBrowseRequested: {
            root.qmlSmokeMediaPickerRequested = true
            mediaFileDialog.open()
        }
        onSourceLinkImportRequested: function(url) { dubbing.importMediaFromLink(url) }
        onStageSetupRequested: function(action, nodeId) { root.openAutomaticStageSetup(action, nodeId) }
        onColabSetupRequested: dubbingColabSetupDialog.open()
    }

    DubbingTranslationFixDialog {
        id: translationFixDialog
        parent: Overlay.overlay
        dubbing: root.dubbing
    }

    DubbingQualityDialog {
        id: qualityDialog
        dubbing: root.dubbing
        onLocalModelRequested: nodeModelDialog.openForCapability("adaptive-llm", "llm-chat")
    }

    FileDialog {
        id: videoExportFileDialog
        title: qsTr("Export dubbed video")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "mp4"
        nameFilters: [qsTr("MP4 video (*.mp4)")]
        onAccepted: dubbing.exportMedia(AppController.files.urlToLocalPath(selectedFile.toString()))
    }

    FileDialog {
        id: audioExportFileDialog
        title: root.pendingAudioExportStem === "vocal" ? qsTr("Export dubbed vocal stem")
                                                         : root.pendingAudioExportStem === "background" ? qsTr("Export background stem")
                                                                                                         : qsTr("Export dubbing mix")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "wav"
        nameFilters: [qsTr("WAV audio (*.wav)")]
        currentFile: root.pendingAudioExportStem === "vocal" ? "dubbed-vocals.wav"
                    : root.pendingAudioExportStem === "background" ? "background.wav" : "dubbed-mix.wav"
        onAccepted: dubbing.exportAudioStem(
                        root.pendingAudioExportStem,
                        AppController.files.urlToLocalPath(selectedFile.toString()))
    }

    property string pendingAudioExportStem: "mix"
    property string pendingSubtitleFormat: "srt"
    property bool pendingSubtitleUsesTarget: true
    property string pendingSubtitleLanguageCode: ""

    FileDialog {
        id: subtitleExportFileDialog
        title: root.pendingSubtitleFormat === "vtt" ? qsTr("Export WebVTT subtitles")
                                                    : qsTr("Export SubRip subtitles")
        fileMode: FileDialog.SaveFile
        defaultSuffix: root.pendingSubtitleFormat
        currentFile: "subtitles-" + (root.pendingSubtitleLanguageCode || "und")
                     + "." + root.pendingSubtitleFormat
        nameFilters: root.pendingSubtitleFormat === "vtt"
                     ? [qsTr("WebVTT subtitles (*.vtt)")]
                     : [qsTr("SubRip subtitles (*.srt)")]
        onAccepted: dubbing.exportSubtitles(
                        AppController.files.urlToLocalPath(selectedFile.toString()),
                        root.pendingSubtitleUsesTarget)
    }

    FolderDialog {
        id: packageExportFolderDialog
        title: qsTr("Choose review package folder")
        onAccepted: dubbing.exportPackage(AppController.files.urlToLocalPath(selectedFolder.toString()))
    }

    FolderDialog {
        id: capCutDraftFolderDialog
        title: qsTr("Choose parent folder for CapCut draft")
        onAccepted: dubbing.exportCapCutDraft(AppController.files.urlToLocalPath(selectedFolder.toString()))
    }

    ConfirmationDialog {
        id: deleteHistoryDialog
        parent: Overlay.overlay
        titleText: qsTr("Delete project from history")
        messageText: qsTr("The project file will not be deleted; only its history entry will be removed.")
        confirmText: qsTr("Delete")
        isDestructive: true
        onConfirmed: { dubbing.deleteHistoryItem(root.pendingHistoryDeleteId); root.pendingHistoryDeleteId = "" }
        onRejected: root.pendingHistoryDeleteId = ""
    }

    ConfirmationDialog {
        id: clearHistoryDialog
        parent: Overlay.overlay
        titleText: qsTr("Clear dubbing history")
        messageText: qsTr("All saved dubbing project entries will be removed from history.")
        confirmText: qsTr("Clear all")
        isDestructive: true
        onConfirmed: dubbing.clearHistory()
    }

    Dialog {
        id: interruptedWorkflowDialog
        parent: Overlay.overlay
        modal: true
        title: qsTr("Interrupted workflow")
        width: Math.min(440, parent ? parent.width - 32 : 440)
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0
        standardButtons: Dialog.NoButton

        contentItem: ColumnLayout {
            spacing: Theme.paddingMedium

            Text {
                Layout.fillWidth: true
                text: qsTr("A previous dubbing workflow stopped unexpectedly. You can continue from the last completed node or discard that interrupted run.")
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                visible: dubbing.workflowRecovery.activeNodeId !== ""
                text: qsTr("Last active node: %1").arg(dubbing.workflowRecovery.activeNodeId)
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSmall
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.paddingSmall
                spacing: Theme.paddingSmall

                PrimaryButton {
                    text: qsTr("Discard run")
                    quiet: true
                    Layout.fillWidth: true
                    onClicked: {
                        if (dubbing.discardInterruptedWorkflow()) interruptedWorkflowDialog.close()
                    }
                }

                PrimaryButton {
                    text: qsTr("Resume")
                    Layout.fillWidth: true
                    onClicked: {
                        if (dubbing.resumeInterruptedWorkflow()) interruptedWorkflowDialog.close()
                    }
                }
            }
        }
    }

    Connections {
        target: dubbing
        function onWorkflowChanged() {
            if (dubbing.workflowRecoveryAvailable && !interruptedWorkflowDialog.visible)
                interruptedWorkflowDialog.open()
        }
    }

    WorkflowPipelineDialog {
        id: workflowDialog
        nodes: dubbing.workflowStages; workflowReady: dubbing.workflowReady; statusText: dubbing.workflowStatusText
        allowIncompleteRun: dubbing.dubbingQuality === "custom"
        busy: dubbing.processing; progress: dubbing.progress / 100.0; progressAvailable: dubbing.progressAvailable; dialogTitle: qsTr("Dubbing workflow")
        reviewWaiting: dubbing.workflowWaitingForInput
        description: qsTr("Review the production-backed import, normalize, isolator, transcript, subtitle, translation, TTS, timing/mix, and export stages.")
        onPrepareRequested: dubbing.prepareWorkflow()
        onRunRequested: automaticPreflightDialog.openPreflight()
        onApproveRequested: dubbing.approveWorkflowReview()
        onRejectRequested: dubbing.rejectWorkflowReview(qsTr("Rejected from workflow review"))
        nodeConfigurations: dubbing.workflowNodeConfigurations
        onNodeConfigurationChanged: dubbing.setWorkflowNodeModel(nodeId, familyId, runtimeId, runtimeVersion, selectedFiles)
    }

    WorkflowNodeModelDialog {
        id: nodeModelDialog
        nodes: dubbing.workflowNodes
        nodeConfigurations: dubbing.workflowNodeConfigurations
        onConfigurationAccepted: function(nodeId, familyId, runtimeId, runtimeVersion, selectedFiles) {
            if (nodeId === "adaptive-llm") {
                adaptiveLlmController.saveConfigurationSelection(
                    familyId, runtimeId, runtimeVersion, selectedFiles)
                qualityDialog.localModelConfigured(
                    familyId, runtimeId, runtimeVersion, selectedFiles)
            } else {
                dubbing.setWorkflowNodeModel(
                    nodeId, familyId, runtimeId, runtimeVersion, selectedFiles)
            }
        }
    }
}
