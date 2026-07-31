pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LAStudio
import "../components/base"

Rectangle {
    id: root

    color: Theme.background

    signal pageRequested(string routeId)

    readonly property bool compact: width < 980
    readonly property int pageMargin: compact ? Theme.paddingLarge : 40

    function displayBytes(bytes) {
        if (bytes < 0) return qsTr("Space could not be determined")
        var units = ["B", "KB", "MB", "GB", "TB"]
        var value = bytes
        var index = 0
        while (value >= 1024 && index < units.length - 1) {
            value /= 1024
            ++index
        }
        return Number(value).toLocaleString(Qt.locale(), 'f', index === 0 ? 0 : 1) + " " + units[index]
    }
    readonly property var studioCards: StudioRouteRegistry.homeFeatureCards

    // Exercised by the offscreen route smoke after layout has settled. This
    // keeps the Home catalogue and its route activation contract testable as
    // the shared registry grows.
    function qmlSmokeHomeCardsCheck() {
        if (studioCardRepeater.count !== 10) return false
        var seenRoutes = {}
        var cards = []
        for (var i = 0; i < studioCardRepeater.count; ++i) {
            var card = studioCardRepeater.itemAt(i)
            if (!card || !card.visible || card.width <= 0 || card.height <= 0
                    || card.x < -1 || card.y < -1
                    || card.x + card.width > studioCardGrid.width + 1)
                return false
            if (seenRoutes[card.targetRoute]) return false
            seenRoutes[card.targetRoute] = true
            cards.push(card)
        }
        var download = cards[8]
        var subtitleOcr = cards[9]
        if (!download || !subtitleOcr
                || download.cardNumber !== 9 || subtitleOcr.cardNumber !== 10
                || download.targetRoute !== "media-download"
                || subtitleOcr.targetRoute !== "subtitle-ocr") return false
        for (var left = 0; left < cards.length; ++left) {
            for (var right = left + 1; right < cards.length; ++right) {
                var a = cards[left]
                var b = cards[right]
                if (a.x < b.x + b.width && a.x + a.width > b.x
                        && a.y < b.y + b.height && a.y + a.height > b.y)
                    return false
            }
        }
        if (homeScroll.contentHeight < studioCardGrid.y + studioCardGrid.height - 1)
            return false
        // At wide and medium sizes the shared registry should lay the new
        // ninth and tenth cards out as the next readable pair. Narrow views
        // are intentionally allowed to flow into one column.
        if (download.y === subtitleOcr.y && download.x >= subtitleOcr.x)
            return false
        return true
    }

    Component.onCompleted: {
        if (!AppController.settings.onboardingComplete) firstRunDialog.open()
    }

    Dialog {
        id: firstRunDialog
        parent: Overlay.overlay
        modal: true
        title: qsTr("Welcome to LA Studio")
        width: Math.min(500, parent ? parent.width - 32 : 500)
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0
        standardButtons: Dialog.NoButton

        contentItem: ColumnLayout {
            spacing: Theme.paddingMedium
            Text {
                Layout.fillWidth: true
                text: qsTr("LA Studio starts with local CPU processing and no required API configuration. For GPU features, connect the direct Colab worker from that feature's settings using its temporary URL and token.")
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Models directory: %1").arg(AppController.settings.modelsPath)
                color: Theme.textPrimary
                wrapMode: Text.WrapAnywhere
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Available space: %1").arg(root.displayBytes(AppController.settings.modelsPathAvailableBytes()))
                color: AppController.settings.modelsPathAvailableBytes() >= 0 ? Theme.success : Theme.warning
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: AppController.settings.externalMediaToolsAvailable()
                      ? qsTr("Media tools: FFmpeg and FFprobe are ready.")
                      : qsTr("Media tools: FFmpeg and FFprobe will be required before importing media.")
                color: AppController.settings.externalMediaToolsAvailable() ? Theme.success : Theme.warning
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.paddingSmall
                PrimaryButton {
                    text: qsTr("Choose models directory")
                    quiet: true
                    Layout.fillWidth: true
                    onClicked: {
                        AppController.settings.onboardingComplete = true
                        firstRunDialog.close()
                        root.pageRequested("settings")
                    }
                }
                PrimaryButton {
                    text: qsTr("Find a model")
                    Layout.fillWidth: true
                    onClicked: {
                        AppController.settings.onboardingComplete = true
                        firstRunDialog.close()
                        root.pageRequested("models")
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.background }
            GradientStop { position: 1.0; color: Qt.darker(Theme.background, 1.16) }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: -160
        anchors.topMargin: -170
        width: 430
        height: 430
        radius: 215
        color: Qt.rgba(0.49, 0.30, 1.0, 0.12)
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: -120
        anchors.topMargin: 80
        width: 360
        height: 360
        radius: 180
        color: Qt.rgba(0.20, 0.55, 1.0, 0.07)
    }

    ScrollView {
        id: homeScroll
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            id: homeContent
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: root.compact ? Theme.paddingLarge : 34
            anchors.bottomMargin: Theme.paddingXL
            width: parent.width - root.pageMargin * 2
            spacing: Theme.paddingLarge

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.paddingMedium

                Rectangle {
                    Layout.preferredWidth: 42
                    Layout.preferredHeight: 42
                    radius: 10
                    color: Qt.rgba(0.49, 0.30, 1.0, 0.18)
                    border.color: Qt.rgba(0.64, 0.49, 1.0, 0.34)
                    border.width: 1

                    Image {
                        anchors.centerIn: parent
                        source: "qrc:/LAStudio/icons/app_icon_32.png"
                        width: 25
                        height: 25
                        fillMode: Image.PreserveAspectFit
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        text: qsTr("LA Studio")
                        color: Theme.textPrimary
                        font.pixelSize: 18
                        font.bold: true
                    }

                    Text {
                        text: qsTr("Local AI audio workspace")
                        color: Theme.textSecondary
                        font.pixelSize: 12
                    }
                }

                StatusPill {
                    label: qsTr("Offline ready")
                    iconName: "check"
                    accent: Theme.success
                    visible: !root.compact
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: root.compact ? 310 : 236
                radius: 12
                color: Qt.rgba(1, 1, 1, 0.035)
                border.color: Qt.rgba(1, 1, 1, 0.075)
                border.width: 1
                clip: true

                Rectangle {
                    width: parent.width * 0.58
                    height: parent.height * 1.8
                    radius: width / 2
                    anchors.right: parent.right
                    anchors.rightMargin: -width * 0.28
                    anchors.verticalCenter: parent.verticalCenter
                    color: Qt.rgba(0.49, 0.30, 1.0, 0.10)
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: root.compact ? Theme.paddingLarge : Theme.paddingXL
                    spacing: Theme.paddingXL

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: Theme.paddingMedium

                        Text {
                            text: qsTr("HOME")
                            color: Theme.accentLight
                            font.pixelSize: 11
                            font.bold: true
                            font.letterSpacing: 1.4
                        }

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Create, transcribe, and shape speech with independent remote GPU routes.")
                            color: Theme.textPrimary
                            font.pixelSize: root.compact ? 30 : 40
                            font.bold: true
                            lineHeight: 1.02
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            Layout.fillWidth: true
                            Layout.maximumWidth: 760
                            text: qsTr("A focused desktop workspace for speech, media download, subtitle OCR, and localization. Pick a feature card below to start.")
                            color: Theme.textSecondary
                            font.pixelSize: 14
                            lineHeight: 1.35
                            wrapMode: Text.WordWrap
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.paddingSmall

                            InfoChip { label: qsTr("Offline processing"); iconName: "cpu"; accent: Theme.accentLight }
                            InfoChip { label: qsTr("Private by design"); iconName: "folder"; accent: Theme.success }
                            InfoChip { label: qsTr("Ten feature cards"); iconName: "spark"; accent: Theme.warning }
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 330
                        Layout.fillHeight: true
                        radius: 10
                        color: Qt.rgba(0, 0, 0, 0.16)
                        border.color: Qt.rgba(1, 1, 1, 0.07)
                        border.width: 1
                        visible: !root.compact

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.paddingLarge
                            spacing: Theme.paddingMedium

                            Text {
                                text: qsTr("Workspace Overview")
                                color: Theme.textPrimary
                                font.pixelSize: 14
                                font.bold: true
                            }

                            OverviewRow { label: qsTr("Feature cards"); value: "10"; iconName: "spark"; accent: Theme.accentLight }
                            OverviewRow { label: qsTr("Audio workflows"); value: qsTr("Focused"); iconName: "waves"; accent: Theme.warning }
                            OverviewRow { label: qsTr("Runtime mode"); value: qsTr("Local"); iconName: "cpu"; accent: Theme.success }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                color: Qt.rgba(1, 1, 1, 0.07)
                            }

                            Text {
                                Layout.fillWidth: true
                            text: qsTr("The home page highlights all ten primary workflows. Configuration stays inside each feature.")
                                color: Theme.textSecondary
                                font.pixelSize: 12
                                lineHeight: 1.25
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.paddingSmall

                Text {
                    text: qsTr("Studios")
                    color: Theme.textPrimary
                    font.pixelSize: 16
                    font.bold: true
                    Layout.fillWidth: true
                }

                Text {
                    text: qsTr("Choose a workflow")
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    visible: !root.compact
                }
            }

            GridLayout {
                id: studioCardGrid
                Layout.fillWidth: true
                columns: root.width > 1420 ? 4 : (root.width > 860 ? 2 : 1)
                rowSpacing: Theme.paddingMedium
                columnSpacing: Theme.paddingMedium

                Repeater {
                    id: studioCardRepeater
                    model: root.studioCards

                    StudioCard {
                        required property int index
                        required property var modelData

                        Layout.fillWidth: true
                        Layout.preferredHeight: 142
                        objectName: "homeFeatureCard-" + targetRoute
                        cardNumber: index + 1
                        title: modelData.label
                        group: modelData.group
                        description: modelData.description
                        iconName: modelData.iconName
                        targetRoute: modelData.id
                        accent: modelData.accent
                    }
                }
            }
        }
    }

    component StudioCard: Rectangle {
        id: card

        property int cardNumber: 1
        property string title: ""
        property string group: ""
        property string description: ""
        property string iconName: ""
        property string targetRoute: ""
        property color accent: Theme.accentLight

        radius: 10
        color: hoverHandler.hovered ? Qt.rgba(1, 1, 1, 0.055) : Qt.rgba(1, 1, 1, 0.032)
        border.color: hoverHandler.hovered ? Qt.rgba(accent.r, accent.g, accent.b, 0.42) : Qt.rgba(1, 1, 1, 0.07)
        border.width: 1
        clip: true

        Behavior on color { ColorAnimation { duration: 130 } }
        Behavior on border.color { ColorAnimation { duration: 130 } }

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 3
            color: card.accent
            opacity: hoverHandler.hovered ? 1.0 : 0.72
        }

        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.paddingLarge
            spacing: Theme.paddingMedium

            Rectangle {
                Layout.preferredWidth: 44
                Layout.preferredHeight: 44
                radius: 10
                color: Qt.rgba(card.accent.r, card.accent.g, card.accent.b, 0.12)
                border.color: Qt.rgba(card.accent.r, card.accent.g, card.accent.b, 0.28)
                border.width: 1
                Layout.alignment: Qt.AlignTop

                LineIcon {
                    anchors.centerIn: parent
                    name: card.iconName
                    color: card.accent
                    width: 21
                    height: 21
                    strokeWidth: 1.8
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.paddingSmall

                    Text {
                        text: card.group
                        color: card.accent
                        font.pixelSize: 10
                        font.bold: true
                        font.letterSpacing: 0.9
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }

                    Text {
                        text: ("0" + card.cardNumber).slice(-2)
                        color: Theme.textSecondary
                        font.pixelSize: 11
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: card.title
                    color: Theme.textPrimary
                    font.pixelSize: 17
                    font.bold: true
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    text: card.description
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    lineHeight: 1.25
                    wrapMode: Text.WordWrap
                    verticalAlignment: Text.AlignTop
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 5

                    Text {
                        text: qsTr("Open")
                        color: hoverHandler.hovered ? card.accent : Theme.textSecondary
                        font.pixelSize: 12
                        font.bold: true
                    }

                    LineIcon {
                        name: "chevron-right"
                        color: hoverHandler.hovered ? card.accent : Theme.textSecondary
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 14
                    }
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: false
            cursorShape: Qt.PointingHandCursor
            onClicked: root.pageRequested(card.targetRoute)
        }

        HoverHandler {
            id: hoverHandler
        }
    }

    component InfoChip: Rectangle {
        id: chip

        property string label: ""
        property string iconName: "check"
        property color accent: Theme.accentLight

        width: chipRow.implicitWidth + 22
        height: 30
        radius: 8
        color: Qt.rgba(accent.r, accent.g, accent.b, 0.10)
        border.color: Qt.rgba(accent.r, accent.g, accent.b, 0.26)
        border.width: 1

        RowLayout {
            id: chipRow
            anchors.centerIn: parent
            spacing: 6

            LineIcon {
                name: chip.iconName
                color: chip.accent
                Layout.preferredWidth: 14
                Layout.preferredHeight: 14
            }

            Text {
                text: chip.label
                color: Theme.textPrimary
                font.pixelSize: 12
                font.bold: true
            }
        }
    }

    component StatusPill: Rectangle {
        id: statusPill

        property string label: ""
        property string iconName: "check"
        property color accent: Theme.success

        Layout.preferredWidth: statusRow.implicitWidth + 22
        Layout.preferredHeight: 32
        radius: 8
        color: Qt.rgba(accent.r, accent.g, accent.b, 0.10)
        border.color: Qt.rgba(accent.r, accent.g, accent.b, 0.26)
        border.width: 1

        RowLayout {
            id: statusRow
            anchors.centerIn: parent
            spacing: 6

            LineIcon {
                name: statusPill.iconName
                color: statusPill.accent
                Layout.preferredWidth: 14
                Layout.preferredHeight: 14
            }

            Text {
                text: statusPill.label
                color: statusPill.accent
                font.pixelSize: 12
                font.bold: true
            }
        }
    }

    component OverviewRow: RowLayout {
        id: overviewRow

        property string label: ""
        property string value: ""
        property string iconName: "check"
        property color accent: Theme.accentLight

        Layout.fillWidth: true
        spacing: Theme.paddingSmall

        Rectangle {
            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
            radius: 8
            color: Qt.rgba(overviewRow.accent.r, overviewRow.accent.g, overviewRow.accent.b, 0.10)

            LineIcon {
                anchors.centerIn: parent
                name: overviewRow.iconName
                color: overviewRow.accent
                width: 15
                height: 15
            }
        }

        Text {
            Layout.fillWidth: true
            text: overviewRow.label
            color: Theme.textSecondary
            font.pixelSize: 12
        }

        Text {
            text: overviewRow.value
            color: Theme.textPrimary
            font.pixelSize: 12
            font.bold: true
        }
    }
}
