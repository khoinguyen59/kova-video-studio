import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia
import "../components/shared"
import "../components/base"
import LAStudio

Page {
    id: root

    readonly property var ocr: AppController.subtitleOcr
    readonly property var runtime: AppController.subtitleOcrRuntime
    readonly property bool wideLayout: width >= 1180
    readonly property bool usingColabRoute: ocr.executionRoute === "colab-gpu"
    readonly property bool usingPaddleLocalEngine: !usingColabRoute
                                                   && ocr.localEngineId === "paddleocr-ppocrv6-tiny"
    // PaddleOCR has a deliberately narrow bundled language profile for this
    // candidate. Keep the language selector visible for the Tesseract/Colab
    // alternatives, but never claim that an unbundled local Paddle language
    // can run.
    readonly property bool selectedLanguageReady: usingPaddleLocalEngine ? ocr.localRouteReady
                                                 : (runtime.runtimeSource === "environment"
                                                    ? ocr.ocrLanguage !== ""
                                                    : runtime.isLanguageInstalled(ocr.ocrLanguage))
    property real previewPositionMs: player.position
    readonly property real displayedWidth: {
        if (ocr.sourceWidth <= 0 || ocr.sourceHeight <= 0 || videoCanvas.width <= 0 || videoCanvas.height <= 0)
            return 0
        return Math.min(videoCanvas.width, videoCanvas.height * ocr.sourceWidth / ocr.sourceHeight)
    }
    readonly property real displayedHeight: {
        if (ocr.sourceWidth <= 0 || ocr.sourceHeight <= 0 || videoCanvas.width <= 0 || videoCanvas.height <= 0)
            return 0
        return Math.min(videoCanvas.height, videoCanvas.width * ocr.sourceHeight / ocr.sourceWidth)
    }
    readonly property real displayedX: (videoCanvas.width - displayedWidth) / 2
    readonly property real displayedY: (videoCanvas.height - displayedHeight) / 2
    readonly property real minimumRoiPixels: 16
    // Pointer movement only updates this local draft. The controller is
    // notified exactly once when a move/resize gesture ends, so it cannot
    // trigger project writes, video seeks, decoding or OCR for every pixel.
    property real draftRoiX: ocr.roiX
    property real draftRoiY: ocr.roiY
    property real draftRoiWidth: ocr.roiWidth
    property real draftRoiHeight: ocr.roiHeight
    property bool roiDragActive: false

    title: qsTr("Subtitle OCR")
    background: Rectangle { color: Theme.background }

    function loadVideo(path) {
        if (path && path.toString().length > 0)
            ocr.loadSource(path)
    }

    function clamp(value, low, high) { return Math.max(low, Math.min(high, value)) }

    function resizedRoi(roi, mode, point, geometry) {
        var left = geometry.x + roi.x * geometry.width
        var right = left + roi.width * geometry.width
        var top = geometry.y + roi.y * geometry.height
        var bottom = top + roi.height * geometry.height
        var x = clamp(point.x, geometry.x, geometry.x + geometry.width)
        var y = clamp(point.y, geometry.y, geometry.y + geometry.height)
        if (mode.indexOf("l") !== -1) left = Math.min(x, right - minimumRoiPixels)
        if (mode.indexOf("r") !== -1) right = Math.max(x, left + minimumRoiPixels)
        if (mode.indexOf("t") !== -1) top = Math.min(y, bottom - minimumRoiPixels)
        if (mode.indexOf("b") !== -1) bottom = Math.max(y, top + minimumRoiPixels)
        return { x: (left - geometry.x) / geometry.width,
                 y: (top - geometry.y) / geometry.height,
                 width: (right - left) / geometry.width,
                 height: (bottom - top) / geometry.height }
    }

    function movedRoi(roi, point, grabX, grabY, geometry) {
        var pixelWidth = roi.width * geometry.width
        var pixelHeight = roi.height * geometry.height
        var left = clamp(point.x - grabX, geometry.x, geometry.x + geometry.width - pixelWidth)
        var top = clamp(point.y - grabY, geometry.y, geometry.y + geometry.height - pixelHeight)
        return { x: (left - geometry.x) / geometry.width,
                 y: (top - geometry.y) / geometry.height,
                 width: roi.width, height: roi.height }
    }

    function syncDraftRoi() {
        if (roiDragActive) return
        draftRoiX = ocr.roiX
        draftRoiY = ocr.roiY
        draftRoiWidth = ocr.roiWidth
        draftRoiHeight = ocr.roiHeight
    }

    function beginRoiDrag() {
        roiDragActive = true
        subtitleControlsAutoHide.interactionActive = true
    }

    function commitOverlayPosition() {
        if (displayedWidth <= 0 || displayedHeight <= 0) return
        var accepted = ocr.setRoi(draftRoiX, draftRoiY, draftRoiWidth, draftRoiHeight)
        if (!accepted) syncDraftRoi()
        roiDragActive = false
        subtitleControlsAutoHide.interactionActive = false
        subtitleControlsAutoHide.noteInteraction()
    }

    function resizeRoi(mode, point) {
        if (displayedWidth <= 0 || displayedHeight <= 0) return
        var next = resizedRoi({ x: draftRoiX, y: draftRoiY,
                                 width: draftRoiWidth, height: draftRoiHeight },
                              mode, point,
                              { x: displayedX, y: displayedY,
                                width: displayedWidth, height: displayedHeight })
        draftRoiX = next.x
        draftRoiY = next.y
        draftRoiWidth = next.width
        draftRoiHeight = next.height
    }

    function moveRoi(point, grabX, grabY) {
        if (displayedWidth <= 0 || displayedHeight <= 0) return
        var next = movedRoi({ x: draftRoiX, y: draftRoiY,
                               width: draftRoiWidth, height: draftRoiHeight },
                            point, grabX, grabY,
                            { x: displayedX, y: displayedY,
                              width: displayedWidth, height: displayedHeight })
        draftRoiX = next.x
        draftRoiY = next.y
    }

    function itemRectInContent(item) {
        var point = item.mapToItem(pageContent, 0, 0)
        return { x: point.x, y: point.y, width: item.width, height: item.height }
    }

    function rectanglesOverlap(first, second) {
        var a = itemRectInContent(first)
        var b = itemRectInContent(second)
        return a.x < b.x + b.width - 1 && a.x + a.width > b.x + 1
            && a.y < b.y + b.height - 1 && a.y + a.height > b.y + 1
    }

    // A child may be below the current viewport, but it must remain within the
    // page width and inside the ScrollView's reachable content. Checking this
    // explicitly catches the layout regressions where a card was present but
    // its controls were clipped below a fixed-height parent.
    function itemIsScrollReachable(item) {
        if (!item || !item.visible || item.width <= 0 || item.height <= 0)
            return false
        var rect = itemRectInContent(item)
        return rect.x >= -1 && rect.y >= -1
                && rect.x + rect.width <= pageContent.width + 1
                && subtitleOcrScroll.contentHeight >= rect.y + rect.height - 1
    }

    function qmlSmokeMediaControlsCheck() {
        return subtitleControlsAutoHide.qmlSmokeStateCheck()
                && subtitleControlsAutoHide.delayMs === 2000
    }

    function qmlSmokeRoiInteractionCheck() {
        // Exercise the same local geometry helpers used by pointer handlers.
        // The three logical surfaces cover the route-smoke sizes and a HiDPI
        // mapping (physical pixels / device scale) without sending a controller
        // call while the pointer is moving.
        var surfaces = [
            { physicalWidth: 1024, physicalHeight: 720, scale: 1.0 },
            { physicalWidth: 1600, physicalHeight: 1000, scale: 1.25 },
            { physicalWidth: 2400, physicalHeight: 1350, scale: 1.5 }
        ]
        for (var i = 0; i < surfaces.length; ++i) {
            var surface = surfaces[i]
            var logicalWidth = surface.physicalWidth / surface.scale
            var logicalHeight = surface.physicalHeight / surface.scale
            var lower = { x: 0.10, y: 0.72, width: 0.80, height: 0.22 }
            var moved = movedRoi(lower,
                                 { x: logicalWidth * 2, y: logicalHeight * 2 },
                                 logicalWidth * 0.20, logicalHeight * 0.08,
                                 { x: 0, y: 0, width: logicalWidth, height: logicalHeight })
            if (moved.x < 0 || moved.y < 0 || moved.x + moved.width > 1
                    || moved.y + moved.height > 1) return false
            var resized = resizedRoi(moved, "br",
                                     { x: logicalWidth * 2, y: logicalHeight * 2 },
                                     { x: 0, y: 0, width: logicalWidth, height: logicalHeight })
            if (resized.x < 0 || resized.y < 0 || resized.x + resized.width > 1
                    || resized.y + resized.height > 1
                    || resized.width * logicalWidth < minimumRoiPixels
                    || resized.height * logicalHeight < minimumRoiPixels) return false
        }
        return true
    }

    // Used by the offscreen QML route smoke. It verifies the responsive card
    // contract, child reachability and disabled-runtime behavior rather than
    // assuming a fixed window height or z-order.
    function qmlSmokeFailure(reason) {
        console.warn("Subtitle OCR QML smoke failure: " + reason)
        return false
    }

    function qmlSmokeLayoutCheck() {
        var cards = [sourceMediaCard, previewCard, runtimeCard, settingsCard, transcriptCard]
        for (var i = 0; i < cards.length; ++i) {
            var card = cards[i]
            if (!itemIsScrollReachable(card)) return qmlSmokeFailure("unreachable-card-" + i)
            for (var j = i + 1; j < cards.length; ++j)
                if (rectanglesOverlap(card, cards[j])) return qmlSmokeFailure("overlapping-cards-" + i + "-" + j)
        }
        // A blank link field correctly keeps Import link disabled; the smoke
        // contract is that the field itself remains usable while runtime is
        // missing, not that an empty URL can be submitted.
        if (!chooseVideoButton.visible || !chooseVideoButton.enabled
                || !sourceDropZone.visible || !sourceLinkInput.visible || !sourceLinkInput.enabled
                || !importLinkButton.visible || !sourceDropArea.enabled
                || chooseVideoButton.width < chooseVideoButton.implicitWidth
                || chooseVideoButton.height < chooseVideoButton.implicitHeight
                || importLinkButton.width < importLinkButton.implicitWidth
                || sourceLinkInput.width <= 0 || sourceDropZone.height <= 0)
            return qmlSmokeFailure("source-controls")
        var requiredControls = [sourceDropZone, chooseVideoButton, sourceLinkInput,
                                importLinkButton, videoCanvas, languageSelector,
                                intervalInput, confidenceInput, runOcrButton, segmentView]
        // The local Paddle candidate intentionally does not show Tesseract's
        // language-pack installer. Its visible language selector is checked
        // above; the Tesseract list is checked whenever that explicit engine
        // is selected rather than treating a hidden control as reachable.
        if (!usingPaddleLocalEngine) requiredControls.splice(5, 0, languagePackScroll)
        for (var k = 0; k < requiredControls.length; ++k)
            if (!itemIsScrollReachable(requiredControls[k])) return qmlSmokeFailure("unreachable-control-" + k)
        if (languageSelector.width < languageSelector.implicitWidth
                || runOcrButton.width < runOcrButton.implicitWidth)
            return qmlSmokeFailure("compressed-controls")
        var savedSourceLink = sourceLinkInput.text
        sourceLinkInput.text = "https://example.invalid/subtitle-ocr-fixture.mp4"
        var validLinkEnablesImport = importLinkButton.enabled
        // The offscreen Qt platform has no active native window, so it cannot
        // truthfully grant activeFocus even when QML has moved keyboard focus
        // to this control.  Local `focus` is the portable QML interaction
        // contract; an interactive desktop additionally exposes activeFocus.
        sourceLinkInput.forceActiveFocus(Qt.OtherFocusReason)
        var sourceLinkCanReceiveFocus = sourceLinkInput.focus || sourceLinkInput.activeFocus
        sourceLinkInput.text = savedSourceLink
        if (!validLinkEnablesImport || !sourceLinkCanReceiveFocus)
            return qmlSmokeFailure("link-input: import-enabled=" + validLinkEnablesImport
                                   + ", input-enabled=" + sourceLinkInput.enabled
                                   + ", input-visible=" + sourceLinkInput.visible
                                   + ", focus=" + sourceLinkInput.focus
                                   + ", active-focus=" + sourceLinkInput.activeFocus
                                   + ", processing=" + ocr.processing
                                   + ", importing=" + ocr.sourceImporting)
        // A missing runtime only blocks execution/install. Selecting the
        // desired language remains useful before the runtime is installed.
        if (!ocr.runtimeAvailable && (!languageSelector.enabled || runOcrButton.enabled))
            return qmlSmokeFailure("missing-runtime-state")
        if (!qmlSmokeMediaControlsCheck()) return qmlSmokeFailure("media-controls")
        if (!qmlSmokeRoiInteractionCheck()) return qmlSmokeFailure("roi-interaction")
        var transcriptRect = itemRectInContent(transcriptCard)
        if (subtitleOcrScroll.contentHeight < transcriptRect.y + transcriptRect.height - 1)
            return qmlSmokeFailure("transcript-content-height")
        return !rectanglesOverlap(sourceMediaCard, previewCard)
            || qmlSmokeFailure("source-preview-overlap")
    }

    component RoiHandle: Rectangle {
        required property string mode
        width: 12
        height: 12
        radius: 6
        color: Theme.primary
        border.color: Theme.textPrimary
        border.width: 1
        z: 3
        MouseArea {
            anchors.fill: parent
            cursorShape: parent.mode === "l" || parent.mode === "r" ? Qt.SizeHorCursor
                       : parent.mode === "t" || parent.mode === "b" ? Qt.SizeVerCursor
                       : Qt.SizeAllCursor
            onPositionChanged: function(mouse) {
                if (!pressed) return
                root.resizeRoi(parent.mode, parent.mapToItem(videoCanvas, mouse.x, mouse.y))
            }
            onPressed: root.beginRoiDrag()
            onReleased: root.commitOverlayPosition()
            onCanceled: root.commitOverlayPosition()
        }
    }

    FileDialog {
        id: videoFileDialog
        title: qsTr("Choose video for Subtitle OCR")
        nameFilters: [qsTr("Video files (*.mp4 *.mkv *.mov *.webm *.avi)"), qsTr("All files (*)")]
        onAccepted: root.loadVideo(selectedFile)
    }
    FileDialog {
        id: openProjectDialog
        title: qsTr("Open Subtitle OCR project")
        nameFilters: [qsTr("Subtitle OCR project (*.laocr.json)"), qsTr("All files (*)")]
        onAccepted: ocr.openProject(selectedFile)
    }
    FileDialog {
        id: saveProjectDialog
        title: qsTr("Save Subtitle OCR project")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "laocr.json"
        nameFilters: [qsTr("Subtitle OCR project (*.laocr.json)")]
        onAccepted: ocr.saveProject(selectedFile)
    }
    FileDialog {
        id: saveSrtDialog
        title: qsTr("Export OCR subtitles")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "srt"
        nameFilters: [qsTr("SubRip subtitles (*.srt)")]
        onAccepted: ocr.exportSrt(selectedFile)
    }
    FileDialog {
        id: saveTextDialog
        title: qsTr("Export OCR transcript")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "txt"
        nameFilters: [qsTr("Text files (*.txt)")]
        onAccepted: ocr.exportText(selectedFile)
    }
    Dialog {
        id: runtimeDiagnosticsDialog
        objectName: "subtitleOcrRuntimeDiagnosticsDialog"
        title: qsTr("OCR runtime diagnostics")
        modal: true
        width: Math.min(root.width - Theme.paddingLarge * 2, 760)
        height: Math.min(root.height - Theme.paddingLarge * 2, 460)
        standardButtons: Dialog.Close
        TextArea {
            anchors.fill: parent
            anchors.margins: Theme.paddingMedium
            readOnly: true
            selectByMouse: true
            wrapMode: TextArea.WrapAnywhere
            text: runtime.diagnostics === "" ? qsTr("No OCR runtime diagnostics have been captured yet.")
                                                : runtime.diagnostics
            color: Theme.textPrimary
            background: Rectangle { color: Theme.background; radius: Theme.radiusSmall }
        }
    }

    Dialog {
        id: subtitleOcrDiagnosticsDialog
        objectName: "subtitleOcrDiagnosticsDialog"
        parent: Overlay.overlay
        modal: true
        title: qsTr("Subtitle OCR diagnostics")
        width: Math.min(root.width - Theme.paddingLarge * 2, 760)
        height: Math.min(root.height - Theme.paddingLarge * 2, 460)
        standardButtons: Dialog.Close
        TextArea {
            anchors.fill: parent
            anchors.margins: Theme.paddingMedium
            readOnly: true
            selectByMouse: true
            wrapMode: TextArea.WrapAnywhere
            text: ocr.diagnostics === "" ? qsTr("No Subtitle OCR diagnostics have been captured yet.")
                                        : ocr.diagnostics
            color: Theme.textPrimary
            background: Rectangle { color: Theme.background; radius: Theme.radiusSmall }
        }
    }

    Dialog {
        id: subtitleOcrColabDialog
        objectName: "subtitleOcrColabDialog"
        parent: Overlay.overlay
        modal: true
        title: qsTr("Colab GPU for Subtitle OCR")
        width: Math.min(560, Overlay.overlay.width - Theme.paddingXL * 2)
        anchors.centerIn: parent
        standardButtons: Dialog.Close
        onOpened: {
            subtitleOcrColabUrl.text = AppController.colabSubtitleOcrSession.workerUrl
            subtitleOcrColabToken.text = ""
        }
        contentItem: ColumnLayout {
            spacing: Theme.paddingSmall
            Text {
                Layout.fillWidth: true
                text: qsTr("Direct Colab is independent of API Gateway. The source video stays on this computer; only the sampled, cropped PNG subtitle frames are sent to the temporary CUDA worker.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Exact model: PP-OCRv5 Multilingual 3.1 · Apache-2.0 · CUDA. The worker selects the official language profile for Vietnamese, Chinese, Japanese or Korean.")
                color: Theme.textPrimary
                wrapMode: Text.WordWrap
            }
            ColabNotebookLink { notebookFile: ocr.colabNotebookFile }
            Text { text: qsTr("Worker URL"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
            TextField {
                id: subtitleOcrColabUrl
                Layout.fillWidth: true
                placeholderText: qsTr("https://…trycloudflare.com")
                selectByMouse: true
            }
            Text { text: qsTr("Temporary session token"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
            TextField {
                id: subtitleOcrColabToken
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: qsTr("Token printed by this exact notebook")
                selectByMouse: true
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: qsTr("Connect and check Colab")
                    enabled: !AppController.colabSubtitleOcrSession.checking
                    onClicked: {
                        if (AppController.colabSubtitleOcrSession.connectTemporaryWorker(
                                subtitleOcrColabUrl.text.trim(), subtitleOcrColabToken.text,
                                "subtitle-ocr", ocr.colabModelId))
                            subtitleOcrColabToken.text = ""
                    }
                }
                Button {
                    text: qsTr("Check Colab")
                    enabled: AppController.colabSubtitleOcrSession.active
                             && !AppController.colabSubtitleOcrSession.checking
                    onClicked: AppController.colabSubtitleOcrSession.checkConnection()
                }
                Button {
                    text: qsTr("Disconnect")
                    enabled: AppController.colabSubtitleOcrSession.active
                    onClicked: AppController.colabSubtitleOcrSession.disconnectTemporaryWorker()
                }
            }
            ColabSessionStatus { session: AppController.colabSubtitleOcrSession }
        }
    }

    MediaPlayer {
        id: player
        source: ocr.sourceUrl
        audioOutput: AudioOutput {}
        videoOutput: videoPreviewOutput
    }

    MediaControlsAutoHide {
        id: subtitleControlsAutoHide
        playing: player.playbackState === MediaPlayer.PlayingState
        controlsFocused: subtitlePlayButton.activeFocus || subtitleSeekSlider.activeFocus
    }

    Connections {
        target: ocr
        function onRoiChanged() { root.syncDraftRoi() }
    }

    Component.onCompleted: syncDraftRoi()

    ScrollView {
        id: subtitleOcrScroll
        objectName: "subtitleOcrScroll"
        anchors.fill: parent
        anchors.margins: Theme.paddingLarge
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            id: pageContent
            width: subtitleOcrScroll.availableWidth
            spacing: Theme.paddingLarge

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.paddingMedium
                Text {
                    text: qsTr("Subtitle OCR")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontXLarge
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Extract burned-in image subtitles from video. Source selection and ROI work before the OCR runtime is installed.")
                    color: Theme.textSecondary
                    wrapMode: Text.WordWrap
                }
                Flow {
                    spacing: Theme.paddingSmall
                    Button { text: qsTr("Open project"); enabled: !ocr.processing; onClicked: openProjectDialog.open() }
                    Button { text: qsTr("Save project"); enabled: !ocr.processing; onClicked: ocr.projectPath === "" ? saveProjectDialog.open() : ocr.saveProject() }
                }
            }

            GridLayout {
                id: cardGrid
                objectName: "subtitleOcrCardGrid"
                Layout.fillWidth: true
                columns: root.wideLayout ? 2 : 1
                rowSpacing: Theme.paddingLarge
                columnSpacing: Theme.paddingLarge

                Rectangle {
                    id: sourceMediaCard
                    objectName: "subtitleOcrSourceMediaCard"
                    Layout.fillWidth: true
                    Layout.columnSpan: cardGrid.columns
                    implicitHeight: sourceMediaLayout.implicitHeight + Theme.paddingLarge * 2
                    radius: Theme.radiusMedium
                    color: Theme.surface
                    border.color: Theme.border
                    border.width: 1

                    ColumnLayout {
                        id: sourceMediaLayout
                        anchors.fill: parent
                        anchors.margins: Theme.paddingLarge
                        spacing: Theme.paddingMedium
                        Text { text: qsTr("1. Source media"); color: Theme.textPrimary; font.pixelSize: Theme.fontLarge; font.bold: true }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Choose or drop a local video, or import a direct public media link. Links use the same managed staging backend as Download and are never stored in the OCR project.")
                            color: Theme.textSecondary
                            wrapMode: Text.WordWrap
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.paddingMedium
                            Rectangle {
                                id: sourceDropZone
                                objectName: "subtitleOcrLocalDropZone"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 94
                                radius: Theme.radiusSmall
                                color: sourceDropArea.containsDrag ? Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.18) : Theme.surfaceAlt
                                border.color: sourceDropArea.containsDrag ? Theme.primary : Theme.border
                                border.width: 1
                                DropArea {
                                    id: sourceDropArea
                                    anchors.fill: parent
                                    keys: ["text/uri-list"]
                                    enabled: !ocr.processing && !ocr.sourceImporting
                                    onDropped: function(drop) {
                                        if (drop.urls.length > 0) root.loadVideo(drop.urls[0])
                                    }
                                }
                                Text {
                                    anchors.centerIn: parent
                                    width: parent.width - Theme.paddingLarge * 2
                                    text: qsTr("Drop a local video here")
                                    color: Theme.textPrimary
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.WordWrap
                                }
                            }
                            Button {
                                id: chooseVideoButton
                                objectName: "subtitleOcrChooseVideoButton"
                                text: qsTr("Choose video")
                                enabled: !ocr.processing && !ocr.sourceImporting
                                onClicked: videoFileDialog.open()
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField {
                                id: sourceLinkInput
                                objectName: "subtitleOcrSourceLinkInput"
                                Layout.fillWidth: true
                                enabled: !ocr.processing && !ocr.sourceImporting
                                placeholderText: qsTr("https://direct.example/video.mp4, YouTube, TikTok, or Douyin URL")
                                selectByMouse: true
                                color: Theme.textPrimary
                                placeholderTextColor: Theme.textSecondary
                                onAccepted: if (text.trim() !== "") ocr.importSourceLink(text.trim())
                            }
                            Button {
                                id: importLinkButton
                                objectName: "subtitleOcrImportLinkButton"
                                text: qsTr("Import link")
                                enabled: sourceLinkInput.text.trim() !== "" && !ocr.processing && !ocr.sourceImporting
                                onClicked: ocr.importSourceLink(sourceLinkInput.text.trim())
                            }
                            Button {
                                objectName: "subtitleOcrCancelLinkButton"
                                text: qsTr("Cancel")
                                visible: ocr.sourceImporting
                                onClicked: ocr.cancelSourceImport()
                            }
                            Button {
                                objectName: "subtitleOcrRetryLinkButton"
                                text: qsTr("Retry")
                                enabled: !ocr.processing && !ocr.sourceImporting && ocr.sourceImportError !== ""
                                onClicked: ocr.retrySourceImport()
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: ocr.sourceImporting || ocr.sourceImportStatus !== ""
                            text: ocr.sourceImportTotalBytes > 0
                                  ? ocr.sourceImportStatus + " — " + (ocr.sourceImportReceivedBytes / 1048576).toFixed(1) + " / " + (ocr.sourceImportTotalBytes / 1048576).toFixed(1) + " MiB"
                                  : ocr.sourceImportStatus
                            color: Theme.textSecondary
                            wrapMode: Text.WordWrap
                        }
                        ProgressBar {
                            Layout.fillWidth: true
                            visible: ocr.sourceImporting && ocr.sourceImportTotalBytes > 0
                            from: 0
                            to: ocr.sourceImportTotalBytes
                            value: ocr.sourceImportReceivedBytes
                        }
                        BusyIndicator {
                            id: sourceImportIndeterminateIndicator
                            objectName: "subtitleOcrSourceImportIndeterminateIndicator"
                            Layout.alignment: Qt.AlignHCenter
                            // A public-media origin is permitted to omit
                            // Content-Length. Keep an explicit running state
                            // in that case rather than presenting a frozen
                            // zero-percent progress bar.
                            visible: ocr.sourceImporting && ocr.sourceImportTotalBytes <= 0
                            running: visible
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: ocr.sourceImportError !== ""
                            text: ocr.sourceImportError
                            color: Theme.danger
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: ocr.sourcePath !== ""
                            text: qsTr("Current source: %1").arg(ocr.sourcePath)
                            color: Theme.success
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }

                Rectangle {
                    id: previewCard
                    objectName: "subtitleOcrPreviewCard"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(470, Math.min(720, width * 0.72))
                    radius: Theme.radiusMedium
                    color: Theme.surface
                    border.color: Theme.border
                    border.width: 1

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.paddingLarge
                        spacing: Theme.paddingMedium
                        Text { text: qsTr("2. Video preview and subtitle region"); color: Theme.textPrimary; font.pixelSize: Theme.fontLarge; font.bold: true }
                        Rectangle {
                            id: videoCanvas
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: 250
                            color: Qt.rgba(0, 0, 0, 0.52)
                            border.color: Theme.border
                            radius: Theme.radiusSmall
                            clip: true
                            VideoOutput { id: videoPreviewOutput; anchors.fill: parent; fillMode: VideoOutput.PreserveAspectFit }
                            Text {
                                anchors.centerIn: parent
                                width: parent.width - Theme.paddingLarge * 2
                                visible: ocr.sourceUrl.toString() === ""
                                text: qsTr("Choose or drop a video above to preview it")
                                color: Theme.textSecondary
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                            }
                            Rectangle {
                                id: roiOverlay
                                objectName: "subtitleOcrRoiOverlay"
                                visible: ocr.sourceWidth > 0 && root.displayedWidth > 0
                                x: root.displayedX + root.draftRoiX * root.displayedWidth
                                y: root.displayedY + root.draftRoiY * root.displayedHeight
                                width: root.draftRoiWidth * root.displayedWidth
                                height: root.draftRoiHeight * root.displayedHeight
                                color: Qt.rgba(0.45, 0.20, 1.0, 0.16)
                                border.color: Theme.primary
                                border.width: 2
                                z: 3
                                MouseArea {
                                    anchors.fill: parent
                                    property real grabX: 0
                                    property real grabY: 0
                                    cursorShape: Qt.SizeAllCursor
                                    onPressed: function(mouse) {
                                        grabX = mouse.x
                                        grabY = mouse.y
                                        root.beginRoiDrag()
                                    }
                                    onPositionChanged: function(mouse) {
                                        if (pressed)
                                            root.moveRoi(parent.mapToItem(videoCanvas, mouse.x, mouse.y), grabX, grabY)
                                    }
                                    onReleased: root.commitOverlayPosition()
                                    onCanceled: root.commitOverlayPosition()
                                }
                                RoiHandle { objectName: "subtitleOcrRoiHandleTl"; mode: "tl"; x: -width / 2; y: -height / 2 }
                                RoiHandle { objectName: "subtitleOcrRoiHandleTr"; mode: "tr"; x: parent.width - width / 2; y: -height / 2 }
                                RoiHandle { objectName: "subtitleOcrRoiHandleBl"; mode: "bl"; x: -width / 2; y: parent.height - height / 2 }
                                RoiHandle { objectName: "subtitleOcrRoiHandleBr"; mode: "br"; x: parent.width - width / 2; y: parent.height - height / 2 }
                                RoiHandle { objectName: "subtitleOcrRoiHandleL"; mode: "l"; x: -width / 2; y: parent.height / 2 - height / 2 }
                                RoiHandle { objectName: "subtitleOcrRoiHandleR"; mode: "r"; x: parent.width - width / 2; y: parent.height / 2 - height / 2 }
                                RoiHandle { objectName: "subtitleOcrRoiHandleT"; mode: "t"; x: parent.width / 2 - width / 2; y: -height / 2 }
                                RoiHandle { objectName: "subtitleOcrRoiHandleB"; mode: "b"; x: parent.width / 2 - width / 2; y: parent.height - height / 2 }
                            }
                            HoverHandler {
                                id: subtitlePreviewHoverHandler
                                enabled: ocr.sourcePath !== ""
                                onHoveredChanged: subtitleControlsAutoHide.pointerInsideSurface = hovered
                            }
                        }
                        Rectangle {
                            id: subtitleSharedMediaControls
                            objectName: "subtitleOcrSharedMediaControls"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 48
                            radius: Theme.radiusSmall
                            color: Theme.surfaceAlt
                            border.color: Theme.border
                            border.width: 1
                            visible: ocr.sourcePath !== ""
                            opacity: subtitleControlsAutoHide.controlsVisible ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: 180 } }
                            HoverHandler {
                                enabled: ocr.sourcePath !== ""
                                onHoveredChanged: subtitleControlsAutoHide.pointerInsideSurface = hovered
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.paddingMedium
                                anchors.rightMargin: Theme.paddingMedium
                                spacing: Theme.paddingSmall
                                Button {
                                    id: subtitlePlayButton
                                    implicitWidth: 30
                                    implicitHeight: 30
                                    flat: true
                                    contentItem: LineIcon {
                                        anchors.centerIn: parent
                                        name: player.playbackState === MediaPlayer.PlayingState ? "pause" : "play"
                                        color: Theme.textPrimary
                                        width: 15
                                        height: 15
                                    }
                                    onClicked: {
                                        player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play()
                                        subtitleControlsAutoHide.noteInteraction()
                                    }
                                }
                                Slider {
                                    id: subtitleSeekSlider
                                    Layout.fillWidth: true
                                    from: 0
                                    to: Math.max(1, ocr.durationMs)
                                    value: root.previewPositionMs
                                    enabled: ocr.durationMs > 0
                                    onPressedChanged: {
                                        subtitleControlsAutoHide.interactionActive = pressed
                                        if (!pressed) {
                                            player.position = value
                                            subtitleControlsAutoHide.noteInteraction()
                                        }
                                    }
                                    onMoved: {
                                        player.position = value
                                        subtitleControlsAutoHide.noteInteraction()
                                    }
                                }
                                Text {
                                    text: Math.round(player.position / 1000) + "s / "
                                          + Math.round(ocr.durationMs / 1000) + "s"
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSmall
                                }
                            }
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.paddingSmall
                            Button { text: qsTr("Preset lower region"); enabled: !ocr.processing && ocr.sourcePath !== ""; onClicked: ocr.setLowerRegionPreset() }
                            Button { text: qsTr("Reset region"); enabled: !ocr.processing && ocr.sourcePath !== ""; onClicked: ocr.resetRoi() }
                            Button { text: qsTr("Preview crop"); enabled: !ocr.processing && ocr.sourcePath !== ""; onClicked: ocr.requestCropPreview(player.position) }
                            Text { text: qsTr("ROI: x %1, y %2, w %3, h %4").arg(root.draftRoiX.toFixed(3)).arg(root.draftRoiY.toFixed(3)).arg(root.draftRoiWidth.toFixed(3)).arg(root.draftRoiHeight.toFixed(3)); color: Theme.textSecondary; topPadding: 7 }
                        }
                    }
                }

                Rectangle {
                    id: runtimeCard
                    objectName: "subtitleOcrRuntimeCard"
                    Layout.fillWidth: true
                    implicitHeight: runtimeLayout.implicitHeight + Theme.paddingLarge * 2
                    radius: Theme.radiusMedium
                    color: ocr.runtimeAvailable || root.usingColabRoute ? Theme.surface : Qt.rgba(1.0, 0.65, 0.15, 0.09)
                    border.color: ocr.runtimeAvailable || root.usingColabRoute ? Theme.border : Theme.warning
                    border.width: 1

                    ColumnLayout {
                        id: runtimeLayout
                        anchors.fill: parent
                        anchors.margins: Theme.paddingLarge
                        spacing: Theme.paddingSmall
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: qsTr("3. Local CPU runtime and language packs"); color: Theme.textPrimary; font.pixelSize: Theme.fontLarge; font.bold: true; Layout.fillWidth: true }
                            Text { text: root.usingPaddleLocalEngine ? qsTr("PaddleOCR %1").arg(ocr.localRuntimeState) : runtime.stateName; color: ocr.localRouteReady || root.usingColabRoute ? Theme.success : Theme.warning; font.bold: true }
                            Button { text: qsTr("Refresh"); enabled: !runtime.busy; onClicked: runtime.refresh() }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: root.usingPaddleLocalEngine
                                  ? (ocr.runtimeAvailable
                                     ? qsTr("Using bundled PaddleOCR PP-OCRv6 tiny %1. The isolated runtime and verified model cache run offline; no global Python is used.").arg(ocr.localEngineVersion)
                                     : qsTr("PaddleOCR PP-OCRv6 tiny is the default local engine but its package runtime or verified model cache is missing. Repair the package; LA Studio will not fall back silently."))
                                  : (runtime.runtimeAvailable ? qsTr("Using %1 Tesseract baseline runtime: %2").arg(runtime.runtimeSource).arg(runtime.runtimePath)
                                                             : qsTr("The package-provisioned Tesseract baseline is required only when that engine is selected. Language data location: %1").arg(runtime.managedRuntimePath))
                            color: Theme.textSecondary
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: !root.usingColabRoute
                            text: root.usingPaddleLocalEngine
                                  ? qsTr("Execution route: Local CPU · PaddleOCR uses offline PP-OCRv6 tiny batch recognition. Tesseract is available only as an explicit compatibility baseline.")
                                  : qsTr("Execution route: Local CPU · Tesseract baseline works offline; internet is used only when you explicitly install a verified language pack.")
                            color: Theme.textSecondary
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: root.usingPaddleLocalEngine && !ocr.localRouteReady && ocr.runtimeAvailable
                            text: qsTr("This bundled PaddleOCR candidate is ready only for Simplified Chinese (chi_sim). Select chi_sim, use the explicit Tesseract baseline with an installed language pack, or use Direct Colab GPU for another supported language.")
                            color: Theme.warning
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: root.usingColabRoute
                            text: qsTr("Execution route: Colab GPU. No local OCR engine is started or used for this run; only cropped sample frames are uploaded after the Colab worker is checked.")
                            color: Theme.textSecondary
                            wrapMode: Text.WordWrap
                        }
                        Text { Layout.fillWidth: true; visible: runtime.runtimeSource === "environment"; text: qsTr("LASTUDIO_TESSERACT override is active. LA Studio will not modify that external runtime."); color: Theme.warning; wrapMode: Text.WordWrap }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.paddingSmall
                            Button { text: qsTr("Repair the package runtime"); visible: !root.usingPaddleLocalEngine && !runtime.runtimeAvailable; enabled: !runtime.busy; onClicked: runtime.installRuntime() }
                            Button { text: qsTr("Retry language install"); visible: !root.usingPaddleLocalEngine && runtime.error !== "" && runtime.runtimeAvailable && !runtime.busy; onClicked: runtime.retryInstallation() }
                            Button { text: qsTr("Cancel install"); visible: !root.usingPaddleLocalEngine && runtime.busy; onClicked: runtime.cancelInstallation() }
                            Button { id: openRuntimeDiagnosticsButton; objectName: "subtitleOcrOpenRuntimeDiagnosticsButton"; text: qsTr("Open diagnostics"); visible: !root.usingPaddleLocalEngine && runtime.diagnostics !== ""; onClicked: runtimeDiagnosticsDialog.open() }
                            Button { id: cleanFailedRuntimeDownloadButton; objectName: "subtitleOcrCleanFailedRuntimeDownloadButton"; text: qsTr("Clean failed download"); visible: !root.usingPaddleLocalEngine && runtime.stateName === "Failed" && runtime.canCleanFailedDownload; enabled: !runtime.busy; onClicked: runtime.cleanFailedDownload() }
                            Text { text: root.usingPaddleLocalEngine ? qsTr("PaddleOCR %1 · PP-OCRv6 tiny · Apache-2.0 · CPU").arg(ocr.localEngineVersion) : qsTr("Tesseract %1 · Apache-2.0 · CPU baseline").arg(runtime.runtimeVersion === "" ? "5.5.1" : runtime.runtimeVersion); color: Theme.textSecondary; topPadding: 7 }
                        }
                        ProgressBar { Layout.fillWidth: true; visible: runtime.progressAvailable; from: 0; to: runtime.bytesTotal; value: runtime.bytesReceived }
                        Text { Layout.fillWidth: true; visible: runtime.progressAvailable; text: qsTr("Downloaded %1 / %2 MiB").arg((runtime.bytesReceived / 1048576).toFixed(1)).arg((runtime.bytesTotal / 1048576).toFixed(1)); color: Theme.textSecondary }
                        Text { Layout.fillWidth: true; visible: runtime.error !== ""; text: runtime.error; color: Theme.danger; wrapMode: Text.WordWrap }
                        Text { visible: !root.usingPaddleLocalEngine; text: qsTr("Tesseract baseline language data"); color: Theme.textPrimary; font.bold: true; topPadding: Theme.paddingSmall }
                        ScrollView {
                            id: languagePackScroll
                            objectName: "subtitleOcrLanguagePackScroll"
                            visible: !root.usingPaddleLocalEngine
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.min(190, languagePackColumn.implicitHeight)
                            clip: true
                            contentWidth: availableWidth
                            ColumnLayout {
                                id: languagePackColumn
                                width: languagePackScroll.availableWidth
                                spacing: Theme.paddingSmall
                                Repeater {
                                    model: runtime.languagePacks
                                    delegate: RowLayout {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 1
                                            Text { text: modelData.label + " (" + modelData.code + ")"; color: Theme.textSecondary; Layout.fillWidth: true; elide: Text.ElideRight }
                                            Text {
                                                Layout.fillWidth: true
                                                visible: modelData.state !== "Missing"
                                                text: modelData.detail || ""
                                                color: modelData.installed ? Theme.success : Theme.warning
                                                font.pixelSize: Theme.fontSmall
                                                wrapMode: Text.WrapAnywhere
                                            }
                                        }
                                        Text { text: modelData.state; color: modelData.installed ? Theme.success : Theme.warning }
                                        Button { text: modelData.installed ? qsTr("Verified") : qsTr("Install"); enabled: runtime.runtimeAvailable && !runtime.busy && !modelData.installed && runtime.runtimeSource !== "environment"; onClicked: runtime.installLanguage(modelData.code) }
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    id: settingsCard
                    objectName: "subtitleOcrSettingsCard"
                    Layout.fillWidth: true
                    implicitHeight: settingsLayout.implicitHeight + Theme.paddingLarge * 2
                    radius: Theme.radiusMedium
                    color: Theme.surface
                    border.color: Theme.border
                    border.width: 1

                    ColumnLayout {
                        id: settingsLayout
                        anchors.fill: parent
                        anchors.margins: Theme.paddingLarge
                        spacing: Theme.paddingMedium
                        Text { text: qsTr("4. OCR settings and actions"); color: Theme.textPrimary; font.pixelSize: Theme.fontLarge; font.bold: true }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: qsTr("Execution route"); color: Theme.textSecondary; Layout.preferredWidth: 115 }
                            ComboBox {
                                id: subtitleOcrExecutionRoute
                                objectName: "subtitleOcrExecutionRoute"
                                Layout.fillWidth: true
                                textRole: "label"
                                model: [
                                    { "id": "local-cpu", "label": qsTr("Local CPU · selected OCR engine") },
                                    { "id": "colab-gpu", "label": qsTr("Colab GPU · PP-OCRv5 Multilingual 3.1") }
                                ]
                                currentIndex: ocr.executionRoute === "colab-gpu" ? 1 : 0
                                enabled: !ocr.processing
                                onActivated: ocr.setExecutionRoute(model[index].id)
                            }
                            Button {
                                id: subtitleOcrConfigureColabButton
                                objectName: "subtitleOcrConfigureColabButton"
                                visible: root.usingColabRoute
                                text: qsTr("Configure / check Colab")
                                enabled: !ocr.processing
                                onClicked: subtitleOcrColabDialog.open()
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: !root.usingColabRoute
                            Text { text: qsTr("Local OCR engine"); color: Theme.textSecondary; Layout.preferredWidth: 115 }
                            ComboBox {
                                id: subtitleOcrLocalEngine
                                objectName: "subtitleOcrLocalEngine"
                                Layout.fillWidth: true
                                textRole: "label"
                                model: [
                                    { "id": "paddleocr-ppocrv6-tiny", "label": qsTr("PaddleOCR PP-OCRv6 tiny 3.7.0 · default") },
                                    { "id": "tesseract-baseline", "label": qsTr("Tesseract 5.5.1 · compatibility baseline") }
                                ]
                                currentIndex: ocr.localEngineId === "tesseract-baseline" ? 1 : 0
                                enabled: !ocr.processing
                                onActivated: ocr.setLocalEngine(model[index].id)
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: root.usingColabRoute
                            text: ocr.colabRouteStatus
                            color: ocr.colabRouteReady ? Theme.success : Theme.warning
                            wrapMode: Text.WordWrap
                        }
                        ComboBox {
                            id: languageSelector
                            objectName: "subtitleOcrLanguageSelector"
                            Layout.fillWidth: true
                            model: runtime.languagePacks
                            textRole: "label"
                            valueRole: "code"
                            enabled: !ocr.processing
                            function selectOcrLanguage() {
                                for (var i = 0; i < model.length; ++i) if (model[i].code === ocr.ocrLanguage) { currentIndex = i; return }
                            }
                            Component.onCompleted: selectOcrLanguage()
                            onModelChanged: selectOcrLanguage()
                            onActivated: ocr.setOcrLanguage(currentValue)
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: root.wideLayout ? 2 : 1
                            columnSpacing: Theme.paddingMedium
                            rowSpacing: Theme.paddingSmall
                            ColumnLayout {
                                Layout.fillWidth: true
                                Text { text: qsTr("Sample interval (ms)"); color: Theme.textSecondary }
                                SpinBox { id: intervalInput; Layout.fillWidth: true; from: 100; to: 30000; stepSize: 100; value: ocr.sampleIntervalMs; onValueModified: ocr.setSampleIntervalMs(value) }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Text { text: qsTr("Minimum confidence: %1%").arg(Math.round(confidenceInput.value * 100)); color: Theme.textSecondary }
                                Slider { id: confidenceInput; Layout.fillWidth: true; from: 0; to: 1; stepSize: 0.01; value: ocr.minimumConfidence; onMoved: ocr.setMinimumConfidence(value) }
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            visible: ocr.cropPreviewUrl.toString() !== ""
                            Layout.preferredHeight: visible ? 180 : 0
                            color: Theme.surfaceAlt
                            radius: Theme.radiusSmall
                            Image { anchors.fill: parent; anchors.margins: Theme.paddingSmall; source: ocr.cropPreviewUrl; fillMode: Image.PreserveAspectFit; asynchronous: true }
                        }
                        Text { Layout.fillWidth: true; visible: ocr.error !== ""; text: ocr.error; color: Theme.danger; wrapMode: Text.WordWrap }
                        Text { Layout.fillWidth: true; text: ocr.phase; color: Theme.textSecondary }
                        ProgressBar { Layout.fillWidth: true; visible: ocr.progressAvailable; from: 0; to: 100; value: ocr.progress }
                        Text { visible: ocr.progressAvailable; text: ocr.progress + "%"; color: Theme.textSecondary }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.paddingSmall
                            Button {
                                id: runOcrButton
                                objectName: "subtitleOcrRunButton"
                                text: qsTr("Run Subtitle OCR")
                                enabled: !ocr.processing && ocr.sourcePath !== "" && ocr.roiWidth > 0 && ocr.roiHeight > 0
                                         && (root.usingColabRoute ? ocr.colabRouteReady
                                                                  : (ocr.runtimeAvailable && root.selectedLanguageReady))
                                onClicked: ocr.run()
                            }
                            Button { text: qsTr("Cancel OCR"); enabled: ocr.processing; onClicked: ocr.cancel() }
                            Button {
                                id: retryFrameExtractionButton
                                objectName: "subtitleOcrRetryFrameExtractionButton"
                                text: qsTr("Retry frame extraction")
                                visible: ocr.canRetryFrameExtraction
                                enabled: !ocr.processing
                                onClicked: ocr.retryFrameExtraction()
                            }
                            Button {
                                text: qsTr("Retry OCR")
                                visible: !ocr.canRetryFrameExtraction
                                enabled: !ocr.processing && ocr.phase === "error"
                                onClicked: ocr.retry()
                            }
                            Button {
                                id: openSubtitleOcrDiagnosticsButton
                                objectName: "subtitleOcrOpenDiagnosticsButton"
                                text: qsTr("Open diagnostics")
                                visible: ocr.diagnostics !== ""
                                enabled: !ocr.processing
                                onClicked: subtitleOcrDiagnosticsDialog.open()
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: !root.usingColabRoute && (!ocr.runtimeAvailable || !root.selectedLanguageReady)
                            text: !ocr.runtimeAvailable
                                  ? (root.usingPaddleLocalEngine
                                     ? qsTr("Repair the package to restore the bundled PaddleOCR runtime and verified model cache.")
                                     : qsTr("Repair the package runtime to enable the Tesseract baseline."))
                                  : (root.usingPaddleLocalEngine
                                     ? qsTr("The selected language is not bundled with local PaddleOCR. Select chi_sim, Tesseract, or Direct Colab GPU.")
                                     : qsTr("Install the selected language pack to enable the Tesseract baseline."))
                            color: Theme.warning
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: root.usingColabRoute && !ocr.colabRouteReady
                            text: qsTr("Connect and check the exact Colab Subtitle OCR notebook before running. LA Studio will not silently fall back to Local CPU.")
                            color: Theme.warning
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Rectangle {
                    id: transcriptCard
                    objectName: "subtitleOcrTranscriptCard"
                    Layout.fillWidth: true
                    Layout.columnSpan: cardGrid.columns
                    Layout.preferredHeight: 300
                    radius: Theme.radiusMedium
                    color: Theme.surface
                    border.color: Theme.border
                    border.width: 1

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.paddingLarge
                        spacing: Theme.paddingMedium
                        Text { text: qsTr("5. Reviewed OCR transcript and export"); color: Theme.textPrimary; font.pixelSize: Theme.fontLarge; font.bold: true }
                        ListView {
                            id: segmentView
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: ocr.segments
                            delegate: RowLayout {
                                required property int index
                                required property var modelData
                                width: segmentView.width
                                Text { text: Math.round(modelData.startMs / 1000) + "–" + Math.round(modelData.endMs / 1000) + "s"; color: Theme.textSecondary; Layout.preferredWidth: 92 }
                                TextField { Layout.fillWidth: true; text: modelData.text; onEditingFinished: ocr.updateSegment(index, { text: text }) }
                                Text { text: Math.round(modelData.confidence * 100) + "%"; color: Theme.textSecondary; Layout.preferredWidth: 44 }
                                Button { text: qsTr("Remove"); enabled: !ocr.processing; onClicked: ocr.removeSegment(index) }
                            }
                            Text {
                                anchors.centerIn: parent
                                visible: segmentView.count === 0
                                text: qsTr("Reviewed subtitle segments will appear here after OCR.")
                                color: Theme.textSecondary
                            }
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.paddingSmall
                            Button { text: qsTr("Export SRT"); enabled: ocr.segments.length > 0; onClicked: saveSrtDialog.open() }
                            Button { text: qsTr("Export text"); enabled: ocr.segments.length > 0; onClicked: saveTextDialog.open() }
                            Button { text: qsTr("Open in Subtitle Voice"); enabled: ocr.segments.length > 0; onClicked: ocr.sendToSubtitleVoice() }
                            Button { text: qsTr("Use in Dubbing"); enabled: ocr.segments.length > 0; onClicked: ocr.sendToDubbing() }
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
                text: qsTr("Manual check: choose, drop, or import a video; adjust the subtitle region; preview the crop; install the CPU runtime and a language pack; run OCR; review segments; then save or export.")
            }
        }
    }
}
