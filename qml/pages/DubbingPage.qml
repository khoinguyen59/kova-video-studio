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
    // Follow the active worker by default, but let an operator inspect and
    // prepare a different manual stage while the current stage is running.
    // The active worker remains protected by its own controls.
    property bool followRunningStep: true
    readonly property string displayedStepId: reviewStepId
    property string observedCompletedStep: ""
    property string playingSeparationStem: ""
    property string playingVoiceClipPath: ""
    // Drawers must not permanently squeeze the editing canvas.  A selected
    // workflow task opens its inspector explicitly; History remains available
    // from the header toggle.
    property bool isHistoryOpen: false
    property bool isNodeInspectorOpen: false
    // The right hand side has one owner at a time: either the task's live
    // result/review surface or its advanced parameter inspector.  This avoids
    // the old two-inspector layout competing for the same narrow space.
    property bool isAdvancedNodeInspectorOpen: false
    // Project-wide language and execution-policy choices live in a setup
    // dialog after choosing Automatic or step-by-step. They are no longer a
    // permanent panel that steals space from the editor and timeline.
    property bool isProjectStatusPanelOpen: false
    property bool previewFocusMode: false
    // Dubbing has its own three-pane workspace and therefore cannot inherit
    // StudioShell's generic resizers. Keep these widths local to this real
    // layout so users can resize History, Preview and the step workspace.
    property int dubbingHistoryPanelWidth: 280
    property int dubbingTaskShelfWidth: 280
    property int dubbingPreviewPanelWidth: 860
    property int dubbingTimelinePanelHeight: 300
    property int dubbingStepPanelWidth: 380
    function clampedDubbingPanelWidth(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, Math.round(value)))
    }
    function clampedDubbingTimelineHeight(value) {
        return Math.max(160, Math.min(520, Math.round(value)))
    }
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
                if (root.followRunningStep)
                    root.reviewStepId = dubbing.currentStepId
            } else if (dubbing.lastCompletedStepId !== "" && dubbing.lastCompletedStepId !== root.observedCompletedStep) {
                root.followRunningStep = true
                root.observedCompletedStep = dubbing.lastCompletedStepId
                root.reviewStepId = dubbing.lastCompletedStepId
            }
        }
        function onWorkflowSetupRequired(nodeId, setupKind, message) {
            root.reviewStepId = nodeId === "adaptive-llm" ? "translate" : nodeId
            root.isNodeInspectorOpen = true
            root.isAdvancedNodeInspectorOpen = false
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
        if (stepId === "review-transcript") return qsTr("Transcribe/STT")
        if (stepId === "translate") return qsTr("Translate")
        if (stepId === "review-translation" || stepId === "subtitle") return qsTr("Subtitle")
        if (stepId === "synthesize") return qsTr("TTS")
        if (stepId === "fit-timing" || stepId === "review-conflicts" || stepId === "alignment") return qsTr("Alignment")
        if (stepId === "mix") return qsTr("Export/Output")
        if (stepId === "export") return qsTr("Export/Output")
        return qsTr("Completed")
    }

    function acceptSelectedSourceMedia(urlOrPath) {
        var path = AppController.files.urlToLocalPath(String(urlOrPath))
        var accepted = dubbing.importMedia(path)
        if (accepted)
            sourceMediaPanel.collapseSourceSetupAfterSelection()
        return accepted
    }

    function stageIdForNode(nodeId) {
        if (nodeId === "media-input" || nodeId === "import") return "import"
        if (nodeId === "ingest") return "normalize"
        if (nodeId === "source-separate") return "isolator"
        if (nodeId === "transcribe") return "transcribe"
        if (nodeId === "review-transcript") return "transcribe"
        if (nodeId === "translate") return "translate"
        if (nodeId === "review-translation") return "subtitle"
        if (nodeId === "assign-voices" || nodeId === "synthesize") return "tts"
        if (nodeId === "fit-timing" || nodeId === "review-conflicts") return "alignment"
        if (nodeId === "mix") return "export"
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
        var next = {"import": "ingest", "ingest": "source-separate", "source-separate": "transcribe", "transcribe": "review-transcript", "review-transcript": "translate", "translate": "review-translation", "review-translation": "synthesize", "synthesize": "fit-timing", "fit-timing": "mix", "mix": "export"}
        return next[nodeId] || ""
    }

    function nextNodeReady(nodeId) {
        return root.nextNodeId(nodeId) !== "" && root.stepComplete(nodeId)
    }

    function runNextNode(nodeId) {
        var next = nextNodeId(nodeId)
        if (next === "") return
        if (next === "review-transcript" || next === "review-translation") {
            root.reviewStepId = next
            subtitleEditorDialog.open()
            return
        }
        root.followRunningStep = true
        root.reviewStepId = next
        // "Next" means execute the next workflow node, not only highlight it.
        // rerunStep also provides controller-side diagnostics for rejected runs.
        dubbing.rerunStep(next, root.defaultExportPath())
    }

    function stepComplete(stepId) {
        if (stepId === "import") return dubbing.sourceMediaPath.length > 0
        if (stepId === "ingest") return dubbing.normalizedAudioPath.length > 0
        if (stepId === "source-separate") return dubbing.vocalsPath.length > 0 && dubbing.backgroundPath.length > 0
        if (stepId === "transcribe" || stepId === "review-transcript") return dubbing.segments.length > 0
        if (stepId === "translate" || stepId === "review-translation" || stepId === "subtitle") {
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
        if (stepId === "fit-timing" || stepId === "alignment") {
            if (dubbing.segments.length === 0) return false
            for (var k = 0; k < dubbing.segments.length; ++k)
                if (!(dubbing.segments[k].clipPath || "")) return false
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
                "synthesize", "fit-timing", "mix", "export"].indexOf(stepId) >= 0
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
        // The production workbench opens details only after selecting a task.
        // The route smoke intentionally selects Transcribe so it validates the
        // same left-controls/right-details state a user sees.
        isNodeInspectorOpen = true
        isAdvancedNodeInspectorOpen = false
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
            if (!dubbingTaskShelf.visible || dubbingTimelinePanel.width <= 0
                    || dubbingTimelineResizeHandle.height < 16) {
                qmlSmokeTranscriptSourceFailure = "Dubbing workbench shelf or full-width timeline is unavailable"
                return -1
            }
            if (dubbingWorkspaceRow.width > dubbingWorkspaceScroller.width + 1) {
                qmlSmokeTranscriptSourceFailure = "task panels overflow the fixed Dubbing workspace instead of resizing it"
                return -1
            }
            // The workbench is a three-pane editor: task controls, central
            // preview, and task review. These regions must consume real
            // layout space in that order, never paint over one another.
            if (dubbingTaskShelf.visible
                    && dubbingTaskShelf.x + dubbingTaskShelf.width > dubbingTaskShelfResizeHandle.x + 1) {
                qmlSmokeTranscriptSourceFailure = "task controls overlap their resize handle"
                return -1
            }
            if (dubbingTaskShelfResizeHandle.visible
                    && dubbingTaskShelfResizeHandle.x + dubbingTaskShelfResizeHandle.width > dubbingPreviewWorkspace.x + 1) {
                qmlSmokeTranscriptSourceFailure = "task controls overlay the video workspace"
                return -1
            }
            if (dubbingWorkspaceResizeHandle.visible
                    && dubbingPreviewWorkspace.x + dubbingPreviewWorkspace.width > dubbingWorkspaceResizeHandle.x + 1) {
                qmlSmokeTranscriptSourceFailure = "video workspace overlays its resize handle"
                return -1
            }
            if (dubbingStepReviewPanel.visible
                    && dubbingWorkspaceResizeHandle.x + dubbingWorkspaceResizeHandle.width > dubbingStepReviewPanel.x + 1) {
                qmlSmokeTranscriptSourceFailure = "video workspace overlays the task review panel"
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

    // Keep the interaction trace evidence-based: it records the exact
    // presentation-stage route, model and worker-card count before and after
    // a route change, rather than merely recording that a control was clicked.
    function qmlSmokeAutomaticRouteState() {
        var stages = automaticPreflightDialog.preflight.stages || []
        var stateFor = function(stageId) {
            for (var index = 0; index < stages.length; ++index) {
                if (stages[index].id === stageId)
                    return (stages[index].route || "Not selected") + "/"
                            + (stages[index].modelId || "No model")
            }
            return "missing"
        }
        return "isolator=" + stateFor("isolator")
                + ";translate=" + stateFor("translate")
                + ";workers=" + (automaticPreflightDialog.preflight.selectedWorkers || []).length
    }

    // Production-shell offscreen interaction contract.  Every transition here
    // is initiated by the actual QML control's click() method; only the native
    // file-picker result is injected at its explicit picker boundary.
    function qmlSmokeAutomaticPreflightCheck() {
        // "Subtitle" is an explicit post-translation review stage, but it has
        // no route/model configuration of its own.  Keep the smoke journey on
        // the stages that expose a real Configure control; the presentation
        // order itself is asserted by the controller regression.
        var configuredStages = ["import", "normalize", "isolator", "transcribe",
                                "translate", "tts", "alignment", "export"]
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
            // Let the real item tree receive both the controller update and
            // the deterministic drawer-collapse request before inspecting
            // geometry.  This is a layout settle boundary, not a mock wait.
            qmlSmokeAutomaticPhase = 30
            return 0
        }
        if (qmlSmokeAutomaticPhase === 30) {
            qmlSmokeAutomaticPhase = 3
            return 0
        }
        if (qmlSmokeAutomaticPhase === 3) {
            if (dubbing.sourceMediaPath === "" || dubbing.sourceLanguage !== "zh" || dubbing.targetLanguage !== "vi") {
                qmlSmokeTranscriptSourceFailure = "Source media or language selection was not persisted into DubbingController"
                return -1
            }
            if (!sourceMediaPanel.qmlSmokeLoadedSourceLayoutCheck()) {
                qmlSmokeTranscriptSourceFailure = "Loaded-source layout did not hide source setup, expose Open video, or preserve the selectable preview frame ratios"
                return -1
            }
            if (!dubbingWorkflowHeader.qmlSmokeClickProjectStatusToggle()
                    || !projectSetupDialog.visible
                    || root.isProjectStatusPanelOpen) {
                qmlSmokeTranscriptSourceFailure = "Project settings did not open as a dialog or left a permanent lower workspace panel"
                return -1
            }
            projectSetupDialog.close()
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
                var localRouteState = qmlSmokeAutomaticRouteState()
                if (!dubbing.setWorkflowNodeParameters("source-separate", {
                    executionProvider: "colab-direct",
                    modelId: dubbing.defaultColabModelForNode("source-separate")
                }) || !dubbing.setWorkflowNodeParameters("translate", {
                    executionProvider: "colab-direct",
                    modelId: dubbing.defaultColabModelForNode("translate")
                })) {
                    qmlSmokeTranscriptSourceFailure = "Could not select two Direct Colab stages for preflight worker-card smoke"
                    return -1
                }
                var directRouteState = qmlSmokeAutomaticRouteState()
                if (directRouteState.indexOf("isolator=Direct Colab/") < 0
                        || directRouteState.indexOf("translate=Direct Colab/") < 0
                        || directRouteState.indexOf("workers=2") < 0) {
                    qmlSmokeTranscriptSourceFailure = "Direct Colab selection did not update exact route/model/worker state: " + directRouteState
                    return -1
                }
                ApplicationWindow.window.recordQmlSmokeDubbing("dubbingPreflightDirectColabSelection", "apply",
                                                                localRouteState, directRouteState)
                qmlSmokeAutomaticPhase = 51
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
        if (qmlSmokeAutomaticPhase === 51) {
            if ((automaticPreflightDialog.preflight.selectedWorkers || []).length !== 2) {
                qmlSmokeTranscriptSourceFailure = "Direct Colab preflight did not show exactly the two selected worker cards"
                return -1
            }
            var verifiedDirectRouteState = qmlSmokeAutomaticRouteState()
            if (!dubbing.setWorkflowNodeParameters("source-separate", { executionProvider: "local-dev" })
                    || !dubbing.setWorkflowNodeParameters("translate", { executionProvider: "local-dev" })) {
                qmlSmokeTranscriptSourceFailure = "Could not switch Direct Colab smoke stages back to Local"
                return -1
            }
            var restoredLocalRouteState = qmlSmokeAutomaticRouteState()
            if (restoredLocalRouteState.indexOf("isolator=Local/") < 0
                    || restoredLocalRouteState.indexOf("translate=Local/") < 0
                    || restoredLocalRouteState.indexOf("workers=0") < 0) {
                qmlSmokeTranscriptSourceFailure = "Local route did not clear Direct Colab worker state: " + restoredLocalRouteState
                return -1
            }
            ApplicationWindow.window.recordQmlSmokeDubbing("dubbingPreflightLocalRouteRestore", "apply",
                                                            verifiedDirectRouteState, restoredLocalRouteState)
            qmlSmokeAutomaticPhase = 52
            return 0
        }
        if (qmlSmokeAutomaticPhase === 52) {
            if ((automaticPreflightDialog.preflight.selectedWorkers || []).length !== 0) {
                qmlSmokeTranscriptSourceFailure = "Local route still exposed Direct Colab worker cards"
                return -1
            }
                automaticPreflightDialog.qmlSmokeClickNext()
                ApplicationWindow.window.recordQmlSmokeDubbing("dubbingPreflightNextButton", "click",
                                                                "stages", "colab-workers")
                qmlSmokeAutomaticPhase = 6
                return 0
            }
        if (qmlSmokeAutomaticPhase === 5) {
            var stageId = configuredStages[qmlSmokeAutomaticStageIndex]
            if (stageId === "import") {
                if (!automaticPreflightDialog.visible || automaticPreflightDialog.currentPage !== 0) {
                    qmlSmokeTranscriptSourceFailure = "Import/Download Configure did not return to Source & language"
                    return -1
                }
                automaticPreflightDialog.currentPage = 1
            } else {
                if (!automaticPreflightDialog.visible || automaticPreflightDialog.currentPage !== 1) {
                    qmlSmokeTranscriptSourceFailure = "Configure hid or moved Automatic preflight for " + stageId
                    return -1
                }
                if (!automaticPreflightDialog.qmlSmokeStageSetupVisible()) {
                    qmlSmokeTranscriptSourceFailure = "Configure did not open a preflight-owned setup dialog for " + stageId
                    return -1
                }
                automaticPreflightDialog.qmlSmokeDismissStageSetup()
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
        root.followRunningStep = true
        root.reviewStepId = stepId
        dubbing.rerunStep(stepId, root.defaultExportPath())
    }

    function openOcrColabSetup() {
        // A Direct Colab OCR worker can be prepared while a different manual
        // stage runs.  Never mutate the OCR route after Transcribe itself has
        // started, nor after an Automatic workflow has frozen its approval.
        if (!root.ocrSetupEditable()) return
        var selected = (dubbing.transcriptConfiguration || {}).ocrColabModelId || ""
        if (dubbing.colabNotebookForNode("subtitle-ocr", selected) === "")
            selected = dubbing.defaultColabModelForNode("subtitle-ocr")
        if (!dubbing.selectWorkflowColabModel("subtitle-ocr", selected)) return
        dubbingColabSetupDialog.stageIds = ["subtitle-ocr"]
        dubbingColabSetupDialog.open()
    }

    function ocrSetupEditable() {
        return !dubbing.processing
               || (!dubbing.settingsLocked && dubbing.currentStepId !== "transcribe")
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
            id: dubbingWorkflowHeader
            dubbing: root.dubbing
            steps: [
                { stepId: "import", title: qsTr("Import/Download"), iconName: "folder", complete: (root.workflowStage("import") || {}).state === "completed", active: root.stageIdForNode(root.displayedStepId) === "import" },
                { stepId: "normalize", title: qsTr("Normalize"), iconName: "activity", complete: (root.workflowStage("normalize") || {}).state === "completed", active: root.stageIdForNode(root.displayedStepId) === "normalize" },
                { stepId: "isolator", title: qsTr("Isolator"), iconName: "waves", complete: (root.workflowStage("isolator") || {}).state === "completed", active: root.stageIdForNode(root.displayedStepId) === "isolator" },
                { stepId: "transcribe", title: qsTr("Transcribe/STT"), iconName: "mic", complete: (root.workflowStage("transcribe") || {}).state === "completed", active: root.stageIdForNode(root.displayedStepId) === "transcribe" },
                { stepId: "translate", title: qsTr("Translate"), iconName: "translate", complete: (root.workflowStage("translate") || {}).state === "completed", active: root.stageIdForNode(root.displayedStepId) === "translate" },
                { stepId: "subtitle", title: qsTr("Subtitle"), iconName: "edit", complete: (root.workflowStage("subtitle") || {}).state === "completed", active: root.stageIdForNode(root.displayedStepId) === "subtitle" },
                { stepId: "tts", title: qsTr("TTS"), iconName: "volume", complete: (root.workflowStage("tts") || {}).state === "completed", active: root.stageIdForNode(root.displayedStepId) === "tts" },
                { stepId: "alignment", title: qsTr("Alignment"), iconName: "alignment", complete: (root.workflowStage("alignment") || {}).state === "completed", active: root.stageIdForNode(root.displayedStepId) === "alignment" },
                { stepId: "export", title: qsTr("Export/Output"), iconName: "download", complete: (root.workflowStage("export") || {}).state === "completed", active: root.stageIdForNode(root.displayedStepId) === "export" }
            ]
            statusText: root.dubbing.processing
                        ? qsTr("%1 · Working").arg(root.stepTitle(root.dubbing.currentStepId))
                        : (root.dubbing.workflowMode === "step" ? qsTr("Ready for node run") : qsTr("Ready"))
            defaultExportPath: root.defaultExportPath()
            historyOpen: root.isHistoryOpen
            settingsOpen: root.isNodeInspectorOpen
            projectStatusOpen: root.isProjectStatusPanelOpen
            onStepSelected: function(stepId) {
                root.followRunningStep = false
                root.reviewStepId = root.actionNodeForStage(stepId)
                root.isNodeInspectorOpen = true
                root.isAdvancedNodeInspectorOpen = false
            }
            onHistoryToggled: root.isHistoryOpen = !root.isHistoryOpen
            onSettingsToggled: {
                root.isNodeInspectorOpen = !root.isNodeInspectorOpen
                if (!root.isNodeInspectorOpen)
                    root.isAdvancedNodeInspectorOpen = false
            }
            onProjectStatusToggled: projectSetupDialog.openFor(
                                        root.dubbing.workflowMode === "automatic" ? "automatic" : "step", false)
            onGenerateRequested: {
                automaticPreflightDialog.openPreflight()
            }
            onPauseRequested: root.dubbing.pauseAutomaticWorkflow()
            onStopRequested: root.dubbing.cancelProcessing()
            onWorkflowRequested: root.openWorkflowCanvas()
            onColabSetupRequested: {
                dubbingColabSetupDialog.stageIds = []
                dubbingColabSetupDialog.open()
            }
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

        Item {
            id: dubbingWorkspaceScroller
            objectName: "dubbingWorkspaceScroller"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.paddingMedium
            clip: true
            // This deliberately is not a horizontally flicked canvas.  Task
            // shelves and inspectors take real layout width, so opening one
            // pushes the preview instead of covering it or creating an
            // invisible offscreen editor.
            readonly property real contentWidth: width

            RowLayout {
                id: dubbingWorkspaceRow
                anchors.fill: parent
                // Inspection stays available while a worker runs. Individual
                // actions guard the active stage and automatic workflow.
                enabled: true
                spacing: Theme.paddingMedium

            DubbingHistoryPanel {
                id: historyPanel
                dubbing: root.dubbing
                enabled: !root.dubbing.processing
                panelWidth: root.dubbingHistoryPanelWidth
                expanded: root.isHistoryOpen && !root.previewFocusMode
                onClearRequested: clearHistoryDialog.open()
                onDeleteRequested: function(historyId) {
                    root.pendingHistoryDeleteId = historyId
                    deleteHistoryDialog.open()
                }
                onProjectOpened: root.isHistoryOpen = false
                onExpandedChanged: root.isHistoryOpen = expanded
            }

            Rectangle {
                id: dubbingHistoryResizeHandle
                objectName: "dubbingHistoryResizeHandle"
                Layout.preferredWidth: 8
                Layout.fillHeight: true
                radius: 4
                color: historyResizeHover.hovered || historyResizeDrag.active
                       ? Theme.accent : Qt.rgba(Theme.textSecondary.r, Theme.textSecondary.g, Theme.textSecondary.b, 0.28)
                visible: root.isHistoryOpen && !root.previewFocusMode
                ToolTip.visible: historyResizeHover.hovered
                ToolTip.text: qsTr("Drag to resize Dubbing History")
                HoverHandler { id: historyResizeHover; cursorShape: Qt.SizeHorCursor }
                DragHandler {
                    id: historyResizeDrag
                    property int pressWidth: 0
                    target: null
                    xAxis.enabled: true
                    yAxis.enabled: false
                    onActiveChanged: {
                        if (active)
                            pressWidth = root.dubbingHistoryPanelWidth
                    }
                    onTranslationChanged: {
                        if (active)
                            root.dubbingHistoryPanelWidth = root.clampedDubbingPanelWidth(
                                        pressWidth + translation.x, 240, 560)
                    }
                }
            }

            // Task actions live to the left of the canvas.  The card is
            // intentionally created only after choosing a task so a new
            // project opens with an uncluttered central video workspace.
            Panel {
                id: dubbingTaskShelf
                objectName: "dubbingTaskShelf"
                visible: root.isNodeInspectorOpen && !root.previewFocusMode
                Layout.preferredWidth: root.dubbingTaskShelfWidth
                Layout.minimumWidth: 220
                Layout.maximumWidth: 420
                Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.paddingMedium
                    spacing: Theme.paddingSmall
                    RowLayout {
                        Layout.fillWidth: true
                        LineIcon { name: "workflow"; color: Theme.accentLight; Layout.preferredWidth: 17; Layout.preferredHeight: 17 }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("TASK CONTROLS")
                            color: Theme.textSecondary
                            font.pixelSize: 10
                            font.bold: true
                            font.letterSpacing: 1
                        }
                        PrimaryButton {
                            text: qsTr("Hide")
                            iconName: "chevron-left"
                            iconOnly: true
                            quiet: true
                            toolTip: qsTr("Hide task controls and details")
                            onClicked: {
                                root.isNodeInspectorOpen = false
                                root.isAdvancedNodeInspectorOpen = false
                            }
                        }
                    }
                    DubbingNodeSettingsPanel {
                        id: taskShelfNodeSettings
                        dubbing: root.dubbing
                        nodeId: root.displayedStepId
                        node: root.workflowNode(nodeId)
                        nodeTitle: root.stepTitle(nodeId)
                        canRun: root.canRunStep(nodeId)
                        canRerun: root.canRerunStep(nodeId)
                        runReady: root.stepRunReady(nodeId)
                        nextNodeId: root.nextNodeId(nodeId)
                        nextReady: root.nextNodeReady(nodeId)
                        compact: true
                        visible: node !== null
                        onConfigureRequested: nodeModelDialog.openFor(nodeId)
                        onLoadRequested: dubbing.loadWorkflowNodeModel(nodeId)
                        onUnloadRequested: dubbing.unloadWorkflowNodeModel(nodeId)
                        onReloadRequested: dubbing.reloadWorkflowNodeModel(nodeId)
                        onRunRequested: root.runStep(nodeId)
                        onNextRequested: root.runNextNode(nodeId)
                        onFixRequested: translationFixDialog.openForAll()
                    }
                    // Keep the primary STT/OCR source decision next to the
                    // selected task.  Detailed conflict and OCR controls stay
                    // in the right review pane, but this makes the active
                    // transcript mode visible without making the operator hunt
                    // through a second, permanently-open inspector.
                    Rectangle {
                        id: dubbingTranscriptSourcePanel
                        objectName: "dubbingTranscriptSourcePanel"
                        visible: root.displayedStepId === "transcribe"
                        Layout.fillWidth: true
                        implicitHeight: transcriptShelfSourceLayout.implicitHeight + Theme.paddingMedium * 2
                        radius: Theme.radiusSmall
                        color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.08)
                        border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.28)
                        border.width: 1

                        ColumnLayout {
                            id: transcriptShelfSourceLayout
                            anchors.fill: parent
                            anchors.margins: Theme.paddingMedium
                            spacing: Theme.paddingSmall

                            Text {
                                text: qsTr("Transcript source")
                                color: Theme.textPrimary
                                font.bold: true
                            }
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
                                enabled: root.ocrSetupEditable()
                                onActivated: function(index) {
                                    dubbing.setWorkflowNodeParameters("transcribe", {
                                        transcriptSource: model[index].id
                                    })
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: (dubbing.transcriptConfiguration.transcriptSource || "stt") === "ocr"
                                      ? qsTr("OCR uses the selected Subtitle OCR route and ROI.")
                                      : (dubbing.transcriptConfiguration.transcriptSource || "stt") === "stt+ocr"
                                        ? qsTr("Both sources run; conflicts stay available for review.")
                                        : qsTr("Uses speech-to-text only.")
                                color: Theme.textSecondary
                                font.pixelSize: 10
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                    PrimaryButton {
                        Layout.fillWidth: true
                        visible: taskShelfNodeSettings.node
                                 && taskShelfNodeSettings.node.configurable === true
                        text: root.isAdvancedNodeInspectorOpen
                              ? qsTr("Show task result") : qsTr("Advanced task settings")
                        iconName: root.isAdvancedNodeInspectorOpen ? "file" : "sliders"
                        quiet: true
                        onClicked: root.isAdvancedNodeInspectorOpen = !root.isAdvancedNodeInspectorOpen
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.isAdvancedNodeInspectorOpen
                              ? qsTr("The right panel shows detailed parameters for this task.")
                              : qsTr("The right panel shows this task's output, review, and next action.")
                        color: Theme.textSecondary
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }
                    Item { Layout.fillHeight: true }
                }
            }

            Rectangle {
                id: dubbingTaskShelfResizeHandle
                objectName: "dubbingTaskShelfResizeHandle"
                Layout.preferredWidth: 8
                Layout.fillHeight: true
                radius: 4
                color: taskShelfResizeHover.hovered || taskShelfResizeDrag.active
                       ? Theme.accent : Qt.rgba(Theme.textSecondary.r, Theme.textSecondary.g, Theme.textSecondary.b, 0.28)
                visible: dubbingTaskShelf.visible
                ToolTip.visible: visible && taskShelfResizeHover.hovered
                ToolTip.text: qsTr("Drag to resize task controls")
                HoverHandler { id: taskShelfResizeHover; cursorShape: Qt.SizeHorCursor }
                DragHandler {
                    id: taskShelfResizeDrag
                    property int pressWidth: 0
                    target: null
                    xAxis.enabled: true
                    yAxis.enabled: false
                    onActiveChanged: {
                        if (active)
                            pressWidth = root.dubbingTaskShelfWidth
                    }
                    onTranslationChanged: {
                        if (active)
                            root.dubbingTaskShelfWidth = root.clampedDubbingPanelWidth(
                                        pressWidth + translation.x, 220, 420)
                    }
                }
            }

            ColumnLayout {
                id: dubbingPreviewWorkspace
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 540
                Layout.preferredWidth: root.dubbingPreviewPanelWidth
                spacing: Theme.paddingMedium
                clip: true

                DubbingSourceMediaPanel {
                    id: sourceMediaPanel
                    dubbing: root.dubbing
                    selectedSegment: root.selectedSegment
                    previewFocusMode: root.previewFocusMode
                    onBrowseRequested: mediaFileDialog.open()
                    onSubtitleEditorRequested: subtitleEditorDialog.open()
                    onLinkImportRequested: function(url) { root.dubbing.importMediaFromLink(url) }
                    onMediaQueueRequested: function(urls) { root.dubbing.enqueueMediaLinks(urls) }
                    onCancelLinkImportRequested: root.dubbing.cancelMediaLinkImport()
                    onSegmentSelected: root.selectedSegment = index
                    onSelectedSegmentChanged: root.selectedSegment = selectedSegment
                    onPreviewFocusRequested: function(focused) {
                        root.previewFocusMode = focused
                    }
                }
            }

            Rectangle {
                id: dubbingWorkspaceResizeHandle
                objectName: "dubbingWorkspaceResizeHandle"
                Layout.preferredWidth: 8
                Layout.fillHeight: true
                radius: 4
                color: workspaceResizeHover.hovered || workspaceResizeDrag.active
                       ? Theme.accent : Qt.rgba(Theme.textSecondary.r, Theme.textSecondary.g, Theme.textSecondary.b, 0.28)
                visible: !root.previewFocusMode
                ToolTip.visible: visible && workspaceResizeHover.hovered
                ToolTip.text: qsTr("Drag to resize Dubbing Preview")
                HoverHandler { id: workspaceResizeHover; cursorShape: Qt.SizeHorCursor }
                DragHandler {
                    id: workspaceResizeDrag
                    property int pressWidth: 0
                    target: null
                    xAxis.enabled: true
                    yAxis.enabled: false
                    onActiveChanged: {
                        if (active)
                            pressWidth = root.dubbingPreviewPanelWidth
                    }
                    onTranslationChanged: {
                        if (active)
                            root.dubbingPreviewPanelWidth = root.clampedDubbingPanelWidth(
                                        pressWidth + translation.x, 540, 1280)
                    }
                }
            }

            Panel {
                id: dubbingStepReviewPanel
                objectName: "dubbingStepReviewPanel"
                // This is the task result/review region. Parameter editing is
                // explicitly switched into DubbingNodeInspector below so two
                // right-side panels never overlap each other.
                visible: !root.previewFocusMode && root.isNodeInspectorOpen
                         && !root.isAdvancedNodeInspectorOpen
                Layout.fillWidth: true; Layout.fillHeight: true
                Layout.minimumWidth: 280
                Layout.preferredWidth: root.dubbingStepPanelWidth
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
                        visible: false
                        onConfigureRequested: nodeModelDialog.openFor(nodeId)
                        onLoadRequested: dubbing.loadWorkflowNodeModel(nodeId)
                        onUnloadRequested: dubbing.unloadWorkflowNodeModel(nodeId)
                        onReloadRequested: dubbing.reloadWorkflowNodeModel(nodeId)
                        onRunRequested: root.runStep(nodeId)
                        onNextRequested: root.runNextNode(nodeId)
                        onFixRequested: translationFixDialog.openForAll()
                    }
                    Rectangle {
                        id: dubbingTranscriptSourceDetailsPanel
                        objectName: "dubbingTranscriptSourceDetailsPanel"
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
                                    id: dubbingTranscriptSourceModeDetails
                                    objectName: "dubbingTranscriptSourceModeDetails"
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
                                    enabled: root.ocrSetupEditable()
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
                                    enabled: root.ocrSetupEditable()
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
                            RowLayout {
                                Layout.fillWidth: true
                                visible: (dubbing.transcriptConfiguration.transcriptSource || "stt") !== "stt"
                                spacing: Theme.paddingSmall
                                Text {
                                    text: qsTr("OCR compute")
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontSmall
                                }
                                ComboBox {
                                    id: dubbingOcrRouteMode
                                    Layout.preferredWidth: 180
                                    textRole: "label"
                                    model: [
                                        { id: "local-cpu", label: qsTr("Local CPU") },
                                        { id: "colab-gpu", label: qsTr("Colab GPU") }
                                    ]
                                    currentIndex: (dubbing.transcriptConfiguration.ocrExecutionRoute || "local-cpu") === "colab-gpu" ? 1 : 0
                                    enabled: root.ocrSetupEditable()
                                    onActivated: function(index) {
                                        if (model[index].id === "colab-gpu")
                                            root.openOcrColabSetup()
                                        else
                                            dubbing.setWorkflowNodeParameters("transcribe", {
                                                "ocrExecutionRoute": "local-cpu"
                                            })
                                    }
                                }
                                PrimaryButton {
                                    text: (dubbing.transcriptConfiguration.ocrExecutionRoute || "local-cpu") === "colab-gpu"
                                          ? qsTr("Configure / check OCR Colab") : qsTr("Set up OCR Colab GPU")
                                    iconName: "cloud"
                                    quiet: true
                                    enabled: root.ocrSetupEditable()
                                    toolTip: qsTr("Select the exact Subtitle OCR GPU notebook, then connect and verify its temporary worker")
                                    onClicked: root.openOcrColabSetup()
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: !root.ocrSetupEditable()
                                          ? qsTr("OCR route is locked while Transcribe runs.")
                                          : (dubbing.processing
                                             ? qsTr("You may prepare OCR while this different manual task runs.")
                                             : qsTr("Choose and verify this before starting Transcribe."))
                                    color: !root.ocrSetupEditable() ? Theme.warning : Theme.textSecondary
                                    font.pixelSize: 10
                                    wrapMode: Text.WordWrap
                                }
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
                        visible: false
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
                        Layout.preferredHeight: visible ? transcriptReviewActions.implicitHeight + Theme.paddingLarge * 2 : 0
                        ColumnLayout {
                            id: transcriptReviewActions
                            anchors.fill: parent
                            anchors.margins: Theme.paddingLarge
                            spacing: Theme.paddingSmall
                            Text {
                                text: qsTr("SOURCE TRANSCRIPT REVIEW")
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontLarge
                                font.bold: true
                            }
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Review source-language timed text from STT and/or Subtitle OCR before translation. This is not the target-language subtitle output step.")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSmall
                                wrapMode: Text.WordWrap
                            }
                            Text {
                                Layout.fillWidth: true
                                text: dubbing.segments.length > 0
                                      ? qsTr("%1 timed source segments are available for review.").arg(dubbing.segments.length)
                                      : qsTr("Run Transcribe/STT before opening source transcript review.")
                                color: dubbing.segments.length > 0 ? Theme.success : Theme.warning
                                font.pixelSize: Theme.fontSmall
                                wrapMode: Text.WordWrap
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                PrimaryButton {
                                    text: qsTr("Open transcript editor")
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
                    Panel {
                        visible: root.displayedStepId === "review-translation"
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? translatedSubtitleActions.implicitHeight + Theme.paddingLarge * 2 : 0
                        ColumnLayout {
                            id: translatedSubtitleActions
                            anchors.fill: parent
                            anchors.margins: Theme.paddingLarge
                            spacing: Theme.paddingSmall
                            Text {
                                text: qsTr("TARGET-LANGUAGE SUBTITLES")
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontLarge
                                font.bold: true
                            }
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Review the translated target text before TTS. Export uses these target-language segments for subtitle files and burn-in.")
                                color: Theme.textSecondary
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
                                Item { Layout.fillWidth: true }
                                PrimaryButton {
                                    text: qsTr("Continue to TTS")
                                    iconName: "chevron-right"
                                    enabled: !dubbing.processing && root.stepComplete("review-translation")
                                    onClicked: root.runNextNode("review-translation")
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
                    visible: !root.previewFocusMode && root.isNodeInspectorOpen
                             && root.isAdvancedNodeInspectorOpen
                             && node && node.configurable === true
                    onCloseRequested: root.isAdvancedNodeInspectorOpen = false
                    onRewriteSetupRequested: qualityDialog.openForMode("custom")
                }
            }
        }

        // The timeline is a top-level workbench region rather than a child of
        // the video column.  It therefore remains centered and spans the
        // complete Dubbing workspace, like an editor timeline.
        Item {
            id: dubbingTimelineResizeHandle
            objectName: "dubbingTimelineResizeHandle"
            Layout.fillWidth: true
            Layout.leftMargin: Theme.paddingMedium
            Layout.rightMargin: Theme.paddingMedium
            Layout.preferredHeight: visible ? 18 : 0
            visible: !root.previewFocusMode
            z: 10

            Rectangle {
                width: 84
                height: 4
                radius: 2
                anchors.centerIn: parent
                color: timelineResizeHover.hovered || timelineResizeDrag.active
                       ? Theme.accent : Qt.rgba(Theme.textSecondary.r, Theme.textSecondary.g, Theme.textSecondary.b, 0.55)
            }

            ToolTip.visible: timelineResizeHover.hovered
            ToolTip.text: qsTr("Drag to resize Dubbing timeline")

            HoverHandler { id: timelineResizeHover; cursorShape: Qt.SizeVerCursor }
            DragHandler {
                id: timelineResizeDrag
                property int pressHeight: 0
                target: null
                xAxis.enabled: false
                yAxis.enabled: true
                onActiveChanged: {
                    if (active)
                        pressHeight = root.dubbingTimelinePanelHeight
                }
                onTranslationChanged: {
                    if (active) {
                        root.dubbingTimelinePanelHeight = root.clampedDubbingTimelineHeight(
                                    pressHeight - translation.y)
                    }
                }
            }
        }

        Panel {
            id: dubbingTimelinePanel
            objectName: "dubbingTimelinePanel"
            visible: !root.previewFocusMode
            Layout.fillWidth: true
            Layout.leftMargin: Theme.paddingMedium
            Layout.rightMargin: Theme.paddingMedium
            Layout.minimumHeight: visible ? 160 : 0
            Layout.maximumHeight: visible ? 520 : 0
            Layout.preferredHeight: visible ? root.dubbingTimelinePanelHeight : 0

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.paddingMedium
                spacing: Theme.paddingSmall

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: qsTr("TIMELINE")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        font.bold: true
                        font.letterSpacing: 1.1
                        Layout.fillWidth: true
                    }
                    Text {
                        text: qsTr("%1 segments").arg(dubbing.segments.length)
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                    }
                }

                WaveformView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    framed: true
                    showPlaceholder: true
                    placeholderText: dubbing.sourceMediaPath.length > 0
                                     ? qsTr("Waveform preview becomes available after audio analysis")
                                     : qsTr("Import media to begin")
                }

                RowLayout {
                    Layout.fillWidth: true
                    Text { text: qsTr("00:00"); color: Theme.textSecondary; font.pixelSize: 10 }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: dubbing.processing
                              ? (dubbing.progressAvailable ? qsTr("Processing %1%").arg(dubbing.progress) : qsTr("Processing"))
                              : qsTr("Edit transcript in the task panel or inspector")
                        color: Theme.textSecondary
                        font.pixelSize: 10
                    }
                }
            }
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
            projectSetupDialog.openFor("automatic", true)
        }
        onStepByStepRequested: {
            if (!root.dubbing.chooseDubbingEntryMode("step")) return
            close()
            projectSetupDialog.openFor("step", true)
        }
        onLeaveDubbingRequested: {
            close()
            AppController.workflows.openStudioRoute("welcome")
        }
    }

    DubbingProjectSetupDialog {
        id: projectSetupDialog
        dubbing: root.dubbing
        languageCatalog: root.languageCatalog
        onConfigurationAccepted: function(mode, startAfterApply) {
            if (!startAfterApply)
                return
            if (mode === "automatic")
                automaticPreflightDialog.openPreflight()
            else
                root.dubbing.startStepByStep()
        }
        onConfigurationCancelled: function(startAfterApply) {
            if (startAfterApply)
                dubbingEntryGate.openGate()
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
        onAdaptiveLlmSetupRequested: qualityDialog.openForMode("adaptive")
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
        description: qsTr("Review the eight production-backed stages: import, normalize, isolator, transcribe, alignment/subtitle, translate, TTS, and export/output.")
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
