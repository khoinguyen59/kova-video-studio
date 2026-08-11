import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtMultimedia
import "../base"
import "../shared"
import "../shared/settings"
import LAStudio

Rectangle {
    id: root

    required property var dubbing
    property int selectedSegment: -1
    readonly property string sourceMediaPath: root.dubbing.sourceMediaPath || ""
    readonly property bool hasLoadedSource: root.sourceMediaPath.length > 0
    readonly property bool isVideoSource: root.hasLoadedSource && /\.(mp4|mkv|mov|webm|avi)$/i.test(root.sourceMediaPath)
    readonly property bool hasDubbedPreview: root.dubbing.dubbedVocalPath.length > 0
    property string previewMode: "source"
    readonly property bool showingDubbedMedia: root.previewMode === "dubbed" && root.hasDubbedPreview
    property bool previewMuted: false
    property real vocalLevel: 1.0
    property real backgroundLevel: 1.0
    property real pendingPosition: -1
    property bool pendingPlayback: false
    property int sourceSwitchAttempts: 0
    property var draftOcrRoi: root.dubbing.dubbingOcrRoi || ({ x: 0.08, y: 0.80, width: 0.84, height: 0.16 })
    property bool ocrRoiDragging: false
    property bool ocrRoiEditMode: false
    // The Dubbing workspace can give the video canvas its own focused view
    // without changing the active project, workflow, or media source.
    property bool previewFocusMode: false
    // Link/download settings are useful before a source is chosen, but must not
    // consume the video canvas once an editor is working on an OCR scan area.
    property bool sourceSetupExpanded: true
    // A loaded source gets a compact, scrollable change/download drawer.  This
    // leaves the canvas usable even when an operator deliberately re-opens it.
    readonly property int sourceSetupMaximumHeight: root.hasLoadedSource ? 96 : 420
    // This is a display frame only. It never crops or stretches the source:
    // VideoOutput continues to preserve the source pixels inside the frame.
    property string previewFrameMode: "source"
    readonly property real previewFrameAspectRatio: previewFrameMode === "16:9" ? 16 / 9
                                                   : (previewFrameMode === "9:16" ? 9 / 16
                                                      : (previewFrameMode === "1:1" ? 1 : 0))
    readonly property var subtitleStyle: (root.dubbing.subtitleConfiguration || {}).style || ({})
    readonly property string activeSubtitleText: {
        for (var i = 0; i < root.dubbing.segments.length; ++i) {
            var segment = root.dubbing.segments[i]
            if (mediaPlayer.position >= segment.startMs && mediaPlayer.position <= segment.endMs)
                return (segment.targetText || segment.sourceText || "").trim()
        }
        return ""
    }

    signal browseRequested()
    signal linkImportRequested(string url)
    signal mediaQueueRequested(string urls)
    signal cancelLinkImportRequested()
    signal segmentSelected(int index)
    signal subtitleEditorRequested()
    signal previewFocusRequested(bool focused)

    Layout.fillWidth: true
    Layout.fillHeight: true
    // Keep a real canvas available for a 16:9 source and OCR ROI handles.  The
    // source/download controls below are scrollable after a media file exists.
    // Keep enough height for a useful OCR canvas at 1280×800, but let the
    // full-width timeline retain its own guaranteed editor space below.
    Layout.minimumHeight: root.isVideoSource ? 440 : 300
    color: Theme.surface
    radius: Theme.radiusMedium
    border.color: Qt.rgba(1, 1, 1, 0.08)
    border.width: 1

    function formatTime(ms) {
        if (isNaN(ms) || ms < 0) return "00:00"
        var totalSec = Math.floor(ms / 1000)
        var hr = Math.floor(totalSec / 3600)
        var min = Math.floor((totalSec - hr * 3600) / 60)
        var sec = totalSec - hr * 3600 - min * 60
        var minStr = min < 10 ? "0" + min : min.toString()
        var secStr = sec < 10 ? "0" + sec : sec.toString()
        return hr > 0 ? (hr < 10 ? "0" + hr : hr.toString()) + ":" + minStr + ":" + secStr : minStr + ":" + secStr
    }

    function formatBytes(bytes) {
        if (bytes < 0) return ""
        if (bytes < 1024) return bytes + " B"
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KiB"
        if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + " MiB"
        return (bytes / (1024 * 1024 * 1024)).toFixed(2) + " GiB"
    }

    function localMediaUrl(path) {
        if (!path) return ""
        var normalized = path.replace(/\\/g, "/")
        var encoded = encodeURI(normalized).replace(/#/g, "%23")
        return Qt.platform.os === "windows" ? "file:///" + encoded : "file://" + encoded
    }

    function pause() { pauseAll() }
    function pauseAll() {
        mediaPlayer.pause()
        vocalPlayer.pause()
        backgroundPlayer.pause()
    }
    function seekAll(position) {
        mediaPlayer.position = position
        if (root.showingDubbedMedia) {
            if (vocalPlayer.seekable) vocalPlayer.position = position
            if (backgroundPlayer.seekable) backgroundPlayer.position = position
        }
    }
    function playAll() {
        if (root.showingDubbedMedia) {
            if (root.dubbing.dubbedVocalPath.length > 0) {
                vocalPlayer.position = mediaPlayer.position
                vocalPlayer.play()
            }
            if (root.dubbing.backgroundPath.length > 0) {
                backgroundPlayer.position = mediaPlayer.position
                backgroundPlayer.play()
            }
        }
        mediaPlayer.play()
    }
    function switchPreviewMode(mode) {
        if (mode === root.previewMode || (mode === "dubbed" && !root.hasDubbedPreview)) return
        root.pendingPosition = mediaPlayer.position
        root.pendingPlayback = mediaPlayer.playbackState === MediaPlayer.PlayingState
        pauseAll()
        root.previewMode = mode
        root.sourceSwitchAttempts = 0
        sourceSwitchFallback.restart()
    }
    function restoreAfterSourceSwitch() {
        if (root.pendingPosition < 0) return
        var restorePosition = root.pendingPosition
        var restorePlayback = root.pendingPlayback
        root.pendingPosition = -1
        root.pendingPlayback = false
        seekAll(restorePosition)
        if (restorePlayback) playAll()
    }
    function seekToSegment(index) {
        if (mediaPlayer.seekable && index >= 0 && index < root.dubbing.segments.length)
            seekAll(root.dubbing.segments[index].startMs)
    }

    function clampRoi(value, low, high) { return Math.max(low, Math.min(high, value)) }
    function commitDubbingOcrRoi() {
        if (!root.dubbing.setDubbingOcrRoi(draftOcrRoi))
            draftOcrRoi = root.dubbing.dubbingOcrRoi
        ocrRoiDragging = false
    }
    function resizeDubbingOcrRoi(mode, point, geometry) {
        var r = draftOcrRoi
        var left = geometry.x + r.x * geometry.width
        var right = left + r.width * geometry.width
        var top = geometry.y + r.y * geometry.height
        var bottom = top + r.height * geometry.height
        var x = clampRoi(point.x, geometry.x, geometry.x + geometry.width)
        var y = clampRoi(point.y, geometry.y, geometry.y + geometry.height)
        var minimum = 18
        if (mode.indexOf("l") !== -1) left = Math.min(x, right - minimum)
        if (mode.indexOf("r") !== -1) right = Math.max(x, left + minimum)
        if (mode.indexOf("t") !== -1) top = Math.min(y, bottom - minimum)
        if (mode.indexOf("b") !== -1) bottom = Math.max(y, top + minimum)
        draftOcrRoi = { x: (left - geometry.x) / geometry.width,
                        y: (top - geometry.y) / geometry.height,
                        width: (right - left) / geometry.width,
                        height: (bottom - top) / geometry.height }
    }
    function moveDubbingOcrRoi(point, grabX, grabY, geometry) {
        var width = draftOcrRoi.width * geometry.width
        var height = draftOcrRoi.height * geometry.height
        var left = clampRoi(point.x - grabX, geometry.x, geometry.x + geometry.width - width)
        var top = clampRoi(point.y - grabY, geometry.y, geometry.y + geometry.height - height)
        draftOcrRoi = { x: (left - geometry.x) / geometry.width,
                        y: (top - geometry.y) / geometry.height,
                        width: draftOcrRoi.width, height: draftOcrRoi.height }
    }

    function qmlSmokeMediaControlsCheck() {
        return controlsAutoHide.qmlSmokeStateCheck()
                && controlsAutoHide.delayMs === 2000
                && subtitleEditorButton.width > 0
                && previewToolbar.width > 0
                && previewToolbar.height === 40
                && previewModeSelector.y >= -1
                && previewModeSelector.y + previewModeSelector.height
                   <= previewToolbar.height + 1
                && subtitlePreviewOverlay.width > 0
                && previewFrame.width > 0
                && previewFrame.height > 0
                && (!root.hasLoadedSource || !sourceSetupPanel.visible)
    }

    // The offscreen route test accepts a production file-picker fixture.  It
    // validates the post-selection layout contract rather than only checking
    // that source setup is present in the source text.
    function qmlSmokeLoadedSourceLayoutCheck() {
        if (!root.hasLoadedSource || sourceSetupPanel.visible || !openVideoButton.visible)
            return false
        var originalFrameMode = root.previewFrameMode
        root.previewFrameMode = "16:9"
        var landscapeRatio = previewFrame.height > 0
                ? previewFrame.width / previewFrame.height : 0
        root.previewFrameMode = "9:16"
        var portraitRatio = previewFrame.height > 0
                ? previewFrame.width / previewFrame.height : 0
        root.previewFrameMode = originalFrameMode
        return Math.abs(landscapeRatio - 16 / 9) < 0.01
                && Math.abs(portraitRatio - 9 / 16) < 0.01
    }

    // Selection can originate from the native file dialog, a downloaded-media
    // row, or an automated preflight Fix action.  Keep the post-selection
    // visual state deterministic instead of relying only on a later QML
    // property-notify turn to collapse the download drawer.
    function collapseSourceSetupAfterSelection() {
        root.sourceSetupExpanded = false
    }

    MediaControlsAutoHide {
        id: controlsAutoHide
        playing: mediaPlayer.playbackState === MediaPlayer.PlayingState
        controlsFocused: previewPlayButton.activeFocus || previewMuteButton.activeFocus
    }

    Connections {
        target: root.dubbing
        function onProjectChanged() {
            if (!root.ocrRoiDragging)
                root.draftOcrRoi = root.dubbing.dubbingOcrRoi
        }
    }

    onSourceMediaPathChanged: {
        // A newly selected/downloaded source should immediately receive the
        // canvas. Re-opening source setup remains an explicit user action.
        root.sourceSetupExpanded = !root.hasLoadedSource
    }
    onPreviewFocusModeChanged: {
        // A focused canvas must never be squeezed by optional download controls.
        if (root.previewFocusMode)
            root.sourceSetupExpanded = false
    }

    MediaPlayer {
        id: mediaPlayer
        source: root.showingDubbedMedia ? root.dubbing.playbackMediaUrl : root.dubbing.sourceMediaUrl
        audioOutput: AudioOutput {
            id: sourceAudioOutput
            volume: root.showingDubbedMedia || root.previewMuted ? 0 : 1
        }
        videoOutput: videoOutput
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.LoadedMedia || mediaStatus === MediaPlayer.BufferedMedia)
                root.restoreAfterSourceSwitch()
        }
    }
    MediaPlayer {
        id: vocalPlayer
        source: root.localMediaUrl(root.dubbing.dubbedVocalPath)
        audioOutput: AudioOutput {
            volume: root.showingDubbedMedia && !root.previewMuted ? root.vocalLevel : 0
        }
    }
    MediaPlayer {
        id: backgroundPlayer
        source: root.localMediaUrl(root.dubbing.backgroundPath)
        audioOutput: AudioOutput {
            // The rendered mix uses 35% background gain. A 100% slider value
            // therefore reproduces the rendered/exported balance.
            volume: root.showingDubbedMedia && !root.previewMuted
                    ? root.backgroundLevel * 0.35 : 0
        }
    }

    Connections {
        target: mediaPlayer
        function onSubtitleTracksChanged() {
            if (mediaPlayer.subtitleTracks.length > 0)
                mediaPlayer.activeSubtitleTrack = 0
        }
        function onPositionChanged() {
            if (mediaPlayer.playbackState !== MediaPlayer.PlayingState) return
            for (var i = 0; i < root.dubbing.segments.length; ++i) {
                var segment = root.dubbing.segments[i]
                if (mediaPlayer.position >= segment.startMs && mediaPlayer.position <= segment.endMs) {
                    if (root.selectedSegment !== i) {
                        root.selectedSegment = i
                        root.segmentSelected(i)
                    }
                    break
                }
            }
        }
    }
    onHasDubbedPreviewChanged: {
        if (root.hasDubbedPreview)
            root.switchPreviewMode("dubbed")
        else
            root.switchPreviewMode("source")
    }
    Component.onCompleted: {
        root.previewMode = root.hasDubbedPreview ? "dubbed" : "source"
        root.sourceSetupExpanded = !root.hasLoadedSource
    }
    Timer {
        id: sourceSwitchFallback
        interval: 120
        onTriggered: {
            if (mediaPlayer.duration > 0
                || mediaPlayer.mediaStatus === MediaPlayer.LoadedMedia
                || mediaPlayer.mediaStatus === MediaPlayer.BufferedMedia
                || root.sourceSwitchAttempts >= 20) {
                root.restoreAfterSourceSwitch()
                return
            }
            root.sourceSwitchAttempts += 1
            restart()
        }
    }
    Timer {
        interval: 500
        repeat: true
        running: root.showingDubbedMedia
                 && mediaPlayer.playbackState === MediaPlayer.PlayingState
        onTriggered: {
            if (vocalPlayer.seekable && Math.abs(vocalPlayer.position - mediaPlayer.position) > 180)
                vocalPlayer.position = mediaPlayer.position
            if (backgroundPlayer.seekable && Math.abs(backgroundPlayer.position - mediaPlayer.position) > 180)
                backgroundPlayer.position = mediaPlayer.position
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.paddingMedium
        spacing: Theme.paddingSmall
        // All preview controls share one horizontal editor toolbar.  Keeping
        // Original/Dubbed on a second row made the preview feel detached and
        // permanently consumed vertical space.  The one toolbar scrolls as a
        // unit on narrow canvases rather than wrapping or painting controls
        // over the video frame.
        Flickable {
            id: previewToolbar
            objectName: "dubbingPreviewToolbar"
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            Layout.minimumHeight: 40
            contentWidth: previewToolbarRow.implicitWidth
            contentHeight: height
            clip: true
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds

            Row {
                id: previewToolbarRow
                height: previewToolbar.height
                spacing: Theme.paddingSmall
                Text {
                    width: implicitWidth
                    height: previewToolbar.height
                    text: qsTr("VIDEO PREVIEW")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    font.bold: true
                    // A compact title leaves room for actual editing controls.
                    font.letterSpacing: 0.25
                    verticalAlignment: Text.AlignVCenter
                }
                Button {
                    id: subtitleEditorButton
                    objectName: "dubbingSubtitleEditorButton"
                    height: previewToolbar.height
                    text: qsTr("Subtitles")
                    enabled: root.dubbing.hasProject && !root.dubbing.processing
                    onClicked: root.subtitleEditorRequested()
                }
                Button {
                    id: sourceSetupToggle
                    objectName: "dubbingSourceSetupToggle"
                    height: previewToolbar.height
                    text: root.sourceSetupExpanded ? qsTr("Hide source setup") : qsTr("Change / download source")
                    enabled: !root.dubbing.mediaQueueProcessing
                    onClicked: {
                        if (root.previewFocusMode)
                            root.previewFocusRequested(false)
                        root.sourceSetupExpanded = !root.sourceSetupExpanded
                    }
                }
                PrimaryButton {
                    id: openVideoButton
                    objectName: "dubbingOpenVideoButton"
                    height: previewToolbar.height
                    text: root.hasLoadedSource ? qsTr("Replace video") : qsTr("Open video")
                    iconName: "folder"
                    quiet: true
                    enabled: !root.dubbing.processing && !root.dubbing.linkImporting
                    toolTip: qsTr("Choose a local video or audio file. Choosing a source closes download setup and restores the canvas.")
                    onClicked: root.browseRequested()
                }
                Button {
                    id: previewFocusToggle
                    objectName: "dubbingPreviewFocusToggle"
                    height: previewToolbar.height
                    text: root.previewFocusMode ? qsTr("Exit video focus") : qsTr("Focus video")
                    enabled: root.isVideoSource
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Give the video canvas the central workspace; source setup stays available from Change / download source.")
                    onClicked: {
                        if (!root.previewFocusMode)
                            root.sourceSetupExpanded = false
                        root.previewFocusRequested(!root.previewFocusMode)
                    }
                }
                AppComboBox {
                    id: previewFrameModeSelector
                    objectName: "dubbingPreviewFrameModeSelector"
                    width: 112
                    height: previewToolbar.height
                    model: [
                        { text: qsTr("Fit source"), value: "source" },
                        { text: qsTr("16:9"), value: "16:9" },
                        { text: qsTr("9:16"), value: "9:16" },
                        { text: qsTr("1:1"), value: "1:1" }
                    ]
                    textRole: "text"
                    currentIndex: {
                        for (var index = 0; index < model.length; ++index)
                            if (model[index].value === root.previewFrameMode)
                                return index
                        return 0
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Preview frame ratio. The source video remains uncropped and unstretched.")
                    onActivated: function(index) {
                        if (index >= 0 && index < model.length)
                            root.previewFrameMode = model[index].value
                    }
                }
                Rectangle {
                    id: previewModeSelector
                    objectName: "dubbingPreviewModeSelector"
                    width: 206
                    height: 32
                    anchors.verticalCenter: parent.verticalCenter
                    radius: Theme.radiusSmall
                    color: Qt.rgba(0, 0, 0, 0.18)
                    border.color: Qt.rgba(1, 1, 1, 0.08)
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 3
                        spacing: 3
                        PreviewModeButton {
                            Layout.fillWidth: true
                            text: qsTr("Original")
                            iconName: "file"
                            selected: !root.showingDubbedMedia
                            enabled: root.dubbing.sourceMediaPath.length > 0
                            onClicked: root.switchPreviewMode("source")
                        }
                        PreviewModeButton {
                            Layout.fillWidth: true
                            text: qsTr("Dubbed")
                            iconName: "mic"
                            selected: root.showingDubbedMedia
                            enabled: root.hasDubbedPreview
                            onClicked: root.switchPreviewMode("dubbed")
                        }
                    }
                }
                Text {
                    width: 104
                    height: previewToolbar.height
                    text: root.showingDubbedMedia
                          ? qsTr("Dubbed mix")
                          : (root.dubbing.sourceMediaPath.length > 0 ? qsTr("Original audio") : qsTr("No media"))
                    color: root.dubbing.sourceMediaPath.length > 0 ? Theme.success : Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }

            ScrollBar.horizontal: ScrollBar {
                policy: previewToolbar.contentWidth > previewToolbar.width
                        ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Qt.rgba(1, 1, 1, 0.07) }

        // Show source setup by default only until a source exists. Keeping it
        // expanded after a video loads squeezed the preview to a thin strip,
        // which made OCR region editing impractical.
        ScrollView {
            id: sourceSetupPanel
            objectName: "dubbingSourceSetupScrollView"
            Layout.fillWidth: true
            visible: !root.hasLoadedSource || root.sourceSetupExpanded
            Layout.minimumHeight: 0
            Layout.maximumHeight: root.sourceSetupMaximumHeight
            Layout.preferredHeight: visible
                                  ? Math.min(sourceSetupContent.implicitHeight,
                                             root.sourceSetupMaximumHeight)
                                  : 0
            clip: true
            contentWidth: availableWidth
            contentHeight: sourceSetupContent.implicitHeight
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: contentHeight > height
                                       ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff

            ColumnLayout {
                id: sourceSetupContent
                width: sourceSetupPanel.availableWidth
                spacing: Theme.paddingSmall
                TextArea {
                    id: directMediaLink
                    Layout.fillWidth: true
                    Layout.minimumHeight: 82
                enabled: !root.dubbing.mediaQueueProcessing
                placeholderText: qsTr("Queue direct media, YouTube, TikTok, or Douyin links — one public link per line")
                color: Theme.textPrimary
                placeholderTextColor: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                selectByMouse: true
                wrapMode: TextEdit.WrapAnywhere
                leftPadding: Theme.paddingMedium; rightPadding: Theme.paddingMedium
                topPadding: Theme.paddingSmall; bottomPadding: Theme.paddingSmall
                background: Rectangle { radius: Theme.radiusSmall; color: Qt.rgba(1, 1, 1, 0.035); border.color: directMediaLink.activeFocus ? Theme.accent : Qt.rgba(1, 1, 1, 0.09); border.width: directMediaLink.activeFocus ? 2 : 1 }
            }
                RowLayout {
                    Layout.fillWidth: true
                    PrimaryButton {
                    id: addQueueLinksButton
                    text: qsTr("Add link(s) to download queue")
                    iconName: "download"
                    quiet: true
                    enabled: directMediaLink.text.trim().length > 0 && !root.dubbing.mediaQueueProcessing
                    onClicked: {
                        root.mediaQueueRequested(directMediaLink.text)
                        directMediaLink.clear()
                    }
                }
                    PrimaryButton {
                    id: openMediaQueueButton
                    objectName: "dubbingOpenMediaQueueButton"
                    text: qsTr("Downloaded media & actions")
                    iconName: "workflow"
                    quiet: true
                    enabled: !root.dubbing.mediaQueueProcessing
                    toolTip: qsTr("Choose any downloaded videos and run one action when you decide")
                    onClicked: mediaQueueDialog.open()
                }
                    PrimaryButton {
                    visible: root.dubbing.mediaQueueDownloading || root.dubbing.mediaQueueProcessing
                    text: qsTr("Cancel queue")
                    iconName: "close"
                    quiet: true
                    onClicked: root.dubbing.cancelMediaQueue()
                }
                    Item { Layout.fillWidth: true }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.paddingSmall
                    Text {
                    Layout.fillWidth: true
                    text: root.dubbing.douyinCookieConfigured
                          ? qsTr("Douyin cookies: %1 (used for this download run only)").arg(root.dubbing.douyinCookieFileName)
                          : qsTr("Douyin cookies are optional; use a fresh Netscape cookie file only when Douyin requires it.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideMiddle
                }
                    PrimaryButton {
                    text: qsTr("Choose Douyin cookies")
                    iconName: "folder"
                    quiet: true
                    enabled: !root.dubbing.mediaQueueDownloading && !root.dubbing.mediaQueueProcessing
                    onClicked: douyinCookieFileDialog.open()
                }
                    PrimaryButton {
                    visible: root.dubbing.douyinCookieConfigured
                    text: qsTr("Clear")
                    iconName: "close"
                    quiet: true
                    enabled: !root.dubbing.mediaQueueDownloading && !root.dubbing.mediaQueueProcessing
                    onClicked: root.dubbing.clearDouyinCookieFile()
                }
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: browserSessionLayout.implicitHeight + Theme.paddingMedium * 2
                radius: Theme.radiusSmall
                color: Qt.rgba(0.45, 0.20, 1.0, 0.08)
                border.color: Qt.rgba(0.55, 0.35, 1.0, 0.35)
                border.width: 1
                    ColumnLayout {
                        id: browserSessionLayout
                        anchors.fill: parent
                        anchors.margins: Theme.paddingMedium
                        spacing: Theme.paddingSmall
                        Text {
                        text: qsTr("Managed Chromium session for Douyin")
                        color: Theme.textPrimary
                        font.bold: true
                    }
                        Text {
                        Layout.fillWidth: true
                        text: root.dubbing.douyinBrowserVerified
                              ? qsTr("Verified. Douyin downloads use this app-owned profile and page JavaScript.")
                              : qsTr("Optional separate profile for Douyin pages that reject yt-dlp cookies. Chrome/Edge cookies are never read.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                    }
                        Flow {
                        Layout.fillWidth: true
                        spacing: Theme.paddingSmall
                        PrimaryButton {
                            objectName: "dubbingDouyinChromiumSetupButton"
                            text: qsTr("Set up Chromium")
                            iconName: "folder"
                            quiet: true
                            enabled: root.dubbing.douyinBrowserAvailable && !root.dubbing.douyinBrowserBusy
                                     && !root.dubbing.mediaQueueDownloading && !root.dubbing.mediaQueueProcessing
                            onClicked: root.dubbing.openDouyinBrowserSession()
                        }
                        PrimaryButton {
                            text: qsTr("Check connection")
                            iconName: "play"
                            quiet: true
                            enabled: root.dubbing.douyinBrowserAvailable && root.dubbing.douyinBrowserConfigured
                                     && !root.dubbing.douyinBrowserBusy && !root.dubbing.mediaQueueDownloading
                                     && !root.dubbing.mediaQueueProcessing
                            onClicked: root.dubbing.checkDouyinBrowserSession()
                        }
                        PrimaryButton {
                            visible: root.dubbing.douyinBrowserVerified
                            text: qsTr("Disable")
                            iconName: "close"
                            quiet: true
                            onClicked: root.dubbing.disconnectDouyinBrowserSession()
                        }
                    }
                        Text {
                        Layout.fillWidth: true
                        visible: root.dubbing.douyinBrowserStatus !== ""
                        text: root.dubbing.douyinBrowserStatus
                        color: root.dubbing.douyinBrowserVerified ? Theme.success : Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                    }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: root.dubbing.mediaQueueItems.length > 0
                    text: qsTr("%1 item(s) in the download library. Download first; later open Downloaded media & actions to choose a separate subset for Import, STT, Translate, TTS, or Export.")
                          .arg(root.dubbing.mediaQueueItems.length)
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
            }
        }
        Text {
            Layout.fillWidth: true
            visible: false
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSmall
            text: {
                var status = root.dubbing.linkImportStatus || qsTr("Downloading media")
                var received = root.dubbing.linkImportReceivedBytes
                var total = root.dubbing.linkImportTotalBytes
                return total > 0 ? status + " — " + root.formatBytes(received) + " / " + root.formatBytes(total)
                                 : status + (received > 0 ? " — " + root.formatBytes(received) : "")
            }
        }

        Text {
            Layout.fillWidth: true
            visible: root.dubbing.mediaQueueDownloading || root.dubbing.mediaQueueProcessing
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSmall
            text: root.dubbing.mediaQueueStatus
        }

        Rectangle {
            id: previewSurface
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Preserve a practical canvas for 16:9 video and OCR handles even
            // on high-DPI displays. The surrounding panel may grow further.
            Layout.minimumHeight: root.isVideoSource
                                  ? Math.min(520, Math.max(400, width * 0.50))
                                  : 220
            Layout.preferredHeight: root.isVideoSource
                                    ? Math.min(660, Math.max(500, width * 0.58))
                                    : 260
            radius: Theme.radiusSmall
            color: Qt.rgba(0, 0, 0, 0.30)
            border.color: Qt.rgba(1, 1, 1, 0.06)
            border.width: 1
            clip: true

            Column {
                anchors.centerIn: parent
                width: parent.width - Theme.paddingXL * 2
                spacing: Theme.paddingSmall
                visible: root.dubbing.sourceMediaPath.length === 0
                LineIcon { anchors.horizontalCenter: parent.horizontalCenter; name: "folder"; color: Theme.accentLight; width: 38; height: 38 }
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("Add source media"); color: Theme.textPrimary; font.pixelSize: Theme.fontMedium; font.bold: true; width: parent.width; horizontalAlignment: Text.AlignHCenter }
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("WAV, MP3, MP4 or MKV"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
            }
            Item {
                id: previewFrame
                objectName: "dubbingPreviewFrame"
                anchors.centerIn: parent
                width: root.previewFrameAspectRatio > 0
                       ? Math.min(previewSurface.width,
                                  previewSurface.height * root.previewFrameAspectRatio)
                       : previewSurface.width
                height: root.previewFrameAspectRatio > 0
                        ? Math.min(previewSurface.height,
                                   previewSurface.width / root.previewFrameAspectRatio)
                        : previewSurface.height

                Rectangle {
                    anchors.fill: parent
                    color: "#11121a"
                    radius: Theme.radiusSmall
                    visible: root.isVideoSource
                }
                VideoOutput {
                    id: videoOutput
                    anchors.fill: parent
                    visible: root.isVideoSource
                    fillMode: VideoOutput.PreserveAspectFit
                }
            }
            Rectangle {
                id: dubbingOcrRoiOverlay
                objectName: "dubbingSubtitleOcrRoiOverlay"
                readonly property rect content: Qt.rect(previewFrame.x + videoOutput.contentRect.x,
                                                        previewFrame.y + videoOutput.contentRect.y,
                                                        videoOutput.contentRect.width,
                                                        videoOutput.contentRect.height)
                visible: root.isVideoSource && root.dubbing.dubbingOcrRoiVisible
                         && content.width > 0 && content.height > 0
                x: content.x + root.draftOcrRoi.x * content.width
                y: content.y + root.draftOcrRoi.y * content.height
                width: root.draftOcrRoi.width * content.width
                height: root.draftOcrRoi.height * content.height
                color: root.ocrRoiEditMode
                       ? Qt.rgba(0.45, 0.20, 1.0, 0.22)
                       : Qt.rgba(0.45, 0.20, 1.0, 0.11)
                border.color: root.ocrRoiEditMode ? Theme.accentLight : Theme.primary
                border.width: root.ocrRoiEditMode ? 3 : 2
                z: 8
                MouseArea {
                    anchors.fill: parent
                    property real grabX: 0
                    property real grabY: 0
                    enabled: root.ocrRoiEditMode && !root.dubbing.processing
                    preventStealing: true
                    cursorShape: Qt.SizeAllCursor
                    onPressed: function(mouse) { grabX = mouse.x; grabY = mouse.y; root.ocrRoiDragging = true }
                    onPositionChanged: function(mouse) {
                        if (pressed) root.moveDubbingOcrRoi(parent.mapToItem(dubbingOcrRoiOverlay.parent, mouse.x, mouse.y),
                                                             grabX, grabY, dubbingOcrRoiOverlay.content)
                    }
                    onReleased: root.commitDubbingOcrRoi()
                    onCanceled: root.commitDubbingOcrRoi()
                }
                Rectangle {
                    visible: root.ocrRoiEditMode || root.dubbing.processing
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: 4
                    implicitWidth: scanAreaLabel.implicitWidth + 12
                    implicitHeight: scanAreaLabel.implicitHeight + 6
                    radius: 5
                    color: root.dubbing.processing ? Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.92)
                                                   : Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.92)
                    Text {
                        id: scanAreaLabel
                        anchors.centerIn: parent
                        text: root.dubbing.processing ? qsTr("OCR scan area locked while running")
                                                       : qsTr("Drag scan area · resize handles")
                        color: Theme.textPrimary
                        font.pixelSize: 10
                        font.bold: true
                    }
                }
                Repeater {
                    model: [ { key: "tl", x: 0, y: 0 }, { key: "tr", x: 1, y: 0 },
                             { key: "bl", x: 0, y: 1 }, { key: "br", x: 1, y: 1 },
                             { key: "l", x: 0, y: 0.5 }, { key: "r", x: 1, y: 0.5 },
                             { key: "t", x: 0.5, y: 0 }, { key: "b", x: 0.5, y: 1 } ]
                    delegate: Rectangle {
                        objectName: "dubbingSubtitleOcrRoiHandle_" + modelData.key
                        visible: root.ocrRoiEditMode && !root.dubbing.processing
                        width: 16; height: 16; radius: 8
                        x: modelData.x * parent.width - width / 2
                        y: modelData.y * parent.height - height / 2
                        color: Theme.primary; border.color: Theme.textPrimary; border.width: 1
                        z: 2
                        MouseArea {
                            anchors.fill: parent
                            enabled: !root.dubbing.processing
                            preventStealing: true
                            cursorShape: Qt.SizeFDiagCursor
                            onPressed: root.ocrRoiDragging = true
                            onPositionChanged: function(mouse) {
                                if (pressed) root.resizeDubbingOcrRoi(modelData.key,
                                    parent.mapToItem(dubbingOcrRoiOverlay.parent, mouse.x, mouse.y),
                                    dubbingOcrRoiOverlay.content)
                            }
                            onReleased: root.commitDubbingOcrRoi()
                            onCanceled: root.commitDubbingOcrRoi()
                        }
                    }
                }
            }
            FontLoader {
                id: subtitlePreviewFont
                source: root.subtitleStyle.fontFile || ""
            }
            Rectangle {
                id: subtitlePreviewOverlay
                objectName: "dubbingSubtitlePreviewOverlay"
                readonly property string alignment: root.subtitleStyle.alignment || "bottom"
                readonly property real safeMargin: Number(root.subtitleStyle.safeMargin || 0.06)
                visible: root.isVideoSource && root.activeSubtitleText.length > 0
                width: Math.max(80, previewFrame.width * Number(root.subtitleStyle.maxWidth || 0.82))
                height: subtitlePreviewText.implicitHeight + Theme.paddingSmall * 2
                x: alignment === "custom"
                   ? previewFrame.x + previewFrame.width * Number(root.subtitleStyle.positionX || 0.5) - width / 2
                   : previewFrame.x + (previewFrame.width - width) / 2
                y: alignment === "top" ? previewFrame.y + previewFrame.height * safeMargin
                  : alignment === "custom" ? previewFrame.y + previewFrame.height * Number(root.subtitleStyle.positionY || 0.90) - height / 2
                                             : previewFrame.y + previewFrame.height - height - previewFrame.height * safeMargin - previewControls.height
                radius: Theme.radiusSmall
                color: "transparent"
                z: 2
                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    color: root.subtitleStyle.backgroundColor || "#00000000"
                    opacity: Math.max(0, Math.min(1, Number(root.subtitleStyle.backgroundOpacity || 0)))
                }
                Text {
                    id: subtitlePreviewShadow
                    anchors.fill: parent
                    anchors.leftMargin: Theme.paddingSmall + Number(root.subtitleStyle.shadowOffset || 0)
                    anchors.rightMargin: Theme.paddingSmall - Number(root.subtitleStyle.shadowOffset || 0)
                    anchors.topMargin: Theme.paddingSmall + Number(root.subtitleStyle.shadowOffset || 0)
                    anchors.bottomMargin: Theme.paddingSmall - Number(root.subtitleStyle.shadowOffset || 0)
                    text: root.activeSubtitleText
                    color: root.subtitleStyle.shadowColor || "#99000000"
                    font.family: subtitlePreviewFont.status === FontLoader.Ready ? subtitlePreviewFont.name : (root.subtitleStyle.fontFamily || "Arial")
                    font.pixelSize: Number(root.subtitleStyle.fontSize || 42)
                    font.weight: Number(root.subtitleStyle.fontWeight || 600)
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.WordWrap; lineHeight: Number(root.subtitleStyle.lineSpacing || 1.0); lineHeightMode: Text.ProportionalHeight
                }
                Text {
                    id: subtitlePreviewText
                    anchors.fill: parent
                    anchors.margins: Theme.paddingSmall
                    text: root.activeSubtitleText
                    color: root.subtitleStyle.textColor || "#FFFFFFFF"
                    font.family: subtitlePreviewFont.status === FontLoader.Ready ? subtitlePreviewFont.name : (root.subtitleStyle.fontFamily || "Arial")
                    font.pixelSize: Number(root.subtitleStyle.fontSize || 42)
                    font.weight: Number(root.subtitleStyle.fontWeight || 600)
                    style: Text.Outline
                    styleColor: root.subtitleStyle.outlineColor || "#D9000000"
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.WordWrap; lineHeight: Number(root.subtitleStyle.lineSpacing || 1.0); lineHeightMode: Text.ProportionalHeight
                }
            }
            Rectangle {
                anchors.fill: parent
                color: Qt.rgba(0.06, 0.06, 0.09, 0.95)
                visible: root.dubbing.sourceMediaPath.length > 0 && !root.isVideoSource
                Column {
                    anchors.centerIn: parent
                    width: parent.width - Theme.paddingXL * 2
                    spacing: Theme.paddingSmall
                    LineIcon { anchors.horizontalCenter: parent.horizontalCenter; name: "volume"; color: Theme.accentLight; width: 42; height: 42 }
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: root.dubbing.sourceMediaPath.split(/[\\/]/).pop(); color: Theme.textPrimary; font.pixelSize: Theme.fontMedium; font.bold: true; elide: Text.ElideMiddle; width: parent.width; horizontalAlignment: Text.AlignHCenter }
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("Audio track playing"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                }
            }
            MouseArea { anchors.fill: parent; enabled: root.dubbing.sourceMediaPath.length === 0 && !root.dubbing.linkImporting; cursorShape: Qt.PointingHandCursor; onClicked: root.browseRequested() }

            HoverHandler {
                id: previewHoverHandler
                enabled: root.dubbing.sourceMediaPath.length > 0
                onHoveredChanged: controlsAutoHide.pointerInsideSurface = hovered
            }
            Rectangle {
                id: previewControls
                objectName: "dubbingSharedMediaControls"
                anchors.left: previewFrame.left; anchors.right: previewFrame.right; anchors.bottom: previewFrame.bottom
                height: 44
                visible: root.dubbing.sourceMediaPath.length > 0 && (opacity > 0 || controlsAutoHide.controlsVisible)
                opacity: controlsAutoHide.controlsVisible ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 250 } }
                gradient: Gradient {
                    GradientStop { position: 0; color: "transparent" }
                    GradientStop { position: 1; color: Qt.rgba(0.06, 0.06, 0.09, 0.92) }
                }

                Item {
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.topMargin: -8
                    height: 16
                    Rectangle {
                        anchors.fill: parent
                        anchors.topMargin: 6
                        anchors.bottomMargin: 6
                        color: Qt.rgba(255, 255, 255, 0.2)
                        Rectangle {
                            anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                            color: Theme.accentLight
                            width: mediaPlayer.duration > 0 ? mediaPlayer.position / mediaPlayer.duration * parent.width : 0
                        }
                    }
                    MouseArea {
                        id: seekArea
                        anchors.fill: parent
                        property bool wasPlaying: false
                        function updatePosition(x) { if (mediaPlayer.duration > 0) root.seekAll(Math.max(0, Math.min(1, x / width)) * mediaPlayer.duration) }
                        onPressed: {
                            controlsAutoHide.interactionActive = true
                            controlsAutoHide.noteInteraction()
                            wasPlaying = mediaPlayer.playbackState === MediaPlayer.PlayingState
                            if (wasPlaying) mediaPlayer.pause()
                            updatePosition(mouseX)
                        }
                        onPositionChanged: if (pressed) {
                            updatePosition(mouseX)
                            controlsAutoHide.noteInteraction()
                        }
                        onReleased: {
                            if (wasPlaying) root.playAll()
                            controlsAutoHide.interactionActive = false
                            controlsAutoHide.noteInteraction()
                        }
                    }
                }
                RowLayout {
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    anchors.leftMargin: Theme.paddingMedium; anchors.rightMargin: Theme.paddingMedium
                    height: 36
                    spacing: Theme.paddingMedium
                    Button {
                        id: previewPlayButton
                        implicitWidth: 28; implicitHeight: 28
                        flat: true
                        contentItem: LineIcon { anchors.centerIn: parent; name: mediaPlayer.playbackState === MediaPlayer.PlayingState ? "pause" : "play"; color: Theme.textPrimary; width: 14; height: 14 }
                        onClicked: {
                            mediaPlayer.playbackState === MediaPlayer.PlayingState ? root.pauseAll() : root.playAll()
                            controlsAutoHide.noteInteraction()
                        }
                    }
                    Button {
                        id: previewMuteButton
                        implicitWidth: 28; implicitHeight: 28
                        flat: true
                        contentItem: LineIcon { anchors.centerIn: parent; name: "volume"; color: root.previewMuted ? Theme.textSecondary : Theme.textPrimary; width: 14; height: 14 }
                        onClicked: {
                            root.previewMuted = !root.previewMuted
                            controlsAutoHide.noteInteraction()
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Text { text: "%1 / %2".arg(root.formatTime(mediaPlayer.position)).arg(root.formatTime(mediaPlayer.duration)); color: Theme.textSecondary; font.pixelSize: 11; font.family: "Monospace" }
                }
            }
        }
        Flow {
            Layout.fillWidth: true
            visible: root.dubbing.dubbingOcrRoiVisible
            spacing: Theme.paddingSmall
            Button {
                text: root.ocrRoiEditMode ? qsTr("Done editing scan area") : qsTr("Edit OCR scan area")
                enabled: !root.dubbing.processing && root.isVideoSource
                onClicked: {
                    // OCR editing always prioritizes the canvas over optional
                    // download controls, so the complete scan box is visible.
                    root.sourceSetupExpanded = false
                    root.ocrRoiEditMode = !root.ocrRoiEditMode
                }
            }
            Button { text: qsTr("Preset lower region"); enabled: !root.dubbing.processing && root.isVideoSource; onClicked: root.dubbing.presetDubbingOcrLowerRegion() }
            Button { text: qsTr("Reset region"); enabled: !root.dubbing.processing && root.isVideoSource; onClicked: root.dubbing.resetDubbingOcrRoi() }
            Button { text: qsTr("Preview crop"); enabled: !root.dubbing.processing && root.isVideoSource; onClicked: root.dubbing.previewDubbingOcrCrop(mediaPlayer.position) }
            Text { visible: root.isVideoSource; text: root.dubbing.processing ? qsTr("OCR scan area is locked while Transcribe runs.") : (root.ocrRoiEditMode ? qsTr("Drag the purple area to move it; drag a round handle to resize it.") : qsTr("Click Edit OCR scan area before moving or resizing the scan box.")); color: root.dubbing.processing ? Theme.warning : Theme.textSecondary; topPadding: 7; font.pixelSize: 10 }
            Text { visible: root.isVideoSource; text: qsTr("ROI: x %1, y %2, w %3, h %4").arg(Number(root.draftOcrRoi.x).toFixed(3)).arg(Number(root.draftOcrRoi.y).toFixed(3)).arg(Number(root.draftOcrRoi.width).toFixed(3)).arg(Number(root.draftOcrRoi.height).toFixed(3)); color: Theme.textSecondary; topPadding: 7; font.pixelSize: 10 }
            Text { visible: !root.isVideoSource; text: qsTr("Choose a video to set the OCR scan region."); color: Theme.textSecondary; topPadding: 7; font.pixelSize: 10 }
        }
        Rectangle {
            Layout.fillWidth: true
            visible: root.dubbing.dubbingOcrRoiVisible
                     && AppController.subtitleOcr.cropPreviewUrl.toString() !== ""
            Layout.preferredHeight: visible ? 180 : 0
            color: Theme.surfaceAlt
            radius: Theme.radiusSmall
            Image {
                anchors.fill: parent
                anchors.margins: Theme.paddingSmall
                source: AppController.subtitleOcr.cropPreviewUrl
                fillMode: Image.PreserveAspectFit
                asynchronous: true
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            visible: root.showingDubbedMedia
            radius: Theme.radiusSmall
            color: Qt.rgba(1, 1, 1, 0.025)
            border.color: Qt.rgba(1, 1, 1, 0.07)

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.paddingMedium
                anchors.rightMargin: Theme.paddingMedium
                spacing: Theme.paddingMedium
                LineIcon {
                    name: "volume"
                    color: Theme.accentLight
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                }
                Text {
                    text: qsTr("MIX")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.bold: true
                    font.letterSpacing: 1
                }
                MixerControl {
                    Layout.fillWidth: true
                    label: qsTr("Vocal")
                    value: root.vocalLevel
                    onMoved: root.vocalLevel = value
                }
                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 30; color: Qt.rgba(1, 1, 1, 0.08) }
                MixerControl {
                    Layout.fillWidth: true
                    label: qsTr("Background")
                    value: root.backgroundLevel
                    enabled: root.dubbing.backgroundPath.length > 0
                    onMoved: root.backgroundLevel = value
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            FieldProxy { Layout.fillWidth: true; text: root.dubbing.sourceMediaPath; placeholderText: qsTr("Media file path") }
            PrimaryButton { text: qsTr("Browse"); iconName: "folder"; quiet: true; enabled: !root.dubbing.processing && !root.dubbing.linkImporting; onClicked: root.browseRequested() }
        }
    }

    component FieldProxy: TextField {
        color: Theme.textPrimary
        placeholderTextColor: Theme.textSecondary
        font.pixelSize: Theme.fontSmall
        selectByMouse: true
        readOnly: true
        leftPadding: Theme.paddingMedium
        rightPadding: Theme.paddingMedium
        background: Rectangle { radius: Theme.radiusSmall; color: Qt.rgba(1, 1, 1, 0.035); border.color: parent.activeFocus ? Theme.accent : Qt.rgba(1, 1, 1, 0.09); border.width: parent.activeFocus ? 2 : 1 }
    }

    component PreviewModeButton: Button {
        id: modeButton
        property bool selected: false
        property string iconName: ""
        implicitHeight: 26
        padding: 0
        contentItem: Row {
            anchors.centerIn: parent
            spacing: 5
            LineIcon {
                name: modeButton.iconName
                color: modeButton.enabled
                       ? (modeButton.selected ? Theme.textPrimary : Theme.textSecondary)
                       : Qt.rgba(0.56, 0.56, 0.69, 0.42)
                width: 13
                height: 13
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: modeButton.text
                color: modeButton.enabled
                       ? (modeButton.selected ? Theme.textPrimary : Theme.textSecondary)
                       : Qt.rgba(0.56, 0.56, 0.69, 0.42)
                font.pixelSize: 11
                font.bold: modeButton.selected
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        background: Rectangle {
            radius: 6
            color: modeButton.selected ? Theme.surfaceAlt
                                       : (modeButton.hovered ? Qt.rgba(1, 1, 1, 0.04) : "transparent")
            border.color: modeButton.selected ? Qt.rgba(0.64, 0.49, 1, 0.5) : "transparent"
        }
        HoverHandler { cursorShape: modeButton.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
    }

    component MixerControl: RowLayout {
        id: mixerControl
        property string label: ""
        property alias value: levelSlider.value
        signal moved()
        spacing: Theme.paddingSmall
        Text {
            text: mixerControl.label
            color: mixerControl.enabled ? Theme.textPrimary : Theme.textSecondary
            font.pixelSize: 11
            Layout.preferredWidth: 66
        }
        ParameterSlider {
            id: levelSlider
            Layout.fillWidth: true
            Layout.minimumWidth: 72
            from: 0
            to: 1
            value: 1
            stepSize: 0.01
            enabled: mixerControl.enabled
            onMoved: mixerControl.moved()
        }
        Text {
            text: Math.round(levelSlider.value * 100) + "%"
            color: mixerControl.enabled ? Theme.textSecondary : Qt.rgba(0.56, 0.56, 0.69, 0.5)
            font.pixelSize: 10
            font.family: "Monospace"
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: 34
        }
    }

    DubbingMediaQueueDialog {
        id: mediaQueueDialog
        dubbing: root.dubbing
    }

    FileDialog {
        id: douyinCookieFileDialog
        title: qsTr("Choose a fresh Douyin Netscape cookie file")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Netscape cookie files (*.txt *.cookies)"), qsTr("All files (*)")]
        onAccepted: root.dubbing.setDouyinCookieFile(AppController.files.urlToLocalPath(selectedFile.toString()))
    }
}
