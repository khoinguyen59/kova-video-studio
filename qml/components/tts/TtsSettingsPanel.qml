import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LAStudio
import "../shared/settings"
import "../base"

ColumnLayout {
    id: root

    property var family: null
    property var dynamicSettings: ({})
    property string selectedLanguage: "en"
    property string styleInstruction: ""
    property bool denoise: true
    property bool preprocessPrompt: true
    property bool randomSeed: true
    property int customSeed: 42
    property string suggestedLanguage: "en"
    property string backendType: ""  // "kokoro", "vibevoice", or "" (omnivoice)
    property bool locked: false
    property bool showGatewaySettings: true

    property var capabilitySchema: []
    property var basicSchema: []
    property var advancedSchema: []
    property var studioConfig: ({})
    property bool advancedOpen: false
    readonly property bool hasLanguageInput: studioConfig && studioConfig.inputs ? studioConfig.inputs.indexOf("language") !== -1 : false
    readonly property bool hasInstructInput: studioConfig && studioConfig.inputs ? studioConfig.inputs.indexOf("instruct") !== -1 : false

    function splitSchema(schema, advanced) {
        var result = []
        for (var i = 0; i < schema.length; ++i) {
            var item = schema[i] || {}
            if (!!item.advanced === advanced) {
                result.push(item)
            }
        }
        return result
    }

    function isValidParameterValue(param, value) {
        if (value === undefined || value === null)
            return false
        if (!param || !param.id)
            return false
        if (param.type === "choice") {
            var choices = param.choices || param.options || []
            if (choices.length === 0)
                return true
            for (var i = 0; i < choices.length; ++i) {
                if (choices[i] && choices[i].value === value)
                    return true
            }
            return false
        }
        return true
    }

    function refreshCapabilityMetadata(resetValues) {
        var shouldReset = resetValues === true
        var previousSettings = shouldReset ? {} : (root.dynamicSettings || {})
        var previousInstruct = shouldReset ? "" : root.styleInstruction
        root.capabilitySchema = AppController.tts.schemaForCapability("tts")
        root.studioConfig = AppController.tts.studioConfigForCapability("tts")
        root.basicSchema = splitSchema(root.capabilitySchema, false)
        root.advancedSchema = splitSchema(root.capabilitySchema, true)
        var mergedSettings = {}
        for (var i = 0; i < root.capabilitySchema.length; ++i) {
            var param = root.capabilitySchema[i] || {}
            if (!param.id)
                continue
            if (isValidParameterValue(param, previousSettings[param.id])) {
                mergedSettings[param.id] = previousSettings[param.id]
            } else if (param["default"] !== undefined) {
                mergedSettings[param.id] = param["default"]
            }
        }
        root.dynamicSettings = mergedSettings
        if (root.hasInstructInput) {
            root.styleInstruction = previousInstruct
            instructInput.text = root.styleInstruction
        } else {
            root.styleInstruction = ""
            instructInput.text = ""
        }
    }

    Component.onCompleted: refreshCapabilityMetadata(true)

    Connections {
        target: AppController.tts
        function onFamilyConfigChanged() { root.refreshCapabilityMetadata(true) }
        function onSchemaChanged() { root.refreshCapabilityMetadata(false) }
    }

    readonly property bool isKokoro: backendType === "kokoro"

    component GatewayField: TextField {
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

    signal settingsChanged()
    signal closeRequested()

    function getSynthesisSettings() {
        var settings = {}
        for (var i = 0; i < root.capabilitySchema.length; ++i) {
            var param = root.capabilitySchema[i] || {}
            if (!param.id) continue
            if (root.dynamicSettings[param.id] !== undefined) {
                settings[param.id] = root.dynamicSettings[param.id]
            } else if (param["default"] !== undefined) {
                settings[param.id] = param["default"]
            }
        }
        if (root.hasLanguageInput) {
            settings["lang"] = root.selectedLanguage
        }
        if (root.hasInstructInput && root.styleInstruction.length > 0) {
            settings["instruct"] = root.styleInstruction
        }
        return settings
    }

    function updateDynamicSetting(parameterId, value) {
        var settings = JSON.parse(JSON.stringify(root.dynamicSettings))
        settings[parameterId] = value
        root.dynamicSettings = settings
        root.settingsChanged()
    }

    function applyExampleSettings(exampleSettings) {
        if (!exampleSettings) return
        var settings = JSON.parse(JSON.stringify(root.dynamicSettings))
        for (var i = 0; i < root.capabilitySchema.length; ++i) {
            var param = root.capabilitySchema[i] || {}
            if (param.id && exampleSettings[param.id] !== undefined) {
                settings[param.id] = exampleSettings[param.id]
            }
        }
        root.dynamicSettings = settings
        if (exampleSettings["lang"] !== undefined) {
            root.selectedLanguage = exampleSettings["lang"]
        }
        if (exampleSettings["instruct"] !== undefined) {
            root.styleInstruction = exampleSettings["instruct"]
            instructInput.text = root.styleInstruction
        }
        root.settingsChanged()
    }

    function getSettingsObject(inputText, referenceText) {
        return VoiceCloningUtils.buildCloneSettings(
            root.selectedLanguage,
            instructInput.text,
            referenceText,
            denoiseToggle.checked,
            preprocessToggle.checked,
            root.dynamicSettings,
            randomSeedToggle.checked,
            parseInt(customSeedInput.text) || 0
        )
    }

    onSuggestedLanguageChanged: {
        if (root.selectedLanguage !== suggestedLanguage) root.selectedLanguage = suggestedLanguage
    }

    spacing: Theme.paddingMedium
    Layout.fillWidth: true
    Layout.fillHeight: true

    RowLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: 36
        spacing: Theme.paddingSmall

        LineIcon {
            name: "sliders"
            color: Theme.accent
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
        }

        Text {
            text: qsTr("TTS Settings")
            color: Theme.textPrimary
            font.pixelSize: Theme.fontMedium
            font.bold: true
            Layout.fillWidth: true
        }

        Button {
            id: closeBtn
            implicitWidth: 30
            implicitHeight: 30
            flat: true

            AppToolTip {
                text: qsTr("Hide settings")
                visible: closeBtn.hovered
            }

            contentItem: LineIcon {
                name: "chevron-right"
                color: closeBtn.hovered ? Theme.accent : Theme.textSecondary
                anchors.centerIn: parent
                width: 16
                height: 16
            }
            background: Rectangle {
                color: closeBtn.hovered ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(1, 1, 1, 0.025)
                border.color: closeBtn.hovered ? Qt.rgba(0.49, 0.30, 1.0, 0.55) : Qt.rgba(1, 1, 1, 0.08)
                border.width: 1
                radius: 7
            }
            onClicked: root.closeRequested()
            HoverHandler { cursorShape: Qt.PointingHandCursor }
        }
    }

    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Qt.rgba(1, 1, 1, 0.07) }

    ScrollView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width - 16
            spacing: Theme.paddingMedium

            SettingsSection {
                title: qsTr("Core")
                visible: true // Always show Core for language selection if supported
                iconName: "file"

                LanguageSelector {
                    id: langSelector
                    Layout.fillWidth: true
                    labelText: qsTr("Target Language")
                    family: root.family
                    hasLanguageInput: root.hasLanguageInput
                    useTextFieldFallback: false
                    language: root.selectedLanguage
                    enabled: !root.locked
                    onLanguageSelected: function(language) {
                        if (root.selectedLanguage !== language) {
                            root.selectedLanguage = language
                            root.settingsChanged()
                        }
                    }
                }

                FieldLabel { 
                    text: (root.family && root.family.id && root.family.id.indexOf("qwen3") !== -1) ? qsTr("Style Instruction") : qsTr("Style & Emotion")
                    visible: instructInput.visible
                }

                TextField {
                    id: instructInput
                    Layout.fillWidth: true
                    visible: root.hasInstructInput
                    placeholderText: (root.family && root.family.id && root.family.id.indexOf("qwen3") !== -1)
                                     ? qsTr("e.g. Speak with excitement, whisper, or deep voice...") 
                                     : qsTr("happy, whisper, dramatic...")
                    color: Theme.textPrimary
                    placeholderTextColor: Theme.textSecondary
                    selectionColor: Theme.accent
                    selectedTextColor: "#ffffff"
                    enabled: !root.locked
                    background: Rectangle {
                        color: Qt.rgba(1, 1, 1, 0.035)
                        radius: 7
                        border.color: instructInput.activeFocus ? Qt.rgba(0.49, 0.30, 1.0, 0.75) : Qt.rgba(1, 1, 1, 0.08)
                        border.width: 1
                    }
                    padding: Theme.paddingMedium
                    onTextChanged: {
                        root.styleInstruction = text
                        root.settingsChanged()
                    }
                }
            }

            SettingsSection {
                title: qsTr("API Gateway TTS")
                iconName: "cloud"
                visible: root.showGatewaySettings

                Text { Layout.fillWidth: true; text: qsTr("This independent route uses API Gateway only; it never uses a Colab worker or token."); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall; wrapMode: Text.WordWrap }
                Text { text: qsTr("Gateway URL"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                GatewayField {
                    text: AppController.settings.gatewayUrl
                    placeholderText: qsTr("https://gateway.example/v1")
                    onEditingFinished: AppController.settings.gatewayUrl = text.trim()
                }
                Text { text: qsTr("API key"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                GatewayField {
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
                Text { text: qsTr("TTS model"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                GatewayField {
                    text: AppController.gatewayTts.gatewayModel
                    placeholderText: qsTr("OpenAI-compatible TTS model")
                    onEditingFinished: AppController.gatewayTts.gatewayModel = text.trim()
                }
                Text { text: qsTr("Voice"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                GatewayField {
                    text: AppController.gatewayTts.gatewayVoice
                    placeholderText: qsTr("alloy")
                    onEditingFinished: AppController.gatewayTts.gatewayVoice = text.trim()
                }
                PrimaryButton {
                    Layout.fillWidth: true
                    enabled: !root.locked
                    text: AppController.gatewayTts.gatewayActive ? qsTr("Use local TTS") : qsTr("Use API Gateway TTS")
                    iconName: AppController.gatewayTts.gatewayActive ? "close" : "cloud"
                    onClicked: {
                        if (AppController.gatewayTts.gatewayActive) AppController.gatewayTts.disconnectGateway()
                        else AppController.gatewayTts.useGateway()
                    }
                }
            }

            SettingsSection {
                title: qsTr("Model Parameters")
                iconName: "sliders"
                visible: root.basicSchema.length > 0

                ModelParameterControls {
                    enabled: !root.locked
                    schema: root.basicSchema
                    dynamicSettings: root.dynamicSettings
                    onParameterChanged: function(parameterId, value) { root.updateDynamicSetting(parameterId, value) }
                }
            }

            CollapsibleSettingsSection {
                title: qsTr("Advanced")
                iconName: "sliders"
                expanded: root.advancedOpen
                visible: root.advancedSchema.length > 0
                onToggled: root.advancedOpen = !root.advancedOpen

                ModelParameterControls {
                    enabled: !root.locked
                    schema: root.advancedSchema
                    dynamicSettings: root.dynamicSettings
                    onParameterChanged: function(parameterId, value) { root.updateDynamicSetting(parameterId, value) }
                }
            }

            SettingsSection {
                title: qsTr("Audio & System")
                visible: !root.isKokoro
                iconName: "cpu"

                ToggleRow {
                    id: denoiseToggle
                    text: qsTr("Denoise")
                    checked: true
                    enabled: !root.locked
                    onCheckedChanged: {
                        root.denoise = checked
                        root.settingsChanged()
                    }
                }

                ToggleRow {
                    id: preprocessToggle
                    text: qsTr("Preprocess prompt")
                    checked: true
                    enabled: !root.locked
                    onCheckedChanged: {
                        root.preprocessPrompt = checked
                        root.settingsChanged()
                    }
                }

                ToggleRow {
                    id: randomSeedToggle
                    text: qsTr("Random seed")
                    checked: true
                    enabled: !root.locked
                    onCheckedChanged: {
                        root.randomSeed = checked
                        root.settingsChanged()
                    }
                }

                TextField {
                    id: customSeedInput
                    Layout.fillWidth: true
                    visible: !randomSeedToggle.checked
                    enabled: !root.locked
                    text: "42"
                    color: Theme.textPrimary
                    placeholderTextColor: Theme.textSecondary
                    validator: IntValidator { bottom: 0 }
                    background: Rectangle {
                        color: Qt.rgba(1, 1, 1, 0.035)
                        radius: 7
                        border.color: customSeedInput.activeFocus ? Qt.rgba(0.49, 0.30, 1.0, 0.85) : Qt.rgba(1, 1, 1, 0.08)
                        border.width: 1
                    }
                    padding: Theme.paddingMedium
                    onTextChanged: {
                        root.customSeed = parseInt(text) || 0
                        root.settingsChanged()
                    }
                }
            }

            Item { Layout.fillWidth: true; Layout.preferredHeight: Theme.paddingSmall }
        }
    }
}
