import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../base"
import LAStudio

Rectangle {
    id: root

    required property var dubbing
    required property string nodeId
    required property var node
    required property string nodeTitle
    required property bool canRun
    required property bool canRerun
    required property bool runReady
    required property string nextNodeId
    required property bool nextReady

    signal configureRequested()
    signal loadRequested()
    signal unloadRequested()
    signal reloadRequested()
    signal runRequested()
    signal nextRequested()
    signal fixRequested()

    Layout.fillWidth: true
    Layout.preferredHeight: root.remoteRouteConfigurable() ? 126 : 66
    radius: Theme.radiusSmall
    color: Theme.surfaceAlt
    border.color: Qt.rgba(1, 1, 1, 0.08)

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.paddingSmall
        anchors.bottomMargin: root.remoteRouteConfigurable() ? Theme.paddingXL + Theme.paddingMedium : Theme.paddingSmall
        spacing: Theme.paddingSmall
        LineIcon { name: "settings"; color: Theme.accentLight; Layout.preferredWidth: 16; Layout.preferredHeight: 16 }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            Text { text: root.node && root.node.configurable ? qsTr("%1 settings").arg(root.nodeTitle) : qsTr("%1 actions").arg(root.nodeTitle); color: Theme.textPrimary; font.pixelSize: Theme.fontSmall; font.bold: true }
            Text { Layout.fillWidth: true; text: root.node && root.node.providerName ? root.node.providerName : qsTr("Use the workflow default model"); color: Theme.textSecondary; font.pixelSize: 10; elide: Text.ElideRight }
        }
        Text { visible: root.node && root.node.configurable === true; text: root.modelStateLabel(); color: root.modelStateColor(); font.pixelSize: 10; font.bold: true; Layout.preferredWidth: 62; horizontalAlignment: Text.AlignRight }
        PrimaryButton {
            text: root.modelActionText()
            iconName: root.modelActionIcon()
            iconOnly: root.modelState() === 3
            toolTip: root.modelState() === 3 ? root.modelActionText() : ""
            visible: root.node && root.node.configurable === true
            Layout.preferredWidth: root.modelState() === 3 ? 38 : 132
            Layout.minimumWidth: root.modelState() === 3 ? 38 : 132
            enabled: !root.lifecycleBusy()
            quiet: root.modelState() === 3
            onClicked: root.runModelAction()
        }
        PrimaryButton { iconName: "reload"; iconOnly: true; toolTip: qsTr("Reload model"); quiet: true; visible: root.canReload(); enabled: !root.lifecycleBusy(); onClicked: root.reloadRequested() }
        PrimaryButton { iconName: "power"; iconOnly: true; toolTip: qsTr("Unload model"); quiet: true; visible: root.canUnload(); enabled: !root.lifecycleBusy(); onClicked: root.unloadRequested() }
        PrimaryButton { visible: root.canRun; iconName: "play"; iconOnly: true; toolTip: qsTr("Run"); enabled: !root.dubbing.processing && root.runReady; onClicked: root.runRequested() }
        PrimaryButton { visible: root.canRerun; iconName: "run-again"; iconOnly: true; toolTip: qsTr("Run again"); quiet: true; enabled: !root.dubbing.processing && root.runReady; onClicked: root.runRequested() }
        PrimaryButton {
            visible: root.nodeId === "translate"
                     && root.dubbing.dubbingQuality !== "fast"
                     && root.dubbing.adaptiveProvider !== "local"
                     && root.dubbing.translationFixCandidateCount > 0
            text: qsTr("Fix %1").arg(root.dubbing.translationFixCandidateCount)
            iconName: "spark"
            quiet: true
            loading: root.dubbing.translationFixing
            enabled: !root.dubbing.processing
            onClicked: root.fixRequested()
        }
        PrimaryButton { visible: root.nextNodeId !== "" && root.nextReady; text: qsTr("Next"); iconName: "chevron-right"; enabled: !root.dubbing.processing; onClicked: root.nextRequested() }
    }

    RowLayout {
        visible: root.remoteRouteConfigurable()
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.paddingSmall
        spacing: Theme.paddingSmall

        Text { text: qsTr("Route"); color: Theme.textSecondary; font.pixelSize: 10 }
        ComboBox {
            id: translationProvider
            Layout.preferredWidth: 132
            textRole: "label"
            model: root.nodeId === "source-separate"
                ? [
                    { "id": "local-dev", "label": qsTr("Local Dev") },
                    { "id": "colab-direct", "label": qsTr("Colab GPU") }
                  ]
                : [
                    { "id": "local-dev", "label": qsTr("Local Dev") },
                    { "id": "api-gateway", "label": qsTr("API Gateway") },
                    { "id": "colab-direct", "label": qsTr("Colab GPU") }
                  ]
            currentIndex: {
                var requested = (root.node && root.node.parameters && root.node.parameters.executionProvider) || "local-dev"
                for (var i = 0; i < model.length; ++i) if (model[i].id === requested) return i
                return 0
            }
            enabled: !root.dubbing.processing
            onActivated: function(index) {
                var provider = model[index].id
                if (provider === "colab-direct") {
                    var selected = root.currentRemoteModel()
                    if (root.dubbing.colabNotebookForNode(root.nodeId, selected) === "")
                        selected = root.dubbing.defaultColabModelForNode(root.nodeId)
                    if (root.dubbing.selectWorkflowColabModel(root.nodeId, selected))
                        root.openRemoteConfiguration(provider)
                    return
                }
                root.dubbing.setWorkflowNodeParameters(
                    root.nodeId,
                    { "executionProvider": provider, "modelId": "" })
                if (provider === "api-gateway")
                    root.openRemoteConfiguration(provider)
            }
        }
        ComboBox {
            id: remoteModelPicker
            visible: translationProvider.currentIndex !== 0 && root.remoteModelOptions().length > 0
            Layout.fillWidth: true
            textRole: "displayName"
            model: root.remoteModelOptions()
            currentIndex: {
                var selectedModel = (root.node && root.node.parameters && root.node.parameters.modelId) || ""
                for (var i = 0; i < model.length; ++i) if (model[i].modelId === selectedModel) return i
                return -1
            }
            enabled: !root.dubbing.processing
            onActivated: function(index) {
                var provider = (root.node && root.node.parameters
                                && root.node.parameters.executionProvider) || "local-dev"
                if (provider === "colab-direct")
                    root.dubbing.selectWorkflowColabModel(root.nodeId,
                                                          model[index].modelId)
                else
                    root.dubbing.setWorkflowNodeParameters(
                        root.nodeId, { "modelId": model[index].modelId })
            }
        }
        TextField {
            id: translationModel
            visible: root.currentExecutionProvider() === "api-gateway"
                     && root.remoteModelOptions().length === 0
                     && !root.remoteCatalogAvailable()
            Layout.fillWidth: true
            color: Theme.textPrimary
            placeholderText: translationProvider.currentIndex === 0
                ? qsTr("Local model selected above")
                : qsTr("Refresh the selected provider's model catalog")
            text: (root.node && root.node.parameters && root.node.parameters.modelId) || ""
            enabled: !root.dubbing.processing && translationProvider.currentIndex !== 0
            selectByMouse: true
            onEditingFinished: root.dubbing.setWorkflowNodeParameters(root.nodeId,
                                                                        { "modelId": text.trim() })
            background: Rectangle {
                radius: Theme.radiusSmall
                color: Qt.rgba(1, 1, 1, 0.035)
                border.color: translationModel.activeFocus ? Theme.accent : Qt.rgba(1, 1, 1, 0.09)
            }
        }
        Text {
            visible: root.currentExecutionProvider() === "api-gateway"
                     && root.remoteModelOptions().length === 0
                     && root.remoteCatalogAvailable()
            Layout.fillWidth: true
            text: qsTr("No compatible model is currently available for this node.")
            color: Theme.warning
            font.pixelSize: 10
            wrapMode: Text.WordWrap
        }
        PrimaryButton {
            visible: translationProvider.currentIndex !== 0
            text: qsTr("Configure route")
            iconName: "settings"
            quiet: true
            enabled: !root.dubbing.processing
            onClicked: root.openRemoteConfiguration(translationProvider.model[translationProvider.currentIndex].id)
        }
    }

    Dialog {
        id: apiGatewayDialog
        parent: Overlay.overlay
        modal: true
        title: qsTr("API Gateway for %1").arg(root.nodeTitle)
        width: Math.min(520, Overlay.overlay.width - Theme.paddingXL * 2)
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel

        onOpened: {
            apiGatewayUrl.text = AppController.settings.gatewayUrl
            apiGatewayKey.text = ""
            apiGatewayError.text = ""
        }
        onAccepted: {
            var url = apiGatewayUrl.text.trim()
            var key = apiGatewayKey.text.trim()
            if (url === "") {
                apiGatewayError.text = qsTr("Enter the API Gateway URL.")
                apiGatewayDialog.open()
                return
            }
            AppController.settings.gatewayUrl = url
            if (key !== "") AppController.settings.setGatewayApiKey(key)
            if (!AppController.settings.gatewayApiKeyConfigured) {
                apiGatewayError.text = qsTr("Enter an API key, or configure one in Settings first.")
                apiGatewayDialog.open()
                return
            }
            root.updateRemoteModel(apiGatewayModel.text)
        }

        contentItem: ColumnLayout {
            spacing: Theme.paddingSmall
            Text {
                Layout.fillWidth: true
                text: qsTr("Use the shared Gateway configuration below, or enter it here for this workflow. The key is stored securely on this device and is not sent to Colab.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
            Text { text: qsTr("Gateway URL"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
            TextField {
                id: apiGatewayUrl
                Layout.fillWidth: true
                placeholderText: qsTr("https://gateway.example/v1")
                selectByMouse: true
            }
            Text { text: qsTr("API key"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
            TextField {
                id: apiGatewayKey
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: AppController.settings.gatewayApiKeyConfigured
                                 ? qsTr("Saved key available — enter to replace")
                                 : qsTr("Enter API key")
                selectByMouse: true
            }
            Text { text: qsTr("Model ID"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
            TextField {
                id: apiGatewayModel
                Layout.fillWidth: true
                text: root.currentRemoteModel()
                placeholderText: qsTr("Model exposed by the API Gateway")
                selectByMouse: true
            }
            Text {
                id: apiGatewayError
                Layout.fillWidth: true
                visible: text !== ""
                color: Theme.danger
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
        }
    }

    Dialog {
        id: colabWorkerDialog
        parent: Overlay.overlay
        modal: true
        property bool awaitingVerification: false
        title: qsTr("Colab GPU for %1").arg(root.nodeTitle)
        width: Math.min(520, Overlay.overlay.width - Theme.paddingXL * 2)
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel

        onOpened: {
            var selected = root.currentRemoteModel()
            if (root.dubbing.colabNotebookForNode(root.nodeId, selected) === "") {
                selected = root.dubbing.defaultColabModelForNode(root.nodeId)
                root.dubbing.selectWorkflowColabModel(root.nodeId, selected)
            }
            var session = root.colabSessionForNode()
            colabWorkerUrl.text = session ? session.workerUrl : ""
            if (!awaitingVerification) {
                colabWorkerToken.text = ""
                colabWorkerError.text = ""
            }
        }
        onAccepted: {
            var selected = root.currentRemoteModel()
            if (!root.dubbing.selectWorkflowColabModel(root.nodeId, selected)) {
                colabWorkerError.text = qsTr("Select one of the exact Colab models listed for this node.")
                colabWorkerDialog.open()
                return
            }
            var session = root.colabSessionForNode()
            if (!session) {
                colabWorkerError.text = qsTr("This workflow node has no Colab worker route.")
                colabWorkerDialog.open()
                return
            }
            if (!session.connectTemporaryWorker(
                    colabWorkerUrl.text.trim(), colabWorkerToken.text,
                    root.colabCapabilityForNode(), selected)) {
                colabWorkerError.text = session.lastError
                colabWorkerDialog.open()
                return
            }
            awaitingVerification = true
            colabWorkerToken.text = ""
        }
        onRejected: {
            if (!awaitingVerification) return
            awaitingVerification = false
            var session = root.colabSessionForNode()
            if (session && session.checking) session.disconnectTemporaryWorker()
        }
        onClosed: {
            var session = root.colabSessionForNode()
            if (awaitingVerification && session && session.checking) {
                Qt.callLater(function() {
                    if (colabWorkerDialog.awaitingVerification && session.checking)
                        colabWorkerDialog.open()
                })
            }
        }

        contentItem: ColumnLayout {
            spacing: Theme.paddingSmall
            Text {
                Layout.fillWidth: true
                text: qsTr("Run this GPU node directly on its matching Colab worker. The temporary URL and token stay only in memory and are never forwarded through API Gateway.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
            ColabNotebookLink { notebookFile: root.colabNotebookForNode() }
            Text { text: qsTr("Worker URL"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
            TextField {
                id: colabWorkerUrl
                Layout.fillWidth: true
                placeholderText: qsTr("https://…trycloudflare.com")
                selectByMouse: true
            }
            Text { text: qsTr("Session token"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
            TextField {
                id: colabWorkerToken
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: qsTr("Temporary token from Colab")
                selectByMouse: true
            }
            ColabSessionStatus {
                session: root.colabSessionForNode()
            }
            Text { text: qsTr("Exact worker model"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
            Text {
                id: colabWorkerModel
                Layout.fillWidth: true
                text: root.currentRemoteModel()
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WrapAnywhere
            }
            Text {
                Layout.fillWidth: true
                text: root.colabNotebookForNode() === ""
                      ? qsTr("Choose a model before opening Colab.")
                      : qsTr("This notebook loads only the selected model and rejects mismatched requests.")
                color: root.colabNotebookForNode() === "" ? Theme.warning : Theme.textSecondary
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
            Text {
                id: colabWorkerError
                Layout.fillWidth: true
                visible: text !== ""
                color: Theme.danger
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
        }
    }

    Connections {
        target: root.colabSessionForNode()
        function onVerificationFinished(success, message) {
            if (!colabWorkerDialog.awaitingVerification) return
            colabWorkerDialog.awaitingVerification = false
            if (success) {
                colabWorkerDialog.close()
                return
            }
            colabWorkerError.text = message
            if (!colabWorkerDialog.visible) colabWorkerDialog.open()
        }
    }

    function modelState() { return root.node && root.node.modelState !== undefined ? root.node.modelState : 0 }
    function remoteRouteConfigurable() { return root.nodeId === "source-separate" || root.nodeId === "transcribe" || root.nodeId === "translate" || root.nodeId === "synthesize" }
    function remoteCapabilityId() {
        if (root.nodeId === "source-separate") return "voice-isolation"
        if (root.nodeId === "transcribe") return "stt"
        if (root.nodeId === "translate") return "translation"
        if (root.nodeId === "synthesize") return "tts"
        return ""
    }
    function remoteCatalogAvailable() {
        var provider = (root.node && root.node.parameters && root.node.parameters.executionProvider) || "local-dev"
        if (provider === "api-gateway") return AppController.remoteModels.gatewayAvailable
        if (provider === "colab-direct") return true
        return false
    }
    function currentRemoteModel() {
        return (root.node && root.node.parameters && root.node.parameters.modelId) || ""
    }
    function currentExecutionProvider() {
        return (root.node && root.node.parameters
                && root.node.parameters.executionProvider) || "local-dev"
    }
    function updateRemoteModel(modelId) {
        var provider = (root.node && root.node.parameters && root.node.parameters.executionProvider) || "local-dev"
        if (provider === "colab-direct")
            root.dubbing.selectWorkflowColabModel(root.nodeId, modelId.trim())
        else
            root.dubbing.setWorkflowNodeParameters(root.nodeId,
                                                   { "modelId": modelId.trim() })
    }
    function colabSessionForNode() {
        if (root.nodeId === "source-separate") return AppController.colabSeparationSession
        if (root.nodeId === "transcribe") return AppController.colabSttSession
        if (root.nodeId === "translate") return AppController.colabTranslationSession
        if (root.nodeId === "synthesize") return AppController.colabTtsSession
        return null
    }
    function colabCapabilityForNode() {
        if (root.nodeId === "source-separate") return "voice-isolation"
        if (root.nodeId === "transcribe") return "stt"
        if (root.nodeId === "translate") return "translation"
        if (root.nodeId === "synthesize") return "tts"
        return ""
    }
    function colabNotebookForNode() {
        return root.dubbing.colabNotebookForNode(root.nodeId,
                                                 root.currentRemoteModel())
    }
    function openRemoteConfiguration(provider) {
        if (provider === "colab-direct") {
            colabWorkerDialog.open()
            return
        }
        if (provider === "api-gateway"
                && (AppController.settings.gatewayUrl === ""
                    || !AppController.settings.gatewayApiKeyConfigured)) {
            apiGatewayDialog.open()
        }
    }
    function remoteModelOptions() {
        var provider = (root.node && root.node.parameters && root.node.parameters.executionProvider) || "local-dev"
        if (provider === "colab-direct")
            return root.dubbing.colabModelOptionsForNode(root.nodeId)
        var candidates = provider === "api-gateway"
                       ? AppController.remoteModels.gatewayModels : []
        var capability = root.remoteCapabilityId()
        var options = []
        for (var i = 0; i < candidates.length; ++i) {
            var item = candidates[i]
            if (item.selectable !== true) continue
            var advertised = String(item.capability || "")
            var matches = advertised === "" || advertised === capability
                       || (advertised === "llm" && (capability === "translation" || capability === "llm-chat"))
            if (!matches) continue
            options.push(item)
        }
        return options
    }
    function lifecycleBusy() { return [2, 4, 5].indexOf(root.modelState()) >= 0 || root.dubbing.processing }
    function canLoad() { return root.node && root.node.configurable === true && [1, 6].indexOf(root.modelState()) >= 0 }
    function canReload() { return root.node && root.node.configurable === true && root.modelState() === 3 }
    function canUnload() { return root.node && root.node.configurable === true && [3, 6].indexOf(root.modelState()) >= 0 }
    function modelActionText() {
        var state = root.modelState()
        if (state === 0) return qsTr("Open model")
        if ([1, 6].indexOf(state) >= 0) return qsTr("Load model")
        return qsTr("Change model")
    }
    function modelActionIcon() {
        var state = root.modelState()
        if (state === 0) return "gallery"
        if ([1, 6].indexOf(state) >= 0) return "download"
        return "settings"
    }
    function runModelAction() {
        if ([1, 6].indexOf(root.modelState()) >= 0) root.loadRequested()
        else root.configureRequested()
    }
    function modelStateLabel() {
        var labels = [qsTr("Unconfigured"), qsTr("Unloaded"), qsTr("Loading"), qsTr("Ready"), qsTr("Running"), qsTr("Unloading"), qsTr("Error")]
        return labels[root.modelState()] || qsTr("Unknown")
    }
    function modelStateColor() {
        var state = root.modelState()
        return state === 3 ? Theme.success : (state === 6 ? Theme.danger : Theme.warning)
    }
}
