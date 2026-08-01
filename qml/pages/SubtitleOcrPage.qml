import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia
import "../components/shared"
import LAStudio

Page {
    id: root

    readonly property var ocr: AppController.subtitleOcr
    readonly property var runtime: AppController.subtitleOcrRuntime
    readonly property bool wideLayout: width >= 1180
    readonly property bool selectedLanguageReady: runtime.runtimeSource === "environment"
                                                 ? ocr.ocrLanguage !== ""
                                                 : runtime.isLanguageInstalled(ocr.ocrLanguage)
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

    title: qsTr("Subtitle OCR")
    background: Rectangle { color: Theme.background }

    function loadVideo(path) {
        if (path && path.toString().length > 0)
            ocr.loadSource(path)
    }

    function clamp(value, low, high) { return Math.max(low, Math.min(high, value)) }

    function commitOverlayPosition() {
        if (displayedWidth <= 0 || displayedHeight <= 0) return
        ocr.setRoi((roiOverlay.x - displayedX) / displayedWidth,
                   (roiOverlay.y - displayedY) / displayedHeight,
                   roiOverlay.width / displayedWidth, roiOverlay.height / displayedHeight)
    }

    function resizeRoi(mode, point) {
        if (displayedWidth <= 0 || displayedHeight <= 0) return
        var left = roiOverlay.x
        var right = roiOverlay.x + roiOverlay.width
        var top = roiOverlay.y
        var bottom = roiOverlay.y + roiOverlay.height
        var x = clamp(point.x, displayedX, displayedX + displayedWidth)
        var y = clamp(point.y, displayedY, displayedY + displayedHeight)
        if (mode.indexOf("l") !== -1) left = Math.min(x, right - minimumRoiPixels)
        if (mode.indexOf("r") !== -1) right = Math.max(x, left + minimumRoiPixels)
        if (mode.indexOf("t") !== -1) top = Math.min(y, bottom - minimumRoiPixels)
        if (mode.indexOf("b") !== -1) bottom = Math.max(y, top + minimumRoiPixels)
        ocr.setRoi((left - displayedX) / displayedWidth, (top - displayedY) / displayedHeight,
                   (right - left) / displayedWidth, (bottom - top) / displayedHeight)
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

    function qmlSmokeMediaControlsCheck() {
        return subtitleControlsAutoHide.qmlSmokeStateCheck()
                && subtitleControlsAutoHide.delayMs === 2000
    }

    // Used by the offscreen QML route smoke. It verifies the responsive card
    // contract, child reachability and disabled-runtime behavior rather than
    // assuming a fixed window height or z-order.
    function qmlSmokeLayoutCheck() {
        var cards = [sourceMediaCard, previewCard, runtimeCard, settingsCard, transcriptCard]
        for (var i = 0; i < cards.length; ++i) {
            var card = cards[i]
            var rect = itemRectInContent(card)
            if (!card.visible || card.width <= 0 || card.height <= 0
                    || rect.x < -1 || rect.y < -1
                    || rect.x + rect.width > pageContent.width + 1) return false
            for (var j = i + 1; j < cards.length; ++j)
                if (rectanglesOverlap(card, cards[j])) return false
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
            return false
        if (videoCanvas.width <= 0 || videoCanvas.height <= 0
                || languagePackScroll.width <= 0 || languagePackScroll.height <= 0
                || languageSelector.width < languageSelector.implicitWidth
                || runOcrButton.width < runOcrButton.implicitWidth)
            return false
        // A missing runtime only blocks execution/install. Selecting the
        // desired language remains useful before the runtime is installed.
        if (!runtime.runtimeAvailable && (!languageSelector.enabled || runOcrButton.enabled))
            return false
        if (!qmlSmokeMediaControlsCheck()) return false
        var transcriptRect = itemRectInContent(transcriptCard)
        if (subtitleOcrScroll.contentHeight < transcriptRect.y + transcriptRect.height - 1)
            return false
        return !rectanglesOverlap(sourceMediaCard, previewCard)
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
                                x: root.displayedX + ocr.roiX * root.displayedWidth
                                y: root.displayedY + ocr.roiY * root.displayedHeight
                                width: ocr.roiWidth * root.displayedWidth
                                height: ocr.roiHeight * root.displayedHeight
                                color: Qt.rgba(0.45, 0.20, 1.0, 0.16)
                                border.color: Theme.primary
                                border.width: 2
                                z: 3
                                MouseArea {
                                    anchors.fill: parent
                                    drag.target: roiOverlay
                                    drag.minimumX: root.displayedX
                                    drag.maximumX: root.displayedX + root.displayedWidth - roiOverlay.width
                                    drag.minimumY: root.displayedY
                                    drag.maximumY: root.displayedY + root.displayedHeight - roiOverlay.height
                                    cursorShape: Qt.SizeAllCursor
                                    onReleased: root.commitOverlayPosition()
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
                            Rectangle {
                                id: subtitleSharedMediaControls
                                objectName: "subtitleOcrSharedMediaControls"
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 48
                                z: 5
                                visible: ocr.sourcePath !== "" && (opacity > 0 || subtitleControlsAutoHide.controlsVisible)
                                opacity: subtitleControlsAutoHide.controlsVisible ? 1 : 0
                                Behavior on opacity { NumberAnimation { duration: 250 } }
                                gradient: Gradient {
                                    GradientStop { position: 0; color: "transparent" }
                                    GradientStop { position: 1; color: Qt.rgba(0.06, 0.06, 0.09, 0.92) }
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
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.paddingSmall
                            Button { text: qsTr("Preset lower region"); enabled: !ocr.processing && ocr.sourcePath !== ""; onClicked: ocr.setLowerRegionPreset() }
                            Button { text: qsTr("Reset region"); enabled: !ocr.processing && ocr.sourcePath !== ""; onClicked: ocr.resetRoi() }
                            Button { text: qsTr("Preview crop"); enabled: !ocr.processing && ocr.sourcePath !== ""; onClicked: ocr.requestCropPreview(player.position) }
                            Text { text: qsTr("ROI: x %1, y %2, w %3, h %4").arg(ocr.roiX.toFixed(3)).arg(ocr.roiY.toFixed(3)).arg(ocr.roiWidth.toFixed(3)).arg(ocr.roiHeight.toFixed(3)); color: Theme.textSecondary; topPadding: 7 }
                        }
                    }
                }

                Rectangle {
                    id: runtimeCard
                    objectName: "subtitleOcrRuntimeCard"
                    Layout.fillWidth: true
                    implicitHeight: runtimeLayout.implicitHeight + Theme.paddingLarge * 2
                    radius: Theme.radiusMedium
                    color: runtime.runtimeAvailable ? Theme.surface : Qt.rgba(1.0, 0.65, 0.15, 0.09)
                    border.color: runtime.runtimeAvailable ? Theme.border : Theme.warning
                    border.width: 1

                    ColumnLayout {
                        id: runtimeLayout
                        anchors.fill: parent
                        anchors.margins: Theme.paddingLarge
                        spacing: Theme.paddingSmall
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: qsTr("3. OCR runtime and language packs"); color: Theme.textPrimary; font.pixelSize: Theme.fontLarge; font.bold: true; Layout.fillWidth: true }
                            Text { text: runtime.stateName; color: runtime.runtimeAvailable ? Theme.success : Theme.warning; font.bold: true }
                            Button { text: qsTr("Refresh"); enabled: !runtime.busy; onClicked: runtime.refresh() }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: runtime.runtimeAvailable ? qsTr("Using %1 runtime: %2").arg(runtime.runtimeSource).arg(runtime.runtimePath)
                                                           : qsTr("The app-managed CPU runtime is required only to run OCR. You can still choose/import video and set the region now. Managed location: %1").arg(runtime.managedRuntimePath)
                            color: Theme.textSecondary
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Execution route: Local CPU · No GPU or Colab required · Internet is used only for an explicit first-time verified download.")
                            color: Theme.textSecondary
                            wrapMode: Text.WordWrap
                        }
                        Text { Layout.fillWidth: true; visible: runtime.runtimeSource === "environment"; text: qsTr("LASTUDIO_TESSERACT override is active. LA Studio will not modify that external runtime."); color: Theme.warning; wrapMode: Text.WordWrap }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.paddingSmall
                            Button { text: runtime.runtimeSource === "managed" ? qsTr("Reinstall runtime") : qsTr("Install runtime"); enabled: !runtime.busy; onClicked: runtime.installRuntime() }
                            Button { text: qsTr("Retry install"); visible: runtime.stateName === "Failed"; enabled: !runtime.busy; onClicked: runtime.retryInstallation() }
                            Button { text: qsTr("Cancel install"); visible: runtime.busy; onClicked: runtime.cancelInstallation() }
                            Button { id: openRuntimeDiagnosticsButton; objectName: "subtitleOcrOpenRuntimeDiagnosticsButton"; text: qsTr("Open diagnostics"); visible: runtime.diagnostics !== ""; onClicked: runtimeDiagnosticsDialog.open() }
                            Button { id: cleanFailedRuntimeDownloadButton; objectName: "subtitleOcrCleanFailedRuntimeDownloadButton"; text: qsTr("Clean failed download"); visible: runtime.stateName === "Failed" && runtime.canCleanFailedDownload; enabled: !runtime.busy; onClicked: runtime.cleanFailedDownload() }
                            Text { text: qsTr("Tesseract %1 · Apache-2.0 · CPU").arg(runtime.runtimeVersion === "" ? "5.5.3" : runtime.runtimeVersion); color: Theme.textSecondary; topPadding: 7 }
                        }
                        ProgressBar { Layout.fillWidth: true; visible: runtime.progressAvailable; from: 0; to: runtime.bytesTotal; value: runtime.bytesReceived }
                        Text { Layout.fillWidth: true; visible: runtime.progressAvailable; text: qsTr("Downloaded %1 / %2 MiB").arg((runtime.bytesReceived / 1048576).toFixed(1)).arg((runtime.bytesTotal / 1048576).toFixed(1)); color: Theme.textSecondary }
                        Text { Layout.fillWidth: true; visible: runtime.error !== ""; text: runtime.error; color: Theme.danger; wrapMode: Text.WordWrap }
                        Text { text: qsTr("Language data"); color: Theme.textPrimary; font.bold: true; topPadding: Theme.paddingSmall }
                        ScrollView {
                            id: languagePackScroll
                            objectName: "subtitleOcrLanguagePackScroll"
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
                                        Text { text: modelData.label + " (" + modelData.code + ")"; color: Theme.textSecondary; Layout.fillWidth: true; elide: Text.ElideRight }
                                        Text { text: modelData.state; color: modelData.installed ? Theme.success : Theme.warning }
                                        Button { text: modelData.installed ? qsTr("Verified") : qsTr("Install"); enabled: runtime.runtimeAvailable && !runtime.busy && !modelData.installed && runtime.runtimeSource === "managed"; onClicked: runtime.installLanguage(modelData.code) }
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
                                enabled: !ocr.processing && ocr.sourcePath !== "" && ocr.roiWidth > 0 && ocr.roiHeight > 0 && ocr.runtimeAvailable && root.selectedLanguageReady
                                onClicked: ocr.run()
                            }
                            Button { text: qsTr("Cancel OCR"); enabled: ocr.processing; onClicked: ocr.cancel() }
                            Button { text: qsTr("Retry OCR"); enabled: !ocr.processing && ocr.phase === "error"; onClicked: ocr.retry() }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: !runtime.runtimeAvailable || !root.selectedLanguageReady
                            text: !runtime.runtimeAvailable ? qsTr("Install the OCR runtime to enable Run Subtitle OCR.") : qsTr("Install the selected language pack to enable Run Subtitle OCR.")
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
