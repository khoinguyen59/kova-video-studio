import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia
import LAStudio

Page {
    id: root
    readonly property var ocr: AppController.subtitleOcr
    readonly property var runtime: AppController.subtitleOcrRuntime
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
                var point = parent.mapToItem(videoCanvas, mouse.x, mouse.y)
                root.resizeRoi(parent.mode, point)
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

    MediaPlayer {
        id: player
        source: ocr.sourceUrl
        audioOutput: AudioOutput {}
        videoOutput: videoPreviewOutput
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.paddingLarge
        spacing: Theme.paddingMedium

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTr("Subtitle OCR")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontXLarge
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Extract hardcoded image subtitles from a selected video. This is separate from speech-to-text and SRT import.")
                color: Theme.textSecondary
                wrapMode: Text.Wrap
            }
            Button { text: qsTr("Open project"); enabled: !ocr.processing; onClicked: openProjectDialog.open() }
            Button { text: qsTr("Save project"); enabled: !ocr.processing; onClicked: ocr.projectPath === "" ? saveProjectDialog.open() : ocr.saveProject() }
            Button { text: qsTr("Refresh OCR runtime"); enabled: !ocr.processing; onClicked: ocr.refreshRuntime() }
        }

        Rectangle {
            Layout.fillWidth: true
            color: runtime.runtimeAvailable ? Qt.rgba(0.20, 0.85, 0.45, 0.08) : Qt.rgba(1.0, 0.65, 0.15, 0.12)
            border.color: runtime.runtimeAvailable ? Theme.success : Theme.warning
            radius: Theme.radiusSmall
            implicitHeight: runtimeSetup.implicitHeight + Theme.paddingMedium * 2
            ColumnLayout {
                id: runtimeSetup
                anchors.fill: parent
                anchors.margins: Theme.paddingMedium
                spacing: Theme.paddingSmall
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: qsTr("OCR runtime setup"); color: Theme.textPrimary; font.bold: true }
                    Item { Layout.fillWidth: true }
                    Text { text: runtime.stateName; color: runtime.runtimeAvailable ? Theme.success : Theme.warning; font.bold: true }
                    Button { text: qsTr("Refresh"); enabled: !runtime.busy; onClicked: runtime.refresh() }
                }
                Text {
                    Layout.fillWidth: true
                    color: Theme.textSecondary
                    wrapMode: Text.Wrap
                    text: runtime.runtimeAvailable
                        ? qsTr("Using %1 runtime: %2").arg(runtime.runtimeSource).arg(runtime.runtimePath)
                        : qsTr("Install the app-managed CPU runtime before running OCR. Installation starts only after you click the button; it is verified with a pinned SHA-256 and needs no administrator permission.")
                }
                Text {
                    Layout.fillWidth: true
                    visible: runtime.runtimeSource === "environment"
                    color: Theme.warning
                    wrapMode: Text.Wrap
                    text: qsTr("Advanced override LASTUDIO_TESSERACT is active. LA Studio will not modify that external runtime; managed language packs require the app-owned runtime.")
                }
                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        text: runtime.runtimeSource === "managed" ? qsTr("Reinstall app-managed runtime") : qsTr("Install app-managed runtime")
                        enabled: !runtime.busy
                        onClicked: runtime.installRuntime()
                    }
                    Button { text: qsTr("Retry"); visible: runtime.stateName === "Failed"; enabled: !runtime.busy; onClicked: runtime.retryInstallation() }
                    Button { text: qsTr("Cancel installation"); visible: runtime.busy; onClicked: runtime.cancelInstallation() }
                    Text {
                        Layout.fillWidth: true
                        color: Theme.textSecondary
                        text: qsTr("Tesseract %1 · Apache-2.0 · CPU only").arg(runtime.runtimeVersion === "" ? "5.5.3" : runtime.runtimeVersion)
                    }
                }
                ProgressBar {
                    Layout.fillWidth: true
                    visible: runtime.progressAvailable
                    from: 0
                    to: runtime.bytesTotal
                    value: runtime.bytesReceived
                }
                Text {
                    Layout.fillWidth: true
                    visible: runtime.progressAvailable
                    color: Theme.textSecondary
                    text: qsTr("Downloaded %1 / %2 MiB").arg((runtime.bytesReceived / 1048576).toFixed(1)).arg((runtime.bytesTotal / 1048576).toFixed(1))
                }
                Text {
                    Layout.fillWidth: true
                    visible: runtime.error !== ""
                    color: Theme.danger
                    wrapMode: Text.Wrap
                    text: runtime.error
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.paddingLarge

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 3
                spacing: Theme.paddingSmall

                Rectangle {
                    id: videoCanvas
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 300
                    color: Qt.rgba(0, 0, 0, 0.52)
                    border.color: Theme.border
                    radius: Theme.radiusSmall
                    clip: true

                    VideoOutput {
                        id: videoPreviewOutput
                        anchors.fill: parent
                        fillMode: VideoOutput.PreserveAspectFit
                    }

                    DropArea {
                        anchors.fill: parent
                        keys: ["text/uri-list"]
                        onDropped: function(drop) {
                            if (drop.urls.length > 0) root.loadVideo(drop.urls[0])
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: ocr.sourceUrl.toString() === ""
                        text: qsTr("Drop a video here or choose a video file")
                        color: Theme.textSecondary
                    }

                    Rectangle {
                        id: roiOverlay
                        visible: ocr.sourceWidth > 0 && root.displayedWidth > 0
                        x: root.displayedX + ocr.roiX * root.displayedWidth
                        y: root.displayedY + ocr.roiY * root.displayedHeight
                        width: ocr.roiWidth * root.displayedWidth
                        height: ocr.roiHeight * root.displayedHeight
                        color: Qt.rgba(0.45, 0.20, 1.0, 0.16)
                        border.color: Theme.primary
                        border.width: 2

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
                        RoiHandle { mode: "tl"; x: -width / 2; y: -height / 2 }
                        RoiHandle { mode: "tr"; x: parent.width - width / 2; y: -height / 2 }
                        RoiHandle { mode: "bl"; x: -width / 2; y: parent.height - height / 2 }
                        RoiHandle { mode: "br"; x: parent.width - width / 2; y: parent.height - height / 2 }
                        RoiHandle { mode: "l"; x: -width / 2; y: parent.height / 2 - height / 2 }
                        RoiHandle { mode: "r"; x: parent.width - width / 2; y: parent.height / 2 - height / 2 }
                        RoiHandle { mode: "t"; x: parent.width / 2 - width / 2; y: -height / 2 }
                        RoiHandle { mode: "b"; x: parent.width / 2 - width / 2; y: parent.height - height / 2 }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Button { text: player.playbackState === MediaPlayer.PlayingState ? qsTr("Pause") : qsTr("Play"); enabled: ocr.sourcePath !== ""; onClicked: player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play() }
                    Slider {
                        Layout.fillWidth: true
                        from: 0
                        to: Math.max(1, ocr.durationMs)
                        value: root.previewPositionMs
                        enabled: ocr.durationMs > 0
                        onMoved: player.position = value
                    }
                    Text { text: Math.round(player.position / 1000) + "s / " + Math.round(ocr.durationMs / 1000) + "s"; color: Theme.textSecondary }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Button { text: qsTr("Choose video"); enabled: !ocr.processing; onClicked: videoFileDialog.open() }
                    Button { text: qsTr("Reset subtitle region"); enabled: !ocr.processing; onClicked: ocr.resetRoi() }
                    Button { text: qsTr("Preview cropped frame"); enabled: !ocr.processing && ocr.sourcePath !== ""; onClicked: ocr.requestCropPreview(player.position) }
                    Text {
                        Layout.fillWidth: true
                        color: Theme.textSecondary
                        text: qsTr("Normalized ROI: x %1, y %2, w %3, h %4")
                              .arg(ocr.roiX.toFixed(3)).arg(ocr.roiY.toFixed(3))
                              .arg(ocr.roiWidth.toFixed(3)).arg(ocr.roiHeight.toFixed(3))
                    }
                }
            }

            ColumnLayout {
                Layout.fillHeight: true
                Layout.preferredWidth: 320
                spacing: Theme.paddingMedium

                GroupBox {
                    title: qsTr("OCR settings")
                    Layout.fillWidth: true
                    ColumnLayout {
                        anchors.fill: parent
                        Label { text: qsTr("Installed Tesseract language") }
                        ComboBox {
                            id: languageSelector
                            Layout.fillWidth: true
                            model: runtime.languagePacks
                            textRole: "label"
                            valueRole: "code"
                            function selectOcrLanguage() {
                                for (var i = 0; i < model.length; ++i) {
                                    if (model[i].code === ocr.ocrLanguage) {
                                        currentIndex = i
                                        return
                                    }
                                }
                            }
                            Component.onCompleted: {
                                selectOcrLanguage()
                            }
                            onModelChanged: selectOcrLanguage()
                            onActivated: ocr.setOcrLanguage(currentValue)
                        }
                        Repeater {
                            model: runtime.languagePacks
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                Text { text: modelData.label + " (" + modelData.code + ")"; color: Theme.textSecondary; Layout.fillWidth: true; elide: Text.ElideRight }
                                Text { text: modelData.state; color: modelData.installed ? Theme.success : Theme.warning }
                                Button {
                                    text: modelData.installed ? qsTr("Verified") : qsTr("Install")
                                    enabled: !runtime.busy && !modelData.installed && runtime.runtimeSource === "managed"
                                    onClicked: runtime.installLanguage(modelData.code)
                                }
                            }
                        }
                        Label { text: qsTr("Sample interval (ms)") }
                        SpinBox { id: intervalInput; from: 100; to: 30000; stepSize: 100; value: ocr.sampleIntervalMs; Layout.fillWidth: true; onValueModified: ocr.setSampleIntervalMs(value) }
                        Label { text: qsTr("Minimum confidence") }
                        Slider { id: confidenceInput; from: 0; to: 1; stepSize: 0.01; value: ocr.minimumConfidence; Layout.fillWidth: true; onMoved: ocr.setMinimumConfidence(value) }
                        Text { text: Math.round(confidenceInput.value * 100) + "%"; color: Theme.textSecondary }
                    }
                }

                GroupBox {
                    title: qsTr("Crop preview")
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Image {
                        anchors.fill: parent
                        anchors.margins: Theme.paddingSmall
                        source: ocr.cropPreviewUrl
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: ocr.error !== ""
                    text: ocr.error
                    color: Theme.danger
                    wrapMode: Text.Wrap
                }
                Text { Layout.fillWidth: true; text: ocr.phase; color: Theme.textSecondary }
                ProgressBar { Layout.fillWidth: true; visible: ocr.progressAvailable; from: 0; to: 100; value: ocr.progress }
                Text { visible: ocr.progressAvailable; text: ocr.progress + "%"; color: Theme.textSecondary }
                RowLayout {
                    Layout.fillWidth: true
                    Button { text: qsTr("Run Subtitle OCR"); enabled: !ocr.processing && ocr.sourcePath !== "" && ocr.runtimeAvailable; onClicked: ocr.run() }
                    Button { text: qsTr("Cancel"); enabled: ocr.processing; onClicked: ocr.cancel() }
                    Button { text: qsTr("Retry"); enabled: !ocr.processing && ocr.phase === "error"; onClicked: ocr.retry() }
                }
            }
        }

        GroupBox {
            title: qsTr("Reviewed OCR transcript")
            Layout.fillWidth: true
            Layout.preferredHeight: 230
            ColumnLayout {
                anchors.fill: parent
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
                        Label { text: Math.round(modelData.startMs / 1000) + "–" + Math.round(modelData.endMs / 1000) + "s"; Layout.preferredWidth: 92 }
                        TextField {
                            Layout.fillWidth: true
                            text: modelData.text
                            onEditingFinished: ocr.updateSegment(index, { text: text })
                        }
                        Label { text: Math.round(modelData.confidence * 100) + "%"; Layout.preferredWidth: 44 }
                        Button { text: qsTr("Remove"); enabled: !ocr.processing; onClicked: ocr.removeSegment(index) }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Button { text: qsTr("Export SRT"); enabled: ocr.segments.length > 0; onClicked: saveSrtDialog.open() }
                    Button { text: qsTr("Export text"); enabled: ocr.segments.length > 0; onClicked: saveTextDialog.open() }
                    Item { Layout.fillWidth: true }
                    Button { text: qsTr("Open in Subtitle Voice"); enabled: ocr.segments.length > 0; onClicked: ocr.sendToSubtitleVoice() }
                    Button { text: qsTr("Use in Dubbing"); enabled: ocr.segments.length > 0; onClicked: ocr.sendToDubbing() }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            color: Theme.textSecondary
            wrapMode: Text.Wrap
            text: qsTr("Manual click-through checklist: select a real video, move/resize the region over burned subtitles, preview the crop, run OCR with installed language data, edit segments, save/reopen the project, export SRT, then import reviewed text into an already-open Dubbing project.")
        }
    }
}
