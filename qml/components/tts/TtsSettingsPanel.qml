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
    property bool showColabSettings: true
    // This chooses the route used by the TTS studio.  It is intentionally
    // independent from either controller's connection/active state.
    property string selectedRemoteProvider: ""
    readonly property bool remoteFirstMode: AppController.settings.remoteFirstMode
    // A Voice Clone worker is a distinct, exact-model Direct Colab route. It
    // can generate from a saved reference/profile, whereas the ordinary TTS
    // worker remains independent and keeps its own URL/token.
    readonly property bool cloneOmniVoiceActive: AppController.colabVoiceClone
                                               && AppController.colabVoiceClone.colabActive
                                               && AppController.colabVoiceClone.model === "omnivoice"
    property var reusableCloneVoices: []
    property int reusableCloneVoiceIndex: -1
    property bool reusableCloneConsent: false

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

    function refreshReusableCloneVoices() {
        var voices = AppController.voiceClonePresets.presetsForFamily("omnivoice")
        var validVoices = []
        for (var i = 0; i < voices.length; ++i) {
            if (voices[i].valid)
                validVoices.push(voices[i])
        }
        var previousId = root.selectedReusableCloneVoiceId()
        root.reusableCloneVoices = validVoices
        root.reusableCloneVoiceIndex = -1
        for (var j = 0; j < validVoices.length; ++j) {
            if (validVoices[j].id === previousId) {
                root.reusableCloneVoiceIndex = j
                return
            }
        }
        if (validVoices.length > 0)
            root.reusableCloneVoiceIndex = 0
    }

    function selectedReusableCloneVoice() {
        if (root.reusableCloneVoiceIndex < 0
                || root.reusableCloneVoiceIndex >= root.reusableCloneVoices.length)
            return null
        return root.reusableCloneVoices[root.reusableCloneVoiceIndex]
    }

    function selectedReusableCloneVoiceId() {
        var voice = root.selectedReusableCloneVoice()
        return voice && voice.id ? voice.id : ""
    }

    function selectedReusableCloneVoiceName() {
        var voice = root.selectedReusableCloneVoice()
        return voice && voice.name ? voice.name : ""
    }

    Component.onCompleted: {
        refreshCapabilityMetadata(true)
        refreshReusableCloneVoices()
    }

    Connections {
        target: AppController.tts
        function onFamilyConfigChanged() { root.refreshCapabilityMetadata(true) }
        function onSchemaChanged() { root.refreshCapabilityMetadata(false) }
    }

    Connections {
        target: AppController.voiceClonePresets
        function onPresetsChanged(familyId) {
            if (familyId === "omnivoice")
                root.refreshReusableCloneVoices()
        }
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
    signal remoteProviderSelected(string provider)

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
                    enabled: !root.locked && !(root.remoteFirstMode && AppController.gatewayTts.gatewayActive && root.selectedRemoteProvider === "gateway")
                    text: root.remoteFirstMode
                          ? (AppController.gatewayTts.gatewayActive
                             ? (root.selectedRemoteProvider === "gateway" ? qsTr("API Gateway TTS selected") : qsTr("Select API Gateway TTS"))
                             : qsTr("Use API Gateway TTS"))
                          : (AppController.gatewayTts.gatewayActive && root.selectedRemoteProvider === "gateway"
                             ? qsTr("Use local TTS")
                             : (AppController.gatewayTts.gatewayActive ? qsTr("Select API Gateway TTS") : qsTr("Use API Gateway TTS")))
                    iconName: root.remoteFirstMode || !(AppController.gatewayTts.gatewayActive && root.selectedRemoteProvider === "gateway") ? "cloud" : "close"
                    onClicked: {
                        if (AppController.gatewayTts.gatewayActive && !root.remoteFirstMode && root.selectedRemoteProvider === "gateway") {
                            AppController.gatewayTts.disconnectGateway()
                            root.remoteProviderSelected("")
                        } else {
                            if (!AppController.gatewayTts.gatewayActive)
                                AppController.gatewayTts.useGateway()
                            if (AppController.gatewayTts.gatewayActive)
                                root.remoteProviderSelected("gateway")
                        }
                    }
                }
            }

            SettingsSection {
                title: qsTr("Reuse cloned OmniVoice")
                iconName: "users"
                visible: root.cloneOmniVoiceActive || (root.family && root.family.id === "omnivoice")

                Text {
                    Layout.fillWidth: true
                    text: root.cloneOmniVoiceActive
                          ? qsTr("The verified OmniVoice Voice Cloning Colab worker is ready. This uses its existing Direct Colab session; no Local model is downloaded and no second TTS notebook is required.")
                          : qsTr("Connect OmniVoice in Voice Cloning first. Once connected, TTS selects OmniVoice automatically and can reuse a named saved clone here.")
                    color: root.cloneOmniVoiceActive ? Theme.success : Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }

                Text { text: qsTr("Saved cloned voice"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                AppComboBox {
                    id: reusableCloneVoiceCombo
                    Layout.fillWidth: true
                    model: root.reusableCloneVoices
                    textRole: "name"
                    secondaryTextRole: "originalAudioName"
                    currentIndex: root.reusableCloneVoiceIndex
                    enabled: !root.locked && root.reusableCloneVoices.length > 0
                    onActivated: function(index) {
                        root.reusableCloneVoiceIndex = index
                        root.reusableCloneConsent = false
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: root.reusableCloneVoices.length === 0
                    text: qsTr("No reusable OmniVoice clone yet. In Voice Cloning, enter Voice name for TTS reuse and finish one clone with permission confirmed.")
                    color: Theme.warning
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }

                CheckBox {
                    id: reusableCloneConsentCheck
                    Layout.fillWidth: true
                    text: qsTr("I have permission to use this cloned voice for TTS")
                    checked: root.reusableCloneConsent
                    enabled: !root.locked && root.reusableCloneVoiceIndex >= 0
                    onCheckedChanged: root.reusableCloneConsent = checked
                }

                PrimaryButton {
                    Layout.fillWidth: true
                    enabled: !root.locked && root.cloneOmniVoiceActive
                             && root.reusableCloneVoiceIndex >= 0
                             && root.reusableCloneConsent
                    text: root.selectedRemoteProvider === "clone"
                          ? qsTr("OmniVoice clone route selected")
                          : qsTr("Use cloned OmniVoice in TTS")
                    iconName: "cloud"
                    onClicked: root.remoteProviderSelected("clone")
                }
            }

            SettingsSection {
                title: qsTr("Colab GPU TTS")
                iconName: "cloud"
                visible: root.showColabSettings

                Text { Layout.fillWidth: true; text: qsTr("This direct temporary worker is independent of API Gateway. Its token stays only in this desktop session."); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall; wrapMode: Text.WordWrap }
                Text {
                    Layout.fillWidth: true
                    text: AppController.colabTts.colabModel !== ""
                          ? qsTr("Selected Colab model: %1").arg(AppController.colabTts.colabModel)
                          : qsTr("No Colab model selected. Open Load Model and use Select for Colab.")
                    color: AppController.colabTts.colabModel !== "" ? Theme.success : Theme.warning
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
                ColabNotebookLink { notebookFile: AppController.colabTts.colabNotebookFile }
                Text { text: qsTr("Worker URL"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                GatewayField {
                    id: colabUrl
                    text: AppController.colabTtsSession.workerUrl
                    placeholderText: qsTr("https://…trycloudflare.com")
                }
                Text { text: qsTr("Session token"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                GatewayField {
                    id: colabToken
                    echoMode: TextInput.Password
                    placeholderText: AppController.colabTts.colabConnected ? qsTr("Connected — enter token to replace") : qsTr("Temporary token from Colab")
                }
                ColabSessionStatus {
                    session: AppController.colabTtsSession
                }
                Text { text: qsTr("Voice"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                GatewayField {
                    text: AppController.colabTts.colabVoice
                    placeholderText: qsTr("af_heart")
                    onEditingFinished: AppController.colabTts.colabVoice = text.trim()
                }
                Text { text: qsTr("Language"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                GatewayField {
                    text: AppController.colabTts.colabLanguage
                    placeholderText: qsTr("en")
                    onEditingFinished: AppController.colabTts.colabLanguage = text.trim()
                }
                PrimaryButton {
                    Layout.fillWidth: true
                    enabled: !root.locked && AppController.colabTts.colabModel !== ""
                             && !AppController.colabTtsSession.checking
                             && !(root.remoteFirstMode && AppController.colabTts.colabActive && root.selectedRemoteProvider === "colab")
                    text: AppController.colabTtsSession.checking
                          ? qsTr("Verifying CUDA and exact model...")
                          : (root.remoteFirstMode
                          ? (AppController.colabTts.colabActive
                             ? (root.selectedRemoteProvider === "colab" ? qsTr("Direct Colab GPU TTS selected") : qsTr("Select direct Colab GPU TTS"))
                             : (AppController.colabTts.colabConnected ? qsTr("Use direct Colab GPU TTS") : qsTr("Connect direct Colab GPU TTS")))
                          : (AppController.colabTts.colabActive && root.selectedRemoteProvider === "colab"
                             ? qsTr("Use local TTS")
                             : (AppController.colabTts.colabActive ? qsTr("Select Colab GPU TTS") : (AppController.colabTts.colabConnected ? qsTr("Use Colab GPU TTS") : qsTr("Connect Colab GPU TTS")))))
                    iconName: root.remoteFirstMode || !(AppController.colabTts.colabActive && root.selectedRemoteProvider === "colab") ? "cloud" : "close"
                    onClicked: {
                        if (AppController.colabTts.colabActive && !root.remoteFirstMode && root.selectedRemoteProvider === "colab") {
                            AppController.colabTts.useLocal()
                            root.remoteProviderSelected("")
                        } else {
                            if (AppController.colabTts.colabConnected) {
                                AppController.colabTts.useColab()
                            } else if (AppController.colabTts.connectColab(colabUrl.text.trim(), colabToken.text)) {
                                colabToken.text = ""
                            }
                            if (AppController.colabTts.colabActive)
                                root.remoteProviderSelected("colab")
                        }
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
                    description: qsTr("Reduce steady background noise in the generated audio. Recommended: on.")
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
                    description: qsTr("Normalize prompt text before synthesis. Recommended: on.")
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
                    description: qsTr("Use a new seed for each run. Turn off to reproduce a result with a fixed seed.")
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
                    placeholderText: qsTr("Fixed seed (whole number, e.g. 42)")
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
