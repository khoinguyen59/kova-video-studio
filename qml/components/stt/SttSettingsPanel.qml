import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LAStudio
import "../shared/settings"
import "../base"

Rectangle {
    id: root
    Layout.preferredWidth: visible ? 300 : 0
    Layout.fillHeight: true
    color: Theme.surface
    radius: Theme.radiusLarge
    clip: true

    property var sttSession: null
    property var family: null
    property var dynamicSettings: ({})
    property var capabilitySchema: []
    property var basicSchema: []
    property var advancedSchema: []
    property var studioConfig: ({})
    property bool advancedOpen: false
    readonly property bool hasLanguageInput: studioConfig && studioConfig.inputs ? studioConfig.inputs.indexOf("language") !== -1 : true
    component ColabField: TextField {
        Layout.fillWidth: true
        color: Theme.textPrimary
        placeholderTextColor: Theme.textSecondary
        font.pixelSize: Theme.fontSmall
        padding: Theme.paddingSmall
        selectByMouse: true
        background: Rectangle {
            radius: Theme.radiusSmall
            color: Qt.rgba(1, 1, 1, 0.035)
            border.color: parent.activeFocus ? Theme.accent : Qt.rgba(1, 1, 1, 0.09)
            border.width: parent.activeFocus ? 2 : 1
        }
    }


    signal closeRequested()

    function splitSchema(schema, advanced) {
        var result = []
        for (var i = 0; i < schema.length; ++i) {
            var item = schema[i] || {}
            if (!!item.advanced === advanced) result.push(item)
        }
        return result
    }

    function refreshFromCatalog() {
        var schema = []
        var studio = (family && family.studio && family.studio.stt) ? family.studio.stt : {}
        var definitions = (family && family.parameterDefinitions) ? family.parameterDefinitions : {}
        var ids = studio.parameters || []
        if (!ids || ids.length === 0) ids = ["threads", "translate"]
        for (var i = 0; i < ids.length; ++i) {
            var id = ids[i]
            var def = definitions[id]
            if (!def) continue
            var item = {}
            for (var k in def) item[k] = def[k]
            item.id = id
            if (id === "threads") {
                item.min = 0
                item.default = 0
                item.autoLabel = qsTr("Auto")
            }
            schema.push(item)
        }

        capabilitySchema = schema
        studioConfig = studio
        basicSchema = splitSchema(schema, false)
        advancedSchema = splitSchema(schema, true)

        var defaults = {}
        for (var j = 0; j < schema.length; ++j) {
            var p = schema[j] || {}
            if (p.id && p["default"] !== undefined) defaults[p.id] = p["default"]
        }
        if (root.sttSession) {
            defaults["threads"] = root.sttSession.threads
            defaults["translate"] = root.sttSession.translate
            defaults["language"] = root.sttSession.language
        }
        dynamicSettings = defaults
        if (root.sttSession) root.sttSession.dynamicSettings = dynamicSettings
    }

    function updateDynamicSetting(parameterId, value) {
        var settings = JSON.parse(JSON.stringify(dynamicSettings))
        settings[parameterId] = value
        dynamicSettings = settings
        if (root.sttSession) root.sttSession.dynamicSettings = dynamicSettings

        if (!root.sttSession) return
        if (parameterId === "threads") {
            var n = Math.max(0, Math.min(64, Math.round(Number(value))))
            if (root.sttSession.threads !== n) root.sttSession.threads = n
            return
        }
        if (parameterId === "translate") {
            var b = !!value
            if (root.sttSession.translate !== b) root.sttSession.translate = b
        }
    }

    Component.onCompleted: refreshFromCatalog()

    onFamilyChanged: refreshFromCatalog()

    Connections {
        target: root.sttSession
        ignoreUnknownSignals: true
        function onThreadsChanged() {
            if (root.dynamicSettings["threads"] !== root.sttSession.threads) root.updateDynamicSetting("threads", root.sttSession.threads)
        }
        function onTranslateChanged() {
            if (root.dynamicSettings["translate"] !== root.sttSession.translate) root.updateDynamicSetting("translate", root.sttSession.translate)
        }
        function onLanguageChanged() {
            if (langSelector.language !== root.sttSession.language) langSelector.language = root.sttSession.language
            if (root.dynamicSettings["language"] !== root.sttSession.language) {
                root.updateDynamicSetting("language", root.sttSession.language)
            }
        }
    }

    Behavior on Layout.preferredWidth {
        NumberAnimation { duration: 250; easing.type: Easing.InOutQuad }
    }

    ColumnLayout {
        width: 300 // Keep internal width fixed to avoid layout jumping
        height: parent.height
        anchors.left: parent.left
        anchors.margins: 0
        
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.paddingLarge
            spacing: Theme.paddingLarge

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: qsTr("Model Settings")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontLarge
                    font.bold: true
                    Layout.fillWidth: true
                }
                
                Button {
                    id: closeButton
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    flat: true
                    onClicked: root.closeRequested()
                    contentItem: LineIcon {
                        name: "close"
                        color: Theme.textSecondary
                        anchors.centerIn: parent
                        width: 14; height: 14
                    }
                    background: Rectangle {
                        radius: 16
                        color: closeButton.hovered ? Qt.rgba(1,1,1,0.05) : "transparent"
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.surfaceAlt }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                    width: parent.width - 4
                    spacing: Theme.paddingMedium

                    SettingsSection {
                        title: qsTr("Core")
                        iconName: "file"
                        visible: root.hasLanguageInput

                        LanguageSelector {
                            id: langSelector
                            Layout.fillWidth: true
                            family: root.family
                            hasLanguageInput: root.hasLanguageInput
                            useTextFieldFallback: true
                            language: root.sttSession ? root.sttSession.language : "auto"
                            onLanguageSelected: function(language) {
                                if (root.sttSession && root.sttSession.language !== language) {
                                    root.sttSession.language = language
                                }
                            }
                        }
                    }

                    SettingsSection {
                        title: qsTr("Colab GPU Worker")
                        iconName: "cloud"

                        Text { Layout.fillWidth: true; text: qsTr("This direct temporary worker is independent of API Gateway."); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall; wrapMode: Text.WordWrap }
                        Text {
                            Layout.fillWidth: true
                            text: root.sttSession && root.sttSession.colabModel !== ""
                                  ? qsTr("Selected Colab model: %1").arg(root.sttSession.colabModel)
                                  : qsTr("No Colab model selected. Open Load Model and use Select for Colab.")
                            color: root.sttSession && root.sttSession.colabModel !== "" ? Theme.success : Theme.warning
                            font.pixelSize: Theme.fontSmall
                            font.bold: true
                            wrapMode: Text.WordWrap
                        }
                        ColabNotebookLink {
                            notebookFile: root.sttSession ? root.sttSession.colabNotebookFile : ""
                        }
                        Text { text: qsTr("Worker URL"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                        ColabField {
                            id: colabUrl
                            text: AppController.colabSttSession.workerUrl
                            placeholderText: qsTr("https://….trycloudflare.com")
                        }
                        Text { text: qsTr("Session token"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                        ColabField {
                            id: colabToken
                            echoMode: TextInput.Password
                            placeholderText: root.sttSession && root.sttSession.colabActive ? qsTr("Connected — enter token to replace") : qsTr("Temporary token from Colab")
                        }
                        ColabSessionStatus {
                            session: AppController.colabSttSession
                        }
                        PrimaryButton {
                            Layout.fillWidth: true
                            text: AppController.colabSttSession.checking
                                  ? qsTr("Verifying CUDA and exact model...")
                                  : (root.sttSession && root.sttSession.colabActive
                                     ? qsTr("Using Colab GPU")
                                     : qsTr("Use or connect Colab GPU"))
                            iconName: root.sttSession && root.sttSession.colabActive ? "check" : "cloud"
                            enabled: root.sttSession && root.sttSession.colabModel !== ""
                                     && !AppController.colabSttSession.checking
                            onClicked: {
                                if (!root.sttSession) return
                                if (root.sttSession.colabPaired) {
                                    root.sttSession.useColab()
                                } else if (root.sttSession.connectColab(colabUrl.text.trim(), colabToken.text)) {
                                    colabToken.text = ""
                                }
                            }
                        }
                        PrimaryButton {
                            Layout.fillWidth: true
                            visible: root.sttSession && root.sttSession.colabPaired
                            text: qsTr("Disconnect Colab worker")
                            iconName: "close"
                            quiet: true
                            onClicked: if (root.sttSession) root.sttSession.disconnectColab()
                        }
                    }

                    SettingsSection {
                        title: qsTr("API Gateway STT")
                        iconName: "cloud"

                        Text { Layout.fillWidth: true; text: qsTr("This is a separate Gateway path and never uses the Colab session."); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall; wrapMode: Text.WordWrap }
                        Text { text: qsTr("Gateway URL"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                        ColabField {
                            text: AppController.settings.gatewayUrl
                            placeholderText: qsTr("https://gateway.example/v1")
                            onEditingFinished: AppController.settings.gatewayUrl = text.trim()
                        }
                        Text { text: qsTr("API key"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                        ColabField {
                            id: gatewayKey
                            echoMode: TextInput.Password
                            placeholderText: AppController.settings.gatewayApiKeyConfigured ? qsTr("API key saved — enter to replace") : qsTr("Stored encrypted on this device")
                            onEditingFinished: {
                                if (text.trim() !== "") {
                                    AppController.settings.setGatewayApiKey(text)
                                    text = ""
                                }
                            }
                        }
                        Text { text: qsTr("STT model"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                        ColabField {
                            text: root.sttSession ? root.sttSession.gatewayModel : ""
                            placeholderText: qsTr("Gateway STT model ID")
                            onEditingFinished: if (root.sttSession) root.sttSession.gatewayModel = text.trim()
                        }
                        PrimaryButton {
                            Layout.fillWidth: true
                            text: root.sttSession && root.sttSession.gatewayActive ? qsTr("Disconnect API Gateway") : qsTr("Use API Gateway STT")
                            iconName: root.sttSession && root.sttSession.gatewayActive ? "close" : "cloud"
                            onClicked: {
                                if (!root.sttSession) return
                                if (root.sttSession.gatewayActive) root.sttSession.disconnectGateway()
                                else root.sttSession.useGateway()
                            }
                        }
                    }

                    SettingsSection {
                        title: qsTr("Model Parameters")
                        iconName: "sliders"
                        visible: root.basicSchema.length > 0

                        ModelParameterControls {
                            schema: root.basicSchema
                            dynamicSettings: root.dynamicSettings
                            onParameterChanged: function(parameterId, value) {
                                root.updateDynamicSetting(parameterId, value)
                            }
                        }
                    }

                    CollapsibleSettingsSection {
                        title: qsTr("Advanced")
                        iconName: "sliders"
                        visible: root.advancedSchema.length > 0
                        expanded: root.advancedOpen
                        onToggled: root.advancedOpen = !root.advancedOpen

                        ModelParameterControls {
                            schema: root.advancedSchema
                            dynamicSettings: root.dynamicSettings
                            onParameterChanged: function(parameterId, value) {
                                root.updateDynamicSetting(parameterId, value)
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
