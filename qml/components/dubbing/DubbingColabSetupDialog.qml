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
    property var draftUrls: ({})
    property var draftTokens: ({})

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
        if (stageId === "voice-clone") return AppController.colabVoiceCloneSession
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
                    model: root.dubbing.colabSetupStages

                    delegate: Rectangle {
                        id: stageCard
                        required property var modelData
                        readonly property string stageId: modelData.id || ""
                        readonly property var stageSession: root.sessionForStage(stageId)
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

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.paddingSmall
                                Text { text: qsTr("Exact model"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                                ComboBox {
                                    id: modelBox
                                    Layout.preferredWidth: 315
                                    textRole: "displayName"
                                    model: root.dubbing.colabModelOptionsForNode(stageCard.stageId)
                                    currentIndex: {
                                        for (var i = 0; i < model.length; ++i)
                                            if (model[i].modelId === stageCard.modelData.modelId) return i
                                        return -1
                                    }
                                    onActivated: function(index) {
                                        root.dubbing.selectWorkflowColabModel(stageCard.stageId, model[index].modelId)
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: stageCard.modelData.capability || ""
                                    color: Theme.textSecondary
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                            }

                            ColabNotebookLink { notebookFile: stageCard.modelData.notebookFile || "" }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.paddingSmall
                                TextField {
                                    id: workerUrlField
                                    Layout.fillWidth: true
                                    placeholderText: qsTr("Temporary worker URL (https://…trycloudflare.com)")
                                    text: root.draftUrl(stageCard.stageId, stageCard.stageSession)
                                    selectByMouse: true
                                    onTextEdited: root.setDraftUrl(stageCard.stageId, text)
                                    color: Theme.textPrimary
                                    placeholderTextColor: Theme.textSecondary
                                    background: Rectangle { radius: Theme.radiusSmall; color: Theme.surface; border.color: workerUrlField.activeFocus ? Theme.accent : Theme.surfaceAlt; border.width: 1 }
                                }
                                TextField {
                                    id: tokenField
                                    Layout.preferredWidth: 220
                                    placeholderText: qsTr("Temporary bearer token")
                                    text: root.draftToken(stageCard.stageId)
                                    echoMode: TextInput.Password
                                    selectByMouse: true
                                    onTextEdited: root.setDraftToken(stageCard.stageId, text)
                                    color: Theme.textPrimary
                                    placeholderTextColor: Theme.textSecondary
                                    background: Rectangle { radius: Theme.radiusSmall; color: Theme.surface; border.color: tokenField.activeFocus ? Theme.accent : Theme.surfaceAlt; border.width: 1 }
                                }
                                PrimaryButton {
                                    text: stageCard.stageSession && stageCard.stageSession.active ? qsTr("Replace") : qsTr("Connect")
                                    iconName: "link"
                                    implicitWidth: 104
                                    enabled: workerUrlField.text.trim() !== "" && tokenField.text !== "" && !root.dubbing.colabSetupChecking
                                    onClicked: {
                                        if (root.dubbing.connectWorkflowColabStage(stageCard.stageId,
                                                                                   stageCard.modelData.modelId,
                                                                                   workerUrlField.text.trim(), tokenField.text)) {
                                            root.setDraftToken(stageCard.stageId, "")
                                        }
                                    }
                                }
                            }

                            ColabSessionStatus {
                                Layout.fillWidth: true
                                session: stageCard.stageSession
                                showDisconnected: true
                            }
                            Text {
                                Layout.fillWidth: true
                                text: stageCard.modelData.diagnostic || ""
                                color: stageCard.modelData.verified ? Theme.success
                                       : (stageCard.modelData.selectedForDirectColab ? Theme.warning : Theme.textSecondary)
                                font.pixelSize: 10
                                wrapMode: Text.WordWrap
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.paddingSmall
                                PrimaryButton {
                                    text: qsTr("Check connection")
                                    iconName: "activity"
                                    quiet: true
                                    enabled: stageCard.stageSession && stageCard.stageSession.active && !stageCard.stageSession.checking
                                    onClicked: root.dubbing.checkWorkflowColabStage(stageCard.stageId)
                                }
                                PrimaryButton {
                                    text: qsTr("Disconnect")
                                    iconName: "close"
                                    quiet: true
                                    visible: stageCard.stageSession && stageCard.stageSession.active
                                    enabled: stageCard.stageSession && !stageCard.stageSession.checking
                                    onClicked: root.dubbing.disconnectWorkflowColabStage(stageCard.stageId)
                                }
                                Item { Layout.fillWidth: true }
                                Text {
                                    text: stageCard.modelData.snapshotValid ? qsTr("Session snapshot valid") : qsTr("No valid session snapshot")
                                    color: stageCard.modelData.snapshotValid ? Theme.success : Theme.textSecondary
                                    font.pixelSize: 10
                                }
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
