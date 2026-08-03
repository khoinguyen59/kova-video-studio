import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../base"
import LAStudio

// The automatic workflow must be reviewed before it can start.  This dialog
// deliberately reads DubbingController.automaticPreflight rather than keeping
// a copy of URL/token/model state in QML, so every change is checked against
// the exact configuration the controller will execute.
Dialog {
    id: root

    required property var dubbing
    property int currentPage: 0
    readonly property var preflight: root.dubbing ? root.dubbing.automaticPreflight : ({})
    readonly property bool ready: root.preflight.ready === true

    signal stepByStepRequested()
    signal nodeModelRequested(string nodeId)
    signal colabSetupRequested()
    signal automaticStartRequested()

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(860, parent ? parent.width - Theme.paddingXL * 2 : 860)
    height: Math.min(650, parent ? parent.height - Theme.paddingXL * 2 : 650)
    modal: true
    padding: 0
    title: ""
    closePolicy: Popup.CloseOnEscape

    function openPreflight() {
        currentPage = 0
        open()
    }

    function requestAutomaticStart() {
        if (!root.dubbing.approveAutomaticPreflight()) return
        root.close()
        root.automaticStartRequested()
    }

    background: Rectangle {
        color: Theme.surface
        radius: Theme.radiusMedium
        border.color: Qt.rgba(1, 1, 1, 0.13)
        border.width: 1
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.paddingLarge
            spacing: Theme.paddingMedium

            LineIcon {
                name: "workflow"
                color: Theme.accentLight
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    text: qsTr("Automatic dubbing preflight")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontLarge
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Review the exact media, routes, models, fixed Colab configurations, and worker checks before any job starts.")
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

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.paddingLarge
            Layout.rightMargin: Theme.paddingLarge
            Layout.topMargin: Theme.paddingMedium
            spacing: Theme.paddingSmall

            Repeater {
                model: [qsTr("Mode"), qsTr("Routes & models"), qsTr("Colab workers"), qsTr("Review")]
                delegate: Rectangle {
                    required property int index
                    required property string modelData
                    Layout.fillWidth: true
                    implicitHeight: 28
                    radius: 14
                    color: index === root.currentPage
                           ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.24)
                           : Qt.rgba(1, 1, 1, 0.04)
                    border.color: index === root.currentPage
                                  ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.55)
                                  : Qt.rgba(1, 1, 1, 0.09)
                    Text {
                        anchors.centerIn: parent
                        text: (index + 1) + ". " + modelData
                        color: index === root.currentPage ? Theme.textPrimary : Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        font.bold: index === root.currentPage
                    }
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.paddingLarge
            currentIndex: root.currentPage

            ColumnLayout {
                spacing: Theme.paddingLarge
                Item { Layout.fillHeight: true }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("How would you like to run this project?")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontXLarge
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Automatic checks every configured stage before it begins. Step-by-step leaves you in control of each individual stage.")
                    color: Theme.textSecondary
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: Theme.paddingMedium
                    PrimaryButton {
                        text: qsTr("Automatic (recommended)")
                        iconName: "play"
                        implicitWidth: 230
                        onClicked: root.currentPage = 1
                    }
                    PrimaryButton {
                        text: qsTr("Review one by one")
                        iconName: "workflow"
                        quiet: true
                        implicitWidth: 210
                        onClicked: {
                            root.close()
                            root.stepByStepRequested()
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }

            ScrollView {
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: Theme.paddingMedium
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Routes and models")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontLarge
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Only configured nodes are used. Configure any node before continuing; changing it will require a fresh review.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                    }
                    Repeater {
                        model: root.preflight.nodes || []
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: nodeRow.implicitHeight + Theme.paddingMedium * 2
                            radius: Theme.radiusSmall
                            color: Qt.rgba(1, 1, 1, 0.025)
                            border.color: Qt.rgba(1, 1, 1, 0.09)
                            RowLayout {
                                id: nodeRow
                                anchors.fill: parent
                                anchors.margins: Theme.paddingMedium
                                spacing: Theme.paddingSmall
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text { text: modelData.title || modelData.id; color: Theme.textPrimary; font.bold: true }
                                    Text {
                                        Layout.fillWidth: true
                                        text: qsTr("Route: %1   •   Model: %2")
                                            .arg(modelData.executionProvider || "local-dev")
                                            .arg(modelData.modelId || qsTr("workflow default"))
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontSmall
                                        elide: Text.ElideRight
                                    }
                                }
                                PrimaryButton {
                                    text: qsTr("Configure")
                                    quiet: true
                                    enabled: !root.dubbing.processing
                                    onClicked: root.nodeModelRequested(modelData.id)
                                }
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                spacing: Theme.paddingMedium
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Direct Colab workers")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontLarge
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Only active nodes routed to Direct Colab appear here. Each must be connected and checked against its exact capability, model, and fixed notebook configuration.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width
                        spacing: Theme.paddingSmall
                        Repeater {
                            model: root.preflight.selectedWorkers || []
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: workerRow.implicitHeight + Theme.paddingMedium * 2
                                radius: Theme.radiusSmall
                                color: modelData.verified ? Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.07)
                                                          : Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.08)
                                border.color: modelData.verified ? Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.38)
                                                                  : Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.45)
                                RowLayout {
                                    id: workerRow
                                    anchors.fill: parent
                                    anchors.margins: Theme.paddingMedium
                                    spacing: Theme.paddingSmall
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text { text: modelData.title || modelData.id; color: Theme.textPrimary; font.bold: true }
                                        Text {
                                            Layout.fillWidth: true
                                            text: qsTr("%1 / %2 / %3")
                                                .arg(modelData.capability || "")
                                                .arg(modelData.modelId || "")
                                                .arg(modelData.variant || qsTr("fixed notebook config"))
                                            color: Theme.textSecondary
                                            font.pixelSize: Theme.fontSmall
                                            elide: Text.ElideRight
                                        }
                                    }
                                    Text {
                                        text: modelData.verified ? qsTr("Verified") : qsTr("Needs check")
                                        color: modelData.verified ? Theme.success : Theme.warning
                                        font.pixelSize: Theme.fontSmall
                                        font.bold: true
                                    }
                                }
                            }
                        }
                        Text {
                            visible: (root.preflight.selectedWorkers || []).length === 0
                            text: qsTr("No active Dubbing node is using Direct Colab. API Gateway and Local CPU routes remain independent.")
                            color: Theme.textSecondary
                            wrapMode: Text.WordWrap
                        }
                    }
                }
                PrimaryButton {
                    text: qsTr("Configure / Check Direct Colab workers")
                    iconName: "cloud"
                    Layout.fillWidth: true
                    onClicked: root.colabSetupRequested()
                }
            }

            ScrollView {
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: Theme.paddingMedium
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: reviewBanner.implicitHeight + Theme.paddingMedium * 2
                        radius: Theme.radiusSmall
                        color: root.ready ? Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.10)
                                           : Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.10)
                        border.color: root.ready ? Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.45)
                                                 : Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.45)
                        ColumnLayout {
                            id: reviewBanner
                            anchors.fill: parent
                            anchors.margins: Theme.paddingMedium
                            Text {
                                text: root.ready ? qsTr("Ready to start") : qsTr("Automatic workflow is blocked")
                                color: root.ready ? Theme.success : Theme.warning
                                font.pixelSize: Theme.fontLarge
                                font.bold: true
                            }
                            Text {
                                Layout.fillWidth: true
                                text: root.ready
                                      ? qsTr("All current requirements passed. Start remains bound to this reviewed configuration only.")
                                      : qsTr("Resolve the items below. The review updates when you change a route, model, media file, or worker connection.")
                                color: Theme.textSecondary
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                    Text {
                        visible: !root.ready
                        text: qsTr("Blocked items")
                        color: Theme.textPrimary
                        font.bold: true
                    }
                    Repeater {
                        model: root.preflight.issues || []
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: issueText.implicitHeight + Theme.paddingSmall * 2
                            radius: Theme.radiusSmall
                            color: Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.08)
                            Text {
                                id: issueText
                                anchors.fill: parent
                                anchors.margins: Theme.paddingSmall
                                text: "• " + (modelData.message || modelData.id)
                                color: Theme.danger
                                wrapMode: Text.WordWrap
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
            spacing: Theme.paddingSmall
            PrimaryButton {
                text: qsTr("Back")
                quiet: true
                visible: root.currentPage > 0
                onClicked: root.currentPage -= 1
            }
            Item { Layout.fillWidth: true }
            PrimaryButton {
                visible: root.currentPage < 3
                text: qsTr("Next")
                iconName: "chevron-right"
                onClicked: root.currentPage += 1
            }
            PrimaryButton {
                visible: root.currentPage === 3
                text: qsTr("Start Automatic Dubbing")
                iconName: "play"
                enabled: root.ready && !root.dubbing.processing
                onClicked: root.requestAutomaticStart()
                AppToolTip {
                    text: root.ready ? qsTr("Start with the reviewed configuration")
                                     : qsTr("Resolve all blocked items before starting")
                    visible: parent.hovered
                }
            }
        }
    }
}
