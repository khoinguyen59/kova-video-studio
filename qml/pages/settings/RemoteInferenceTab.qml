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
        readonly property var safeEntry: entry && typeof entry === "object" ? entry : ({})
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
                    text: safeEntry.displayName || safeEntry.modelId || qsTr("Unnamed model")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSmall
                    font.bold: true
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: {
                        var details = []
                        if (safeEntry.capability) details.push(safeEntry.capability)
                        if (safeEntry.workerCapability && safeEntry.workerCapability !== safeEntry.capability)
                            details.push(qsTr("worker %1").arg(safeEntry.workerCapability))
                        if (safeEntry.revision) details.push(qsTr("rev %1").arg(safeEntry.revision))
                        if (safeEntry.license) details.push(safeEntry.license)
                        if (safeEntry.device) details.push(safeEntry.device)
                        if (safeEntry.requiredVramGb > 0) details.push(qsTr("needs %1 GB VRAM").arg(safeEntry.requiredVramGb))
                        return details.length > 0 ? details.join(" · ") : safeEntry.modelId
                    }
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
            }

            Text {
                text: safeEntry.selectable === false ? qsTr("Unavailable") : qsTr("Available")
                color: safeEntry.selectable === false ? Theme.warning : Theme.success
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
            text: qsTr("Local work starts on this computer's CPU and needs no network configuration. GPU work is connected directly from the studio that needs it: paste that Colab worker's URL and temporary token there. API Gateway is optional and never starts automatically.")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }

        GridLayout {
            Layout.fillWidth: true
            columns: root.wideLayout ? 2 : 1
            columnSpacing: Theme.paddingLarge
            rowSpacing: Theme.paddingLarge

            SectionPanel {
                title: qsTr("Optional API Gateway Models")
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop

                Text {
                    Layout.fillWidth: true
                    text: qsTr("This is optional. Leave these fields empty to use local CPU and direct Colab GPU only. When configured, it uses only the Gateway URL and encrypted API key; no Colab worker is contacted.")
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
                    delegate: CatalogRow {
                        entry: typeof modelData === "undefined" || modelData === null ? ({}) : modelData
                    }
                }
            }

            SectionPanel {
                title: qsTr("Active Colab GPU Connections")
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop

                Text {
                    Layout.fillWidth: true
                    text: qsTr("This is an overview only. Connect each GPU worker from the exact studio that uses it (Speech-to-Text, Text-to-Speech, Voice Clone, Voice Design, Isolation, Alignment, Translation, or Chat). Worker models are never merged with Gateway credentials.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
                Text { visible: false; text: qsTr("Worker URL"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                RemoteField {
                    id: colabUrl
                    visible: false
                    text: AppController.colabSttSession.workerUrl
                    placeholderText: qsTr("https://…trycloudflare.com")
                }
                Text { visible: false; text: qsTr("Session token"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                RemoteField {
                    id: colabToken
                    visible: false
                    echoMode: TextInput.Password
                    placeholderText: AppController.colabSttSession.active
                                     ? qsTr("Connected — enter to replace")
                                     : qsTr("Temporary token from Colab")
                }
                RowLayout {
                    visible: false
                    Layout.fillWidth: true
                    spacing: Theme.paddingSmall
                    PrimaryButton {
                        Layout.fillWidth: true
                        text: root.remoteModels.colabRefreshing ? qsTr("Refreshing active workers…")
                             : (AppController.colabSttSession.active ? qsTr("Refresh Active Workers") : qsTr("Pair STT & Refresh Workers"))
                        iconName: "cloud"
                        enabled: !root.remoteModels.colabRefreshing
                        onClicked: {
                            if (AppController.colabSttSession.active && colabToken.text.trim() === "")
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
                PrimaryButton {
                    Layout.fillWidth: true
                    text: root.remoteModels.colabRefreshing ? qsTr("Refreshing active connections...") : qsTr("Refresh active connections")
                    iconName: "reload"
                    enabled: !root.remoteModels.colabRefreshing
                    onClicked: root.remoteModels.refreshColab()
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
                    text: qsTr("%1 direct Colab model(s) reported by active workers").arg(root.remoteModels.colabModels.length)
                    color: Theme.success
                    font.pixelSize: Theme.fontSmall
                }
                Repeater {
                    model: root.remoteModels.colabModels
                    delegate: CatalogRow {
                        entry: typeof modelData === "undefined" || modelData === null ? ({}) : modelData
                    }
                }
            }
        }

        SectionPanel {
            title: qsTr("Local CPU Models")
            Layout.fillWidth: true

            Text {
                Layout.fillWidth: true
                text: qsTr("These installed models run on this computer's CPU. Configure a direct Colab worker at a GPU feature when that feature requires GPU acceleration.")
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
                        Text { text: qsTr("Local CPU"); color: Theme.textSecondary; font.pixelSize: 10; font.bold: true }
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
