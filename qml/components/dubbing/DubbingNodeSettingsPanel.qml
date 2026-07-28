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
                root.dubbing.setWorkflowNodeParameters(root.nodeId,
                                                       { "executionProvider": model[index].id,
                                                         "modelId": "" })
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
                root.dubbing.setWorkflowNodeParameters(root.nodeId,
                                                       { "modelId": model[index].modelId })
            }
        }
        TextField {
            id: translationModel
            visible: translationProvider.currentIndex !== 0
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
            visible: translationProvider.currentIndex !== 0
                     && root.remoteModelOptions().length === 0
                     && root.remoteCatalogAvailable()
            Layout.fillWidth: true
            text: qsTr("No compatible model is currently available for this node.")
            color: Theme.warning
            font.pixelSize: 10
            wrapMode: Text.WordWrap
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
        if (provider === "colab-direct") return AppController.remoteModels.colabAvailable
        return false
    }
    function remoteModelOptions() {
        var provider = (root.node && root.node.parameters && root.node.parameters.executionProvider) || "local-dev"
        var candidates = provider === "api-gateway" ? AppController.remoteModels.gatewayModels
                       : (provider === "colab-direct" ? AppController.remoteModels.colabModels : [])
        var capability = root.remoteCapabilityId()
        var options = []
        for (var i = 0; i < candidates.length; ++i) {
            var item = candidates[i]
            if (item.selectable !== true) continue
            if (provider === "colab-direct" && item.capability !== capability) continue
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
