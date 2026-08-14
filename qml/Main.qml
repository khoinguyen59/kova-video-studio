import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LAStudio

ApplicationWindow {
    id: root

    width: AppController.settings.windowWidth
    height: AppController.settings.windowHeight
    x: AppController.settings.windowX
    y: AppController.settings.windowY
    minimumWidth: 960
    minimumHeight: 600
    // A QML-only frameless window cannot participate fully in Windows' non-client
    // area behaviour (snap layouts, Aero Snap and the system shadow).  Keep the
    // custom chrome on other platforms, but let Windows own the frame instead.
    readonly property bool usesNativeWindowFrame: Qt.platform.os === "windows"
    flags: Qt.Window | (usesNativeWindowFrame ? 0 : Qt.FramelessWindowHint)
    readonly property string appName: Qt.application.name && Qt.application.name.length > 0 ? Qt.application.name : "LA Studio"
    readonly property string appVersion: Qt.application.version
    property string dismissedUpdateVersion: ""
    property bool restoringWindowPlacement: true
    property int qmlSmokeSubtitleLayoutSizeIndex: 0
    property bool qmlSmokeSubtitleLayoutResizePending: false
    property int qmlSmokeHomeLayoutSizeIndex: 0
    property bool qmlSmokeHomeLayoutResizePending: false
    property int qmlSmokeDubbingLayoutSizeIndex: 0
    property bool qmlSmokeDubbingLayoutResizePending: false
    property bool qmlSmokeDubbingAutomaticPending: false
    property string qmlSmokeMediaPath: ""
    property string qmlSmokeProjectUrl: ""
    property bool qmlSmokeProjectGatePending: false
    property var qmlSmokeDubbingTrace: []
    property bool qmlSmokeFailed: false
    property int qmlSmokeVoiceCloneLayoutSizeIndex: 0
    property bool qmlSmokeVoiceCloneLayoutResizePending: false
    // A project is the application workspace, not a late Dubbing-only form.
    // Preserve a requested model card while the operator creates/opens the
    // project, then route to it after the gate accepts the project.
    property string pendingProjectRouteId: ""
    property string pendingProjectFamilyId: ""
    title: appName + " - " + appVersion
    color: Theme.background
    palette {
        window: Theme.background
        windowText: Theme.textPrimary
        base: Theme.surface
        alternateBase: Theme.surfaceAlt
        text: Theme.textPrimary
        button: Theme.surfaceAlt
        buttonText: Theme.textPrimary
        highlight: Theme.accent
        highlightedText: "#ffffff"
        placeholderText: Theme.textSecondary
        toolTipBase: Theme.surface
        toolTipText: Theme.textPrimary
    }

    function updateBannerActionText() {
        if (AppController.updates.downloading) return qsTr("Downloading...")
        if (AppController.updates.downloaded) return qsTr("Install")
        return qsTr("Download")
    }

    function recordQmlSmokeDubbing(control, action, before, after) {
        var next = qmlSmokeDubbingTrace.slice(0)
        next.push({ "control": control, "action": action,
                    "before": before, "after": after })
        qmlSmokeDubbingTrace = next
    }

    function routeRequiresProject(routeId) {
        return ["studio-stt", "studio-tts", "studio-voice-cloning",
                "studio-voice-design", "studio-voice-isolator",
                "studio-alignment", "studio-translation", "studio-dubbing",
                "studio-llm", "media-download", "subtitle-ocr"].indexOf(routeId) >= 0
    }

    function routeLabel(routeId) {
        var routes = StudioRouteRegistry.routes || []
        for (var index = 0; index < routes.length; ++index) {
            if (routes[index].id === routeId)
                return routes[index].label || routeId
        }
        return routeId
    }

    function activateStudioRoute(routeId, familyId) {
        stack.currentIndex = StudioRouteRegistry.getIndex(routeId)
        if (!familyId || familyId === "")
            return
        Qt.callLater(function() {
            if (routeId === "studio-stt") sttLoader.openConfig(familyId)
            else if (routeId === "studio-tts") ttsLoader.openConfig(familyId)
            else if (routeId === "studio-voice-cloning") voiceCloningLoader.openConfig(familyId)
            else if (routeId === "studio-voice-design") voiceDesignLoader.openConfig(familyId)
            else if (routeId === "studio-alignment") alignmentLoader.openConfig(familyId)
            else if (routeId === "studio-translation") translationLoader.openConfig(familyId)
            else if (routeId === "studio-llm") llmLoader.openConfig(familyId)
        })
    }

    function requestStudioRoute(routeId, familyId) {
        workflowsPopup.close()
        downloadsPopup.close()
        communityDialog.close()
        if (!routeRequiresProject(routeId) || AppController.dubbing.hasProject) {
            activateStudioRoute(routeId, familyId || "")
            return
        }
        pendingProjectRouteId = routeId
        pendingProjectFamilyId = familyId || ""
        globalProjectGate.openFor(routeLabel(routeId))
    }

    function resumeProjectRoute() {
        var routeId = pendingProjectRouteId
        var familyId = pendingProjectFamilyId
        pendingProjectRouteId = ""
        pendingProjectFamilyId = ""
        if (routeId !== "")
            activateStudioRoute(routeId, familyId)
    }

    function runUpdateBannerAction() {
        dismissedUpdateVersion = ""
        if (AppController.updates.downloaded) {
            installUpdateDialog.open()
        } else if (AppController.updates.updateAvailable) {
            AppController.updates.downloadUpdate()
        }
    }

    // Error toast
    Popup {
        id: errorPopup
        anchors.centerIn: parent
        width: 400
        padding: Theme.paddingLarge
        modal: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surface
            radius: Theme.radiusMedium
            border.color: Theme.danger
            border.width: 2
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.paddingMedium

            Text {
                text: AppController.pendingErrorCount > 1
                      ? qsTr("Error (%1 pending)").arg(AppController.pendingErrorCount)
                      : qsTr("Error")
                color: Theme.danger
                font.pixelSize: Theme.fontLarge
                font.bold: true
            }
            Text {
                text: AppController.errorMessage
                color: Theme.textPrimary
                font.pixelSize: Theme.fontMedium
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            PrimaryButton {
                text: qsTr("Dismiss")
                Layout.alignment: Qt.AlignRight
                onClicked: {
                    errorPopup.close()
                    AppController.clearError()
                }
            }
        }
    }

    Connections {
        target: AppController
        function onErrorMessageChanged() {
            if (AppController.errorMessage.length > 0)
                errorPopup.open()
        }
    }

    Connections {
        target: Theme
        function onRequestShowDownloads() {
            downloadsPopup.open()
        }
    }

    Connections {
        target: AppController.workflows
        function onOpenRequested(routeId) {
            root.requestStudioRoute(routeId)
        }
    }

    ProjectSelectionGate {
        id: globalProjectGate
        onProjectReady: root.resumeProjectRoute()
        onLeaveRequested: {
            root.pendingProjectRouteId = ""
            root.pendingProjectFamilyId = ""
            root.activateStudioRoute("welcome", "")
        }
    }

    DownloadsPopup {
        id: downloadsPopup
        x: sidebar.width + 8
        y: root.height - height - 16
    }

    WorkflowPopup {
        id: workflowsPopup
        x: sidebar.width + 8
        y: root.height - height - 16
    }

    CommunityDialog {
        id: communityDialog
    }

    ConfirmationDialog {
        id: installUpdateDialog
        titleText: qsTr("Install update")
        messageText: qsTr("LA Studio will close and start the installer. Your app data and downloaded models will stay in the app home directory.")
        confirmText: qsTr("Install")
        cancelText: qsTr("Cancel")
        onConfirmed: AppController.updates.installDownloadedUpdate()
    }

    function persistWindowPlacement() {
        if (restoringWindowPlacement || visibility === ApplicationWindow.Minimized)
            return
        const maximized = visibility === ApplicationWindow.Maximized
        // Maximizing changes the window dimensions to the monitor bounds. Keep
        // the last normal geometry so restoring the window remains predictable.
        AppController.settings.saveWindowPlacement(
            maximized ? AppController.settings.windowX : x,
            maximized ? AppController.settings.windowY : y,
            maximized ? AppController.settings.windowWidth : width,
            maximized ? AppController.settings.windowHeight : height,
            maximized)
    }

    ConfirmationDialog {
        id: updateConsentDialog
        titleText: qsTr("Automatic update checks")
        messageText: qsTr("LA Studio can periodically contact GitHub to check whether a new version is available. No update is downloaded or installed automatically. You can change this later in Settings.")
        confirmText: qsTr("Allow")
        cancelText: qsTr("Not now")
        onConfirmed: {
            AppController.settings.automaticUpdateChecks = true
            AppController.settings.updateCheckConsentAsked = true
        }
        onCancelled: AppController.settings.updateCheckConsentAsked = true
    }

    Component.onCompleted: {
        // Do not bind visibility directly to the persisted value: changing a
        // native Windows window state emits onVisibilityChanged, which writes
        // the setting back and used to create a binding loop/white window.
        visibility = AppController.settings.windowMaximized
                   ? ApplicationWindow.Maximized : ApplicationWindow.Windowed
        restoringWindowPlacement = false
        if (!AppController.settings.updateCheckConsentAsked)
            updateConsentDialog.open()
    }

    function startQmlRouteSmoke() {
        qmlSmokeTimer.routeIndex = 0
        qmlSmokeTimer.waitTicks = 0
        qmlSmokeSubtitleLayoutSizeIndex = 0
        qmlSmokeSubtitleLayoutResizePending = false
        qmlSmokeHomeLayoutSizeIndex = 0
        qmlSmokeHomeLayoutResizePending = false
        qmlSmokeDubbingLayoutSizeIndex = 0
        qmlSmokeDubbingLayoutResizePending = false
        qmlSmokeDubbingAutomaticPending = false
        qmlSmokeProjectGatePending = false
        qmlSmokeVoiceCloneLayoutSizeIndex = 0
        qmlSmokeVoiceCloneLayoutResizePending = false
        // Exercise the same global project gate as an operator.  The Dubbing
        // controller no longer creates an untitled project implicitly, so the
        // smoke must establish its isolated .ladub.json workspace before it
        // can cross the production file-picker boundary later in the route.
        if (!AppController.dubbing.hasProject) {
            if (qmlSmokeProjectUrl === "") {
                console.warn("QML smoke has no isolated project fixture URL")
                qmlSmokeFailed = true
                return
            }
            qmlSmokeProjectGatePending = true
            requestStudioRoute("studio-dubbing")
        }
        qmlSmokeTimer.start()
    }

    function qmlSmokeRouteLoaded(routeIndex) {
        switch (routeIndex) {
        case 0: return true
        case 1: return sttLoader.status === Loader.Ready
        case 2: return ttsLoader.status === Loader.Ready
        case 3: return voiceCloningLoader.status === Loader.Ready
        case 4: return voiceDesignLoader.status === Loader.Ready
        case 5: return voiceIsolatorLoader.status === Loader.Ready
        case 6: return alignmentLoader.status === Loader.Ready
        case 7: return translationLoader.status === Loader.Ready
        case 8: return dubbingLoader.status === Loader.Ready
        case 9: return llmLoader.status === Loader.Ready
        case 10: return modelsLoader.status === Loader.Ready
        case 11: return myModelsLoader.status === Loader.Ready
        case 12: return developerLoader.status === Loader.Ready
        case 13: return settingsLoader.status === Loader.Ready
        case 14: return mediaDownloadLoader.status === Loader.Ready
        case 15: return subtitleOcrLoader.status === Loader.Ready
        default: return false
        }
    }

    function qmlSmokeExerciseRoute(routeIndex) {
        if (routeIndex === 0 && welcomePage.qmlSmokeHomeCardsCheck) {
            var homeSizes = [
                { width: 1024, height: 720 },
                { width: 1280, height: 800 },
                { width: 1600, height: 900 }
            ]
            if (qmlSmokeHomeLayoutResizePending) {
                qmlSmokeHomeLayoutResizePending = false
                return welcomePage.qmlSmokeHomeCardsCheck() ? 0 : -1
            }
            if (qmlSmokeHomeLayoutSizeIndex < homeSizes.length) {
                var homeSize = homeSizes[qmlSmokeHomeLayoutSizeIndex++]
                root.width = homeSize.width
                root.height = homeSize.height
                qmlSmokeHomeLayoutResizePending = true
                return 0
            }
            return 1
        }
        if (routeIndex === 1 && sttLoader.item
                && sttLoader.item.qmlSmokePendingSelectionIsolated) {
            return sttLoader.item.qmlSmokePendingSelectionIsolated()
        }
        if (routeIndex === 3 && voiceCloningLoader.item
                && voiceCloningLoader.item.qmlSmokeVoiceCloneLayoutCheck) {
            var voiceCloneSizes = [
                { width: 1024, height: 720 },
                { width: 1280, height: 800 },
                { width: 1600, height: 900 }
            ]
            if (qmlSmokeVoiceCloneLayoutResizePending) {
                qmlSmokeVoiceCloneLayoutResizePending = false
                return voiceCloningLoader.item.qmlSmokeVoiceCloneLayoutCheck() ? 0 : -1
            }
            if (qmlSmokeVoiceCloneLayoutSizeIndex < voiceCloneSizes.length) {
                var voiceCloneSize = voiceCloneSizes[qmlSmokeVoiceCloneLayoutSizeIndex++]
                root.width = voiceCloneSize.width
                root.height = voiceCloneSize.height
                qmlSmokeVoiceCloneLayoutResizePending = true
                return 0
            }
            return 1
        }
        if (routeIndex === 8 && dubbingLoader.item
                && dubbingLoader.item.qmlSmokeTranscriptSourceCheck) {
            var dubbingSizes = [
                { width: 1024, height: 720 },
                { width: 1280, height: 800 },
                { width: 1600, height: 900 }
            ]
            if (qmlSmokeDubbingLayoutResizePending) {
                var dubbingCheckResult = dubbingLoader.item.qmlSmokeTranscriptSourceCheck()
                if (dubbingCheckResult === 0)
                    return 0
                qmlSmokeDubbingLayoutResizePending = false
                if (dubbingCheckResult < 0) return -1
                dubbingLoader.item.beginQmlSmokeAutomaticPreflightCheck()
                qmlSmokeDubbingAutomaticPending = true
                return 0
            }
            if (qmlSmokeDubbingAutomaticPending) {
                var automaticCheckResult = dubbingLoader.item.qmlSmokeAutomaticPreflightCheck()
                if (automaticCheckResult === 0) return 0
                qmlSmokeDubbingAutomaticPending = false
                return automaticCheckResult > 0 ? 1 : -1
            }
            if (qmlSmokeDubbingLayoutSizeIndex < dubbingSizes.length) {
                var dubbingSize = dubbingSizes[qmlSmokeDubbingLayoutSizeIndex++]
                root.width = dubbingSize.width
                root.height = dubbingSize.height
                if (dubbingLoader.item.beginQmlSmokeTranscriptSourceCheck)
                    dubbingLoader.item.beginQmlSmokeTranscriptSourceCheck()
                qmlSmokeDubbingLayoutResizePending = true
                return 0
            }
            return 1
        }
        if (routeIndex === 15 && subtitleOcrLoader.item
                && subtitleOcrLoader.item.qmlSmokeLayoutCheck) {
            var subtitleOcrSizes = [
                { width: 1024, height: 720 },
                { width: 1280, height: 800 },
                { width: 1600, height: 900 }
            ]
            if (qmlSmokeSubtitleLayoutResizePending) {
                qmlSmokeSubtitleLayoutResizePending = false
                return subtitleOcrLoader.item.qmlSmokeLayoutCheck() ? 0 : -1
            }
            if (qmlSmokeSubtitleLayoutSizeIndex < subtitleOcrSizes.length) {
                var size = subtitleOcrSizes[qmlSmokeSubtitleLayoutSizeIndex++]
                root.width = size.width
                root.height = size.height
                qmlSmokeSubtitleLayoutResizePending = true
                return 0
            }
            return 1
        }
        return 1
    }

    Timer {
        id: qmlSmokeTimer
        interval: 100
        repeat: true
        running: false
        property int routeIndex: 0
        property int waitTicks: 0

        onTriggered: {
            if (root.qmlSmokeProjectGatePending) {
                if (waitTicks === 0) {
                    if (!globalProjectGate.visible) {
                        console.warn("Global project gate did not block a studio route")
                        root.qmlSmokeFailed = true
                        running = false
                        Qt.quit()
                        return
                    }
                    globalProjectGate.createProject(root.qmlSmokeProjectUrl)
                    waitTicks = 1
                    return
                }
                if (!AppController.dubbing.hasProject || globalProjectGate.visible) {
                    console.warn("Global project gate did not create the isolated smoke project")
                    root.qmlSmokeFailed = true
                    running = false
                    Qt.quit()
                    return
                }
                root.qmlSmokeProjectGatePending = false
                waitTicks = 0
                return
            }
            if (routeIndex >= StudioRouteRegistry.routes.length) {
                running = false
                Qt.quit()
                return
            }
            var route = StudioRouteRegistry.routes[routeIndex]
            if (!route || StudioRouteRegistry.getIndex(route.id) !== routeIndex) {
                console.warn("Route registry is inconsistent for index " + routeIndex)
                running = false
                Qt.quit()
                return
            }
            if (waitTicks === 0) {
                stack.currentIndex = routeIndex
                waitTicks = 1
                return
            }
            if (root.qmlSmokeRouteLoaded(routeIndex)) {
                var exerciseResult = root.qmlSmokeExerciseRoute(routeIndex)
                if (exerciseResult === 0) {
                    ++waitTicks
                    if (waitTicks > 100) {
                        console.warn("Timed out while exercising model selection for route "
                                     + route.id)
                        running = false
                        Qt.quit()
                    }
                    return
                }
                if (exerciseResult < 0) {
                    var failedContract = routeIndex === 1
                        ? "Pending model selection escaped the configuration dialog for route "
                        : "Route-specific QML smoke contract failed for route "
                    var failureDetail = routeIndex === 8 && dubbingLoader.item
                            ? dubbingLoader.item.qmlSmokeTranscriptSourceFailure : ""
                    console.warn(failedContract + route.id
                                 + (failureDetail ? ": " + failureDetail : ""))
                    root.qmlSmokeFailed = true
                    running = false
                    Qt.quit()
                    return
                }
                ++routeIndex
                waitTicks = 0
                return
            }
            ++waitTicks
            if (waitTicks > 100) {
                console.warn("Route did not finish loading: " + route.id)
                root.qmlSmokeFailed = true
                running = false
                Qt.quit()
            }
        }
    }

    onXChanged: persistWindowPlacement()
    onYChanged: persistWindowPlacement()
    onWidthChanged: persistWindowPlacement()
    onHeightChanged: persistWindowPlacement()
    onVisibilityChanged: persistWindowPlacement()
    onClosing: persistWindowPlacement()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        AppTitleBar {
            window: root
            appName: root.appName
            appVersion: root.appVersion
            visible: !root.usesNativeWindowFrame
            Layout.preferredHeight: root.usesNativeWindowFrame ? 0 : 34
        }

        Rectangle {
            id: updateBanner

            readonly property bool dismissed: AppController.updates.latestVersion !== "" && root.dismissedUpdateVersion === AppController.updates.latestVersion

            visible: AppController.updates.updateAvailable && !dismissed
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 56 : 0
            color: Qt.rgba(1.0, 0.65, 0.15, 0.13)
            border.color: Qt.rgba(1.0, 0.65, 0.15, 0.35)
            border.width: visible ? 1 : 0

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.paddingLarge
                anchors.rightMargin: Theme.paddingLarge
                spacing: Theme.paddingMedium

                LineIcon {
                    name: AppController.updates.downloaded ? "check" : "download"
                    color: AppController.updates.downloaded ? Theme.success : Theme.warning
                    Layout.preferredWidth: 22
                    Layout.preferredHeight: 22
                    strokeWidth: 2.0
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3

                    Text {
                        Layout.fillWidth: true
                        text: AppController.updates.downloaded
                              ? qsTr("LA Studio v%1 is ready to install").arg(AppController.updates.latestVersion)
                              : qsTr("LA Studio v%1 is available").arg(AppController.updates.latestVersion)
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontMedium
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: !AppController.updates.downloading
                        text: qsTr("Update now, or keep working and install it later from Settings.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                    }

                    ProgressBar {
                        visible: AppController.updates.downloading
                        from: 0
                        to: 1
                        value: AppController.updates.downloadProgress
                        Layout.fillWidth: true
                        Layout.preferredHeight: 6
                    }
                }

                PrimaryButton {
                    text: qsTr("Release notes")
                    iconName: "external-link"
                    quiet: true
                    implicitWidth: 130
                    implicitHeight: 32
                    visible: AppController.updates.releaseUrl !== ""
                    onClicked: Qt.openUrlExternally(AppController.updates.releaseUrl)
                }

                PrimaryButton {
                    text: root.updateBannerActionText()
                    iconName: AppController.updates.downloaded ? "check" : "download"
                    implicitWidth: 120
                    implicitHeight: 32
                    enabled: !AppController.updates.downloading
                    onClicked: root.runUpdateBannerAction()
                }

                PrimaryButton {
                    text: qsTr("Later")
                    quiet: true
                    implicitWidth: 78
                    implicitHeight: 32
                    visible: !AppController.updates.downloading
                    onClicked: root.dismissedUpdateVersion = AppController.updates.latestVersion
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Sidebar {
                id: sidebar
                Layout.fillHeight: true
                Layout.preferredWidth: sidebar.isCollapsed ? sidebar.collapsedWidth : sidebar.expandedWidth
                Behavior on Layout.preferredWidth {
                    NumberAnimation { duration: 180; easing.type: Easing.InOutQuad }
                }
                currentIndex: stack.currentIndex
                activitiesActive: workflowsPopup.opened
                downloadsActive: downloadsPopup.opened
                communityActive: communityDialog.opened
                onNavigated: function(routeId) {
                    root.requestStudioRoute(routeId)
                }
                onCommunityClicked: {
                    workflowsPopup.close()
                    downloadsPopup.close()
                    if (communityDialog.opened) {
                        communityDialog.close()
                    } else {
                        communityDialog.open()
                    }
                }
                onWorkflowsClicked: {
                    downloadsPopup.close()
                    communityDialog.close()
                    if (workflowsPopup.opened) {
                        workflowsPopup.close()
                    } else {
                        workflowsPopup.open()
                    }
                }
                onDownloadsClicked: {
                    workflowsPopup.close()
                    communityDialog.close()
                    if (downloadsPopup.opened) {
                        downloadsPopup.close()
                    } else {
                        downloadsPopup.open()
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: Theme.surfaceAlt
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                StackLayout {
                    id: stack
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                WelcomePage {
                    id: welcomePage
                    onPageRequested: function(routeId) {
                        root.requestStudioRoute(routeId)
                    }
                }
                Loader {
                    id: sttLoader
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    active: stack.currentIndex === 1 || pendingFamilyId !== ""
                    property string pendingFamilyId: ""
                    sourceComponent: SttPage {
                        id: sttPage
                    }
                    onLoaded: {
                        if (pendingFamilyId !== "") {
                            item.openConfiguration(pendingFamilyId)
                            pendingFamilyId = ""
                        }
                    }
                    function openConfig(familyId) {
                        if (item) {
                            item.openConfiguration(familyId)
                        } else {
                            pendingFamilyId = familyId
                        }
                    }
                }
                Loader {
                    id: ttsLoader
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    active: stack.currentIndex === 2 || pendingFamilyId !== ""
                    property string pendingFamilyId: ""
                    sourceComponent: TtsPage {
                        id: ttsPage
                    }
                    onLoaded: {
                        if (pendingFamilyId !== "") {
                            item.openConfiguration(pendingFamilyId)
                            pendingFamilyId = ""
                        }
                    }
                    function openConfig(familyId) {
                        if (item) {
                            item.openConfiguration(familyId)
                        } else {
                            pendingFamilyId = familyId
                        }
                    }
                }
                Loader {
                    id: voiceCloningLoader
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    active: stack.currentIndex === 3 || pendingFamilyId !== ""
                    property string pendingFamilyId: ""
                    sourceComponent: VoiceCloningPage {
                        id: voiceCloningPage
                    }
                    onLoaded: {
                        if (pendingFamilyId !== "") {
                            item.openConfiguration(pendingFamilyId)
                            pendingFamilyId = ""
                        }
                    }
                    function openConfig(familyId) {
                        if (item) {
                            item.openConfiguration(familyId)
                        } else {
                            pendingFamilyId = familyId
                        }
                    }
                }
                Loader {
                    id: voiceDesignLoader
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    active: stack.currentIndex === 4 || pendingFamilyId !== ""
                    property string pendingFamilyId: ""
                    sourceComponent: VoiceDesignPage {
                        id: voiceDesignPage
                    }
                    onLoaded: {
                        if (pendingFamilyId !== "") {
                            item.openConfiguration(pendingFamilyId)
                            pendingFamilyId = ""
                        }
                    }
                    function openConfig(familyId) {
                        if (item) {
                            item.openConfiguration(familyId)
                        } else {
                            pendingFamilyId = familyId
                        }
                    }
                }
                Loader {
                    id: voiceIsolatorLoader
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    active: stack.currentIndex === 5
                    sourceComponent: VoiceIsolatorPage {}
                }
                Loader {
                    id: alignmentLoader
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    active: stack.currentIndex === 6 || pendingFamilyId !== ""
                    property string pendingFamilyId: ""
                    sourceComponent: AlignmentPage {
                        id: alignmentPage
                    }
                    onLoaded: {
                        if (pendingFamilyId !== "") {
                            item.openConfiguration(pendingFamilyId)
                            pendingFamilyId = ""
                        }
                    }
                    function openConfig(familyId) {
                        if (item) {
                            item.openConfiguration(familyId)
                        } else {
                            pendingFamilyId = familyId
                        }
                    }
                }
                Loader {
                    id: translationLoader
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    active: stack.currentIndex === 7 || pendingFamilyId !== ""
                    property string pendingFamilyId: ""
                    sourceComponent: TranslationPage {}
                    function openConfig(familyId) {
                        if (item) item.openConfiguration(familyId)
                        else pendingFamilyId = familyId
                    }
                }
                Loader {
                    id: dubbingLoader
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    active: stack.currentIndex === 8
                    sourceComponent: DubbingPage { qmlSmokeMediaPath: root.qmlSmokeMediaPath }
                }
                Loader {
                    id: llmLoader
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    active: stack.currentIndex === 9 || pendingFamilyId !== ""
                    property string pendingFamilyId: ""
                    sourceComponent: LlmPage {}
                    onLoaded: if (pendingFamilyId !== "") { item.openConfiguration(pendingFamilyId); pendingFamilyId = "" }
                    function openConfig(familyId) { if (item) item.openConfiguration(familyId); else pendingFamilyId = familyId }
                }
                Loader {
                    id: modelsLoader
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    asynchronous: true
                    active: stack.currentIndex === 10
                    sourceComponent: ModelsPage {
                    onOpenStudioRequested: function(capability, familyId) {
                        var routeId = StudioRouteRegistry.routeForCapability(capability)
                        root.requestStudioRoute(routeId, familyId)
                    }
                    }
                }
                Loader {
                    id: myModelsLoader
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    asynchronous: true
                    active: stack.currentIndex === 11
                    sourceComponent: MyModelsPage {
                    onOpenStudioRequested: function(capability, familyId) {
                        var routeId = StudioRouteRegistry.routeForCapability(capability)
                        root.requestStudioRoute(routeId, familyId)
                    }
                    }
                }
                Loader {
                    id: developerLoader
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    active: stack.currentIndex === 12
                    sourceComponent: DeveloperPage {}
                }
                Loader {
                    id: settingsLoader
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    active: stack.currentIndex === 13
                    sourceComponent: SettingsPage {}
                }
                Loader {
                    id: mediaDownloadLoader
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    active: stack.currentIndex === 14
                    sourceComponent: MediaDownloadPage {
                        onOpenDubbingRequested: {
                            root.requestStudioRoute("studio-dubbing")
                        }
                        onOpenSubtitleOcrRequested: {
                            root.requestStudioRoute("subtitle-ocr")
                        }
                    }
                }
                Loader {
                    id: subtitleOcrLoader
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    active: stack.currentIndex === 15
                    sourceComponent: SubtitleOcrPage {}
                }
                }

                BottomLogPanel {
                    id: bottomLogPanel
                    // Diagnostics belong to Settings instead of occupying
                    // vertical space in every studio, especially Dubbing's
                    // timeline workspace.
                    visible: stack.currentIndex === 13
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? bottomLogPanel.implicitHeight : 0
                    
                    Behavior on Layout.preferredHeight {
                        enabled: !bottomLogPanel.isResizing
                        NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
                    }
                }
            }
        }
    }

    ResizeHandle {
        edge: Qt.LeftEdge
        cursorShape: Qt.SizeHorCursor
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: edgeWidth
    }

    ResizeHandle {
        edge: Qt.RightEdge
        cursorShape: Qt.SizeHorCursor
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: edgeWidth
    }

    ResizeHandle {
        edge: Qt.TopEdge
        cursorShape: Qt.SizeVerCursor
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: edgeWidth
    }

    ResizeHandle {
        edge: Qt.BottomEdge
        cursorShape: Qt.SizeVerCursor
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: edgeWidth
    }

    ResizeHandle {
        edge: Qt.TopEdge | Qt.LeftEdge
        cursorShape: Qt.SizeFDiagCursor
        anchors.left: parent.left
        anchors.top: parent.top
        width: cornerWidth
        height: cornerWidth
    }

    ResizeHandle {
        edge: Qt.TopEdge | Qt.RightEdge
        cursorShape: Qt.SizeBDiagCursor
        anchors.right: parent.right
        anchors.top: parent.top
        width: cornerWidth
        height: cornerWidth
    }

    ResizeHandle {
        edge: Qt.BottomEdge | Qt.LeftEdge
        cursorShape: Qt.SizeBDiagCursor
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: cornerWidth
        height: cornerWidth
    }

    ResizeHandle {
        edge: Qt.BottomEdge | Qt.RightEdge
        cursorShape: Qt.SizeFDiagCursor
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: cornerWidth
        height: cornerWidth
    }

    component ResizeHandle: MouseArea {
        property int edge: 0
        readonly property int edgeWidth: 6
        readonly property int cornerWidth: 14

        visible: !root.usesNativeWindowFrame
                 && root.visibility !== Window.Maximized
                 && root.visibility !== Window.FullScreen
        enabled: visible
        z: 1000
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton

        onPressed: {
            if (root.startSystemResize) {
                root.startSystemResize(edge)
            }
        }
    }
}
