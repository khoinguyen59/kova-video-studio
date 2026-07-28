import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LAStudio
import "../../components"
import "../../components/base"
import "../../components/shared/settings"

ScrollView {
    id: root

    clip: true
    contentWidth: availableWidth
    ScrollBar.vertical.policy: ScrollBar.AsNeeded

    readonly property var remoteModels: AppController.remoteModels
    readonly property bool wideLayout: availableWidth >= 980

    component RemoteField: TextField {
        Layout.fillWidth: true
        color: Theme.textPrimary
        placeholderTextColor: Theme.textSecondary
        selectByMouse: true
        background: Rectangle {
            radius: Theme.radiusSmall
            color: Qt.rgba(1, 1, 1, 0.035)
            border.color: parent.activeFocus ? Theme.accent : Qt.rgba(1, 1, 1, 0.09)
        }
    }

    component CatalogRow: Rectangle {
        required property var entry
        Layout.fillWidth: true
        implicitHeight: row.implicitHeight + Theme.paddingMedium * 2
        radius: Theme.radiusSmall
        color: Qt.rgba(0, 0, 0, 0.12)
        border.color: Qt.rgba(1, 1, 1, 0.07)

        RowLayout {
            id: row
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: Theme.paddingMedium
            spacing: Theme.paddingSmall

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    Layout.fillWidth: true
                    text: entry.displayName || entry.modelId || qsTr("Unnamed model")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSmall
                    font.bold: true
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: {
                        var details = []
                        if (entry.capability) details.push(entry.capability)
                        if (entry.revision) details.push(qsTr("rev %1").arg(entry.revision))
                        if (entry.license) details.push(entry.license)
                        if (entry.device) details.push(entry.device)
                        if (entry.requiredVramGb > 0) details.push(qsTr("needs %1 GB VRAM").arg(entry.requiredVramGb))
                        return details.length > 0 ? details.join(" · ") : entry.modelId
                    }
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
            }

            Text {
                text: entry.selectable === false ? qsTr("Unavailable") : qsTr("Available")
                color: entry.selectable === false ? Theme.warning : Theme.success
                font.pixelSize: 10
                font.bold: true
            }
        }
    }

    ColumnLayout {
        width: Math.min(1120, Math.max(0, root.availableWidth - Theme.paddingMedium * 2))
        anchors.left: parent.left
        anchors.leftMargin: Theme.paddingMedium
        spacing: Theme.paddingLarge

        Item { Layout.preferredHeight: 2 }

        Text {
            Layout.fillWidth: true
            text: qsTr("Gateway and Colab are independent inference sources. Refreshing, pairing, or a failure in one never changes the other source's credentials, session, or model list.")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }

        SectionPanel {
            title: qsTr("Execution policy")
            Layout.fillWidth: true

            ToggleRow {
                Layout.fillWidth: true
                text: qsTr("Remote-first mode")
                checked: AppController.settings.remoteFirstMode
                onToggled: AppController.settings.remoteFirstMode = checked
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Enabled by default: local model and runtime installers are hidden. Already-installed local development models remain available, and neither remote source falls back to them.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: root.wideLayout ? 2 : 1
            columnSpacing: Theme.paddingLarge
            rowSpacing: Theme.paddingLarge

            SectionPanel {
                title: qsTr("API Gateway Models")
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Uses only the Gateway URL and encrypted Gateway API key. The model list is fetched directly from /v1/models; no Colab worker is contacted.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
                Text { text: qsTr("Gateway URL"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                RemoteField {
                    text: AppController.settings.gatewayUrl
                    placeholderText: qsTr("https://gateway.example/v1")
                    onEditingFinished: AppController.settings.gatewayUrl = text.trim()
                }
                Text { text: qsTr("API key"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                RemoteField {
                    id: gatewayKey
                    echoMode: TextInput.Password
                    placeholderText: AppController.settings.gatewayApiKeyConfigured
                                     ? qsTr("Saved securely — enter to replace")
                                     : qsTr("Stored encrypted on this device")
                    onEditingFinished: {
                        if (text.trim() !== "") {
                            AppController.settings.setGatewayApiKey(text)
                            text = ""
                        }
                    }
                }
                PrimaryButton {
                    Layout.fillWidth: true
                    text: root.remoteModels.gatewayRefreshing ? qsTr("Refreshing models…") : qsTr("Test & Refresh Gateway Models")
                    iconName: "reload"
                    enabled: !root.remoteModels.gatewayRefreshing
                    onClicked: root.remoteModels.refreshGateway()
                }
                Text {
                    visible: root.remoteModels.gatewayError !== ""
                    Layout.fillWidth: true
                    text: root.remoteModels.gatewayError
                    color: Theme.danger
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
                Text {
                    visible: root.remoteModels.gatewayAvailable
                    Layout.fillWidth: true
                    text: qsTr("%1 Gateway model(s) available").arg(root.remoteModels.gatewayModels.length)
                    color: Theme.success
                    font.pixelSize: Theme.fontSmall
                }
                Repeater {
                    model: root.remoteModels.gatewayModels
                    delegate: CatalogRow { entry: modelData }
                }
            }

            SectionPanel {
                title: qsTr("Active Colab GPU Models")
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Pairs a temporary worker directly with its own URL and token. The worker reports /v1/capabilities; its models are never merged with Gateway models.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
                Text { text: qsTr("Worker URL"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                RemoteField {
                    id: colabUrl
                    text: AppController.colabSession.workerUrl
                    placeholderText: qsTr("https://…trycloudflare.com")
                }
                Text { text: qsTr("Session token"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                RemoteField {
                    id: colabToken
                    echoMode: TextInput.Password
                    placeholderText: AppController.colabSession.active
                                     ? qsTr("Connected — enter to replace")
                                     : qsTr("Temporary token from Colab")
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.paddingSmall
                    PrimaryButton {
                        Layout.fillWidth: true
                        text: root.remoteModels.colabRefreshing ? qsTr("Refreshing worker…")
                             : (AppController.colabSession.active ? qsTr("Refresh Worker") : qsTr("Pair & Refresh Worker"))
                        iconName: "cloud"
                        enabled: !root.remoteModels.colabRefreshing
                        onClicked: {
                            if (AppController.colabSession.active && colabToken.text.trim() === "")
                                root.remoteModels.refreshColab()
                            else if (root.remoteModels.pairColab(colabUrl.text.trim(), colabToken.text))
                                colabToken.text = ""
                        }
                    }
                    PrimaryButton {
                        text: qsTr("Open Notebook")
                        iconName: "external-link"
                        quiet: true
                        onClicked: Qt.openUrlExternally("https://colab.research.google.com/")
                    }
                }
                Text {
                    visible: root.remoteModels.colabError !== ""
                    Layout.fillWidth: true
                    text: root.remoteModels.colabError
                    color: Theme.danger
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
                Text {
                    visible: root.remoteModels.colabAvailable
                    Layout.fillWidth: true
                    text: qsTr("%1 direct Colab model(s) reported by this session").arg(root.remoteModels.colabModels.length)
                    color: Theme.success
                    font.pixelSize: Theme.fontSmall
                }
                Repeater {
                    model: root.remoteModels.colabModels
                    delegate: CatalogRow { entry: modelData }
                }
            }
        }

        SectionPanel {
            title: qsTr("Local Dev Models")
            Layout.fillWidth: true

            Text {
                Layout.fillWidth: true
                text: qsTr("These are already installed local development models. Remote routes do not download them automatically and never fall back to them.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
            Text {
                visible: AppController.models.count === 0
                text: qsTr("No local models installed.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
            }
            Repeater {
                model: AppController.models
                delegate: Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: localRow.implicitHeight + Theme.paddingMedium * 2
                    radius: Theme.radiusSmall
                    color: Qt.rgba(0, 0, 0, 0.12)
                    border.color: Qt.rgba(1, 1, 1, 0.07)
                    RowLayout {
                        id: localRow
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: Theme.paddingMedium
                        Text { Layout.fillWidth: true; text: model.id; color: Theme.textPrimary; font.pixelSize: Theme.fontSmall; elide: Text.ElideRight }
                        Text { text: qsTr("Local Dev"); color: Theme.textSecondary; font.pixelSize: 10; font.bold: true }
                    }
                }
            }
        }

        Item { Layout.preferredHeight: Theme.paddingMedium }
    }

    component SectionPanel: Rectangle {
        property string title: ""
        default property alias content: panelContent.data
        Layout.fillWidth: true
        implicitHeight: panelContent.implicitHeight + Theme.paddingLarge * 2
        radius: Theme.radiusMedium
        color: Theme.surface
        border.color: Qt.rgba(1, 1, 1, 0.08)
        border.width: 1

        ColumnLayout {
            id: panelContent
            anchors.fill: parent
            anchors.margins: Theme.paddingLarge
            spacing: Theme.paddingSmall
            Text { text: parent.parent.title; color: Theme.textPrimary; font.pixelSize: Theme.fontMedium; font.bold: true }
        }
    }
}
