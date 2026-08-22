pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../base"
import LAStudio

// One Dubbing-only setup surface for temporary Direct Colab workers.  It is
// intentionally a view over AppController's session objects: URL/token never
// pass through project persistence, Settings, a Gateway client, or this QML
// component after a connection starts.
Dialog {
    id: root

    required property var dubbing
    // When opened from Automatic preflight, only the selected Direct Colab
    // workers are displayed. The general Dubbing settings surface leaves this
    // empty and continues to show all available capability cards.
    property var stageIds: []
    property var draftUrls: ({})
    property var draftTokens: ({})
    property string unifiedWorkerUrl: ""
    property string unifiedToken: ""

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(980, parent ? parent.width - Theme.paddingXL * 2 : 980)
    height: Math.min(760, parent ? parent.height - Theme.paddingXL * 2 : 760)
    modal: true
    padding: 0
    title: ""
    closePolicy: Popup.CloseOnEscape

    function sessionForStage(stageId) {
        if (stageId === "source-separate") return AppController.colabSeparationSession
        if (stageId === "transcribe") return AppController.colabSttSession
        if (stageId === "subtitle-ocr") return AppController.colabSubtitleOcrSession
        if (stageId === "translate") return AppController.colabTranslationSession
        if (stageId === "synthesize") return AppController.colabTtsSession
        if (stageId === "alignment") return AppController.colabAlignmentSession
        return null
    }

    function draftUrl(stageId, session) {
        var value = draftUrls[stageId]
        return value === undefined ? (session ? session.workerUrl : "") : value
    }

    function setDraftUrl(stageId, value) {
        var next = Object.assign({}, draftUrls)
        next[stageId] = value
        draftUrls = next
    }

    function draftToken(stageId) { return draftTokens[stageId] || "" }

    function setDraftToken(stageId, value) {
        var next = Object.assign({}, draftTokens)
        next[stageId] = value
        draftTokens = next
    }

    function selectedDirectColabStageCount() {
        var stages = dubbing ? dubbing.colabSetupStages : []
        var count = 0
        for (var i = 0; i < stages.length; ++i)
            if (stages[i].selectedForDirectColab) ++count
        return count
    }

    background: Rectangle {
        color: Theme.surface
        radius: Theme.radiusMedium
        border.color: Qt.rgba(1, 1, 1, 0.12)
        border.width: 1
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.paddingLarge
            spacing: Theme.paddingMedium

            LineIcon {
                name: "cloud"
                color: Theme.accentLight
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    text: qsTr("Dubbing Direct Colab setup")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontLarge
                    font.bold: true
                }
                Text {
                    text: qsTr("Configure and verify GPU stages once. API Gateway remains a separate route; tokens stay only in this app session.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
            }
            Button {
                implicitWidth: 32
                implicitHeight: 32
                onClicked: root.close()
                contentItem: LineIcon { anchors.centerIn: parent; name: "close"; color: Theme.textSecondary; width: 16; height: 16 }
                background: Rectangle { radius: Theme.radiusSmall; color: parent.hovered ? Qt.rgba(1, 1, 1, 0.06) : "transparent" }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.surfaceAlt }

        Rectangle {
            Layout.fillWidth: true
            Layout.margins: Theme.paddingLarge
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
                    text: qsTr("Next transcript action")
                    color: Theme.textPrimary
                    font.bold: true
                }
                RowLayout {
                    Layout.fillWidth: true
                    ComboBox {
                        id: colabTranscriptSourceMode
                        objectName: "dubbingColabTranscriptSourceMode"
                        Layout.preferredWidth: 230
                        textRole: "label"
                        valueRole: "id"
                        model: [
                            { id: "stt", label: qsTr("Chỉ STT") },
                            { id: "ocr", label: qsTr("Chỉ OCR") },
                            { id: "reconcile", label: qsTr("Khớp STT + OCR") }
                        ]
                        currentIndex: {
                            var source = root.dubbing.transcriptConfiguration.transcriptSource || "stt"
                            if (source === "stt+ocr") source = "reconcile"
                            for (var i = 0; i < model.length; ++i)
                                if (model[i].id === source) return i
                            return 0
                        }
                        enabled: !root.dubbing.processing
                        onActivated: function(index) {
                            root.dubbing.setWorkflowNodeParameters("transcribe", {
                                transcriptSource: model[index].id
                            })
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("STT and Subtitle OCR can always be configured and run independently below. Reconcile only combines saved results locally; it starts neither worker.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.paddingLarge
            Layout.rightMargin: Theme.paddingLarge
            implicitHeight: unifiedLayout.implicitHeight + Theme.paddingMedium * 2
            radius: Theme.radiusSmall
            color: Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.06)
            border.color: Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.35)
            border.width: 1

            ColumnLayout {
                id: unifiedLayout
                anchors.fill: parent
                anchors.margins: Theme.paddingMedium
                spacing: Theme.paddingSmall

                Text {
                    text: qsTr("Optional system route: Unified Dubbing Colab")
                    color: Theme.textPrimary
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Run one Unified Colab coordinator, then enter its URL and token once. It connects only the %1 Dubbing stage(s) currently selected for Direct Colab. Existing individual Colab, API Gateway, and Local routes remain unchanged.")
                        .arg(root.selectedDirectColabStageCount())
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    // A unified URL is an explicit worker contract, not an
                    // alias for any one of the exact-model notebooks below.
                    // Keep the established per-model notebook links intact
                    // until the coordinator itself is available as a tested
                    // artifact.
                    text: qsTr("The unified worker must expose the selected stage routes under /v1/unified/&lt;capability&gt;/&lt;model&gt;. Use the existing exact-model notebook cards below when you do not run such a coordinator.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.paddingSmall
                    TextField {
                        id: unifiedUrlField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Unified worker URL (https://…trycloudflare.com)")
                        text: root.unifiedWorkerUrl
                        selectByMouse: true
                        onTextEdited: root.unifiedWorkerUrl = text
                        color: Theme.textPrimary
                        placeholderTextColor: Theme.textSecondary
                        background: Rectangle { radius: Theme.radiusSmall; color: Theme.surface; border.color: unifiedUrlField.activeFocus ? Theme.accent : Theme.surfaceAlt; border.width: 1 }
                    }
                    TextField {
                        id: unifiedTokenField
                        Layout.preferredWidth: 220
                        placeholderText: qsTr("Temporary bearer token")
                        text: root.unifiedToken
                        echoMode: TextInput.Password
                        selectByMouse: true
                        onTextEdited: root.unifiedToken = text
                        color: Theme.textPrimary
                        placeholderTextColor: Theme.textSecondary
                        background: Rectangle { radius: Theme.radiusSmall; color: Theme.surface; border.color: unifiedTokenField.activeFocus ? Theme.accent : Theme.surfaceAlt; border.width: 1 }
                    }
                    PrimaryButton {
                        text: qsTr("Connect selected stages")
                        iconName: "link"
                        implicitWidth: 180
                        enabled: root.selectedDirectColabStageCount() > 0
                                 && unifiedUrlField.text.trim() !== ""
                                 && unifiedTokenField.text !== ""
                                 && !root.dubbing.colabSetupChecking
                        onClicked: {
                            if (root.dubbing.connectUnifiedWorkflowColab(unifiedUrlField.text.trim(), unifiedTokenField.text))
                                root.unifiedToken = ""
                        }
                    }
                }
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.paddingLarge
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width
                spacing: Theme.paddingMedium

                Repeater {
                    id: stageRepeater
                    // Capture the outer dialog once.  A Repeater delegate has
                    // its own component scope, so resolving the `root` id from
                    // an event handler is not reliable on every Qt build.
                    readonly property var setupDialog: root
                    model: (root.stageIds && root.stageIds.length > 0)
                           ? root.dubbing.colabSetupStages.filter(function(stage) {
                               return root.stageIds.indexOf(stage.id) >= 0
                           })
                           : root.dubbing.colabSetupStages

                    delegate: Rectangle {
                        id: stageCard
                        required property var modelData
                        readonly property var setupDialog: stageRepeater.setupDialog
                        readonly property string stageId: modelData.id || ""
                        readonly property var stageSession: stageRepeater.setupDialog.sessionForStage(stageId)
                        Layout.fillWidth: true
                        implicitHeight: stageLayout.implicitHeight + Theme.paddingMedium * 2
                        radius: Theme.radiusSmall
                        color: Qt.rgba(1, 1, 1, 0.025)
                        border.color: modelData.verified
                                      ? Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.46)
                                      : (modelData.selectedForDirectColab
                                         ? Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.42)
                                         : Qt.rgba(1, 1, 1, 0.10))
                        border.width: 1

                        ColumnLayout {
                            id: stageLayout
                            anchors.fill: parent
                            anchors.margins: Theme.paddingMedium
                            spacing: Theme.paddingSmall

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.paddingSmall
                                Text {
                                    Layout.fillWidth: true
                                    text: stageCard.modelData.title || stageCard.stageId
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontMedium
                                    font.bold: true
                                }
                                Text {
                                    text: stageCard.modelData.verified
                                          ? qsTr("Verified exact worker")
                                          : (stageCard.modelData.selectedForDirectColab
                                             ? qsTr("Direct Colab needs verification")
                                             : qsTr("Optional — current route is not Direct Colab"))
                                    color: stageCard.modelData.verified ? Theme.success
                                           : (stageCard.modelData.selectedForDirectColab ? Theme.warning : Theme.textSecondary)
                                    font.pixelSize: 10
                                    font.bold: true
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                visible: stageCard.stageId === "transcribe" || stageCard.stageId === "subtitle-ocr"
                                text: stageCard.modelData.requiredForCurrentTranscriptAction
                                      ? qsTr("Required by the next transcript action.")
                                      : qsTr("Available independently. Select its action when you are ready to run it.")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSmall
                                wrapMode: Text.WordWrap
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.paddingSmall
                                Text { text: qsTr("Exact model"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                                ComboBox {
                                    id: modelBox
                                    Layout.preferredWidth: 315
                                    textRole: "displayName"
                                    model: stageCard.setupDialog.dubbing.colabModelOptionsForNode(stageCard.stageId)
                                    currentIndex: {
                                        for (var i = 0; i < model.length; ++i)
                                            if (model[i].modelId === stageCard.modelData.modelId) return i
                                        return -1
                                    }
                                    onActivated: function(index) {
                                        stageCard.setupDialog.dubbing.selectWorkflowColabModel(stageCard.stageId, model[index].modelId)
                                    }
                                    enabled: true
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: stageCard.modelData.capability || ""
                                    color: Theme.textSecondary
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                            }

                            ColabNotebookLink {
                                notebookFile: stageCard.modelData.notebookFile || ""
                                enabled: true
                                opacity: 1.0
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.paddingSmall
                                TextField {
                                    id: workerUrlField
                                    Layout.fillWidth: true
                                    placeholderText: qsTr("Temporary worker URL (https://…trycloudflare.com)")
                                    text: stageCard.setupDialog.draftUrl(stageCard.stageId, stageCard.stageSession)
                                    selectByMouse: true
                                    onTextEdited: stageCard.setupDialog.setDraftUrl(stageCard.stageId, text)
                                    enabled: true
                                    color: Theme.textPrimary
                                    placeholderTextColor: Theme.textSecondary
                                    background: Rectangle { radius: Theme.radiusSmall; color: Theme.surface; border.color: workerUrlField.activeFocus ? Theme.accent : Theme.surfaceAlt; border.width: 1 }
                                }
                                TextField {
                                    id: tokenField
                                    Layout.preferredWidth: 220
                                    placeholderText: qsTr("Temporary bearer token")
                                    text: stageCard.setupDialog.draftToken(stageCard.stageId)
                                    echoMode: TextInput.Password
                                    selectByMouse: true
                                    onTextEdited: stageCard.setupDialog.setDraftToken(stageCard.stageId, text)
                                    enabled: true
                                    color: Theme.textPrimary
                                    placeholderTextColor: Theme.textSecondary
                                    background: Rectangle { radius: Theme.radiusSmall; color: Theme.surface; border.color: tokenField.activeFocus ? Theme.accent : Theme.surfaceAlt; border.width: 1 }
                                }
                                PrimaryButton {
                                    text: stageCard.stageSession && stageCard.stageSession.active ? qsTr("Replace") : qsTr("Connect")
                                    iconName: "link"
                                    implicitWidth: 104
                                    enabled: workerUrlField.text.trim() !== "" && tokenField.text !== "" && !stageCard.setupDialog.dubbing.colabSetupChecking
                                    onClicked: {
                                        if (stageCard.setupDialog.dubbing.connectWorkflowColabStage(stageCard.stageId,
                                                                                   stageCard.modelData.modelId,
                                                                                   workerUrlField.text.trim(), tokenField.text)) {
                                            stageCard.setupDialog.setDraftToken(stageCard.stageId, "")
                                        }
                                    }
                                }
                            }

                            ColabSessionStatus {
                                Layout.fillWidth: true
                                session: stageCard.stageSession
                                showDisconnected: true
                                useExternalActions: true
                                onCheckRequested: {
                                    stageCard.setupDialog.dubbing.checkWorkflowColabStage(stageCard.stageId)
                                }
                                onDisconnectRequested: {
                                    stageCard.setupDialog.dubbing.disconnectWorkflowColabStage(stageCard.stageId)
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: stageCard.modelData.diagnostic || ""
                                color: stageCard.modelData.verified ? Theme.success
                                       : (stageCard.modelData.selectedForDirectColab ? Theme.warning : Theme.textSecondary)
                                font.pixelSize: 10
                                wrapMode: Text.WordWrap
                            }
                            Text {
                                Layout.fillWidth: true
                                text: stageCard.modelData.snapshotValid ? qsTr("Session snapshot valid") : qsTr("No valid session snapshot")
                                color: stageCard.modelData.snapshotValid ? Theme.success : Theme.textSecondary
                                font.pixelSize: 10
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
            spacing: Theme.paddingMedium
            Text {
                Layout.fillWidth: true
                text: root.dubbing.colabSetupSummary === "" ? qsTr("Choose an exact model, then connect and verify its own notebook worker.") : root.dubbing.colabSetupSummary
                color: root.dubbing.colabSetupChecking ? Theme.warning : Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
            PrimaryButton {
                text: root.dubbing.colabSetupChecking ? qsTr("Checking…") : qsTr("Check all selected")
                iconName: "activity"
                enabled: !root.dubbing.colabSetupChecking
                onClicked: root.dubbing.validateAllWorkflowColabStages()
            }
            PrimaryButton { text: qsTr("Close"); quiet: true; onClicked: root.close() }
        }
    }
}
