import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15

ApplicationWindow {
    id: window
    width: 1680
    height: 980
    minimumWidth: 1180
    minimumHeight: 700
    visible: true
    title: "LA Studio — Dubbing layout preview"
    color: uiPalette.background

    QtObject {
        id: uiPalette
        readonly property color background: "#1e1e2e"
        readonly property color surface: "#2a2a3e"
        readonly property color surfaceAlt: "#35354a"
        readonly property color border: "#47475f"
        readonly property color accent: "#7c4dff"
        readonly property color text: "#f3f1ff"
        readonly property color muted: "#c7c2dc"
        readonly property color success: "#66bb6a"
    }

    property string activeTask: "Normalize"
    property bool showInspector: false
    property bool showProjectSetup: false
    property string previewRatio: "16:9"
    readonly property real canvasRatio: previewRatio === "9:16" ? 9 / 16
                                      : (previewRatio === "1:1" ? 1 : 16 / 9)

    component OutlineButton: Button {
        id: control
        property bool selected: false
        implicitHeight: 34
        padding: 10
        contentItem: Text {
            text: control.text
            color: control.selected ? uiPalette.text : uiPalette.muted
            font.pixelSize: 13
            font.bold: control.selected
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
        }
        background: Rectangle {
            radius: 7
            color: control.selected ? Qt.rgba(0.49, 0.30, 1.0, 0.23)
                                    : (control.hovered ? Qt.rgba(1, 1, 1, 0.06) : "transparent")
            border.color: control.selected ? uiPalette.accent : uiPalette.border
            border.width: 1
        }
    }

    component TaskButton: Button {
        id: control
        required property string taskName
        implicitWidth: Math.max(88, label.implicitWidth + 28)
        implicitHeight: 44
        padding: 9
        onClicked: {
            window.activeTask = taskName
            window.showInspector = true
        }
        contentItem: Text {
            id: label
            text: control.taskName
            color: window.activeTask === control.taskName ? uiPalette.text : uiPalette.muted
            font.pixelSize: 13
            font.bold: window.activeTask === control.taskName
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 7
            color: window.activeTask === control.taskName ? Qt.rgba(0.49, 0.30, 1.0, 0.18) : "transparent"
            border.color: window.activeTask === control.taskName ? Qt.rgba(0.49, 0.30, 1.0, 0.45) : "transparent"
            border.width: 1
        }
    }

    component PanelTitle: Text {
        font.pixelSize: 12
        font.bold: true
        font.letterSpacing: 1.0
        color: uiPalette.muted
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 66
            color: Qt.rgba(0, 0, 0, 0.13)
            border.color: Qt.rgba(1, 1, 1, 0.08)
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 10
                Text { text: "LA"; color: uiPalette.accent; font.pixelSize: 20; font.bold: true }
                Text { text: "Dubbing Studio"; color: uiPalette.text; font.pixelSize: 20; font.bold: true }
                Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; Layout.topMargin: 16; Layout.bottomMargin: 16; color: uiPalette.border }

                Flickable {
                    id: taskFlickable
                    Layout.fillWidth: true
                    Layout.maximumWidth: Math.max(420, window.width * 0.54)
                    Layout.fillHeight: true
                    contentWidth: taskRow.implicitWidth
                    contentHeight: height
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    Row {
                        id: taskRow
                        height: parent.height
                        spacing: 4
                        TaskButton { taskName: "Import" }
                        TaskButton { taskName: "Normalize" }
                        TaskButton { taskName: "Isolator" }
                        TaskButton { taskName: "Transcribe/STT" }
                        TaskButton { taskName: "Translate" }
                        TaskButton { taskName: "Subtitle" }
                        TaskButton { taskName: "TTS" }
                        TaskButton { taskName: "Alignment" }
                        TaskButton { taskName: "Export" }
                    }
                    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                }

                Item { Layout.fillWidth: true }
                OutlineButton { text: "Project setup"; onClicked: window.showProjectSetup = !window.showProjectSetup }
                OutlineButton { text: "Colab setup" }
                Button {
                    id: generateButton
                    text: "Generate final dubbing"
                    implicitHeight: 38
                    padding: 13
                    contentItem: Text { text: generateButton.text; color: "white"; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { radius: 8; color: uiPalette.accent }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: window.showProjectSetup ? 66 : 0
            visible: height > 0
            clip: true
            color: uiPalette.surfaceAlt
            Behavior on Layout.preferredHeight { NumberAnimation { duration: 150 } }
            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 14
                Text { text: "Project setup"; color: uiPalette.text; font.bold: true }
                OutlineButton { text: "Source: English" }
                OutlineButton { text: "Target: Vietnamese" }
                OutlineButton { text: "Speaker labels" }
                OutlineButton { text: "Dubbing quality" }
                Item { Layout.fillWidth: true }
                Text { text: "Saved per project — no permanent bottom panel"; color: uiPalette.muted; font.pixelSize: 12 }
            }
        }

        SplitView {
            id: verticalWorkspace
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Vertical
            handle: Rectangle {
                implicitHeight: 14
                color: "transparent"
                Rectangle {
                    width: 68
                    height: 4
                    radius: 2
                    anchors.centerIn: parent
                    color: verticalHandleHover.hovered ? uiPalette.accent : uiPalette.border
                }
                HoverHandler { id: verticalHandleHover }
                property bool containsMouse: verticalHandleHover.hovered
                ToolTip.visible: containsMouse
                ToolTip.text: "Drag to resize timeline"
            }

            SplitView {
                id: horizontalWorkspace
                SplitView.fillWidth: true
                SplitView.fillHeight: true
                SplitView.minimumHeight: 330
                orientation: Qt.Horizontal
                handle: Rectangle {
                    implicitWidth: 12
                    color: "transparent"
                    Rectangle {
                        height: 58
                        width: 4
                        radius: 2
                        anchors.centerIn: parent
                        color: horizontalHandleHover.hovered ? uiPalette.accent : uiPalette.border
                    }
                    HoverHandler { id: horizontalHandleHover }
                    property bool containsMouse: horizontalHandleHover.hovered
                }

                Rectangle {
                    id: taskShelf
                    SplitView.minimumWidth: 248
                    SplitView.preferredWidth: 320
                    SplitView.maximumWidth: 440
                    color: uiPalette.surface
                    radius: 10
                    border.color: uiPalette.border
                    border.width: 1
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 13
                        PanelTitle { text: "TASK CONTROLS" }
                        Text { text: window.activeTask; color: uiPalette.text; font.pixelSize: 20; font.bold: true }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: "Controls appear here only for the selected flow task. Setup does not squeeze the video canvas."
                            color: uiPalette.muted
                            font.pixelSize: 13
                        }
                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: uiPalette.border }
                        Repeater {
                            model: window.activeTask === "Transcribe/STT" ? ["Route: Colab GPU", "Model: Whisper.cpp", "OCR scan area", "Run transcription"]
                                  : window.activeTask === "Translate" ? ["Route: API Gateway", "Model: M2M100", "Target language", "Run translation"]
                                  : window.activeTask === "Isolator" ? ["Route: Colab GPU", "Model: Spleeter 2 stems", "Run isolation"]
                                  : ["Task status", "Route and model", "Configure", "Run selected task"]
                            delegate: OutlineButton { required property string modelData; Layout.fillWidth: true; text: modelData; horizontalPadding: 12 }
                        }
                        Item { Layout.fillHeight: true }
                        Text { text: "History is a separate collapsible drawer."; color: uiPalette.muted; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    }
                }

                Rectangle {
                    id: canvasPanel
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 430
                    color: uiPalette.surface
                    radius: 10
                    border.color: uiPalette.border
                    border.width: 1
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 10
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "VIDEO CANVAS"; color: uiPalette.muted; font.pixelSize: 12; font.bold: true; font.letterSpacing: 1.0 }
                            Item { Layout.fillWidth: true }
                            OutlineButton { text: "16:9"; selected: window.previewRatio === "16:9"; onClicked: window.previewRatio = "16:9" }
                            OutlineButton { text: "9:16"; selected: window.previewRatio === "9:16"; onClicked: window.previewRatio = "9:16" }
                            OutlineButton { text: "1:1"; selected: window.previewRatio === "1:1"; onClicked: window.previewRatio = "1:1" }
                            OutlineButton { text: "Fit source"; selected: window.previewRatio === "source"; onClicked: window.previewRatio = "source" }
                        }
                        Rectangle {
                            id: canvas
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: "#101018"
                            radius: 8
                            clip: true
                            Rectangle {
                                id: mediaFrame
                                anchors.centerIn: parent
                                width: window.previewRatio === "source" ? Math.min(parent.width - 28, parent.height * 16 / 9)
                                                                        : Math.min(parent.width - 28, (parent.height - 28) * window.canvasRatio)
                                height: window.previewRatio === "source" ? Math.min(parent.height - 28, parent.width * 9 / 16)
                                                                         : width / window.canvasRatio
                                color: "#171725"
                                border.color: Qt.rgba(uiPalette.accent.r, uiPalette.accent.g, uiPalette.accent.b, 0.55)
                                border.width: 1
                                Text { anchors.centerIn: parent; text: "Video preview\nOCR canvas remains inside this frame"; horizontalAlignment: Text.AlignHCenter; color: uiPalette.muted; font.pixelSize: 14; lineHeight: 1.45 }
                                Rectangle {
                                    visible: window.activeTask === "Transcribe/STT"
                                    x: width * 0.10; y: height * 0.72; width: parent.width * 0.80; height: parent.height * 0.18
                                    color: Qt.rgba(0.49, 0.30, 1.0, 0.10)
                                    border.color: uiPalette.accent
                                    border.width: 2
                                    Text { anchors.centerIn: parent; text: "Drag OCR scan area"; color: uiPalette.text; font.pixelSize: 12 }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    id: inspector
                    visible: window.showInspector
                    SplitView.minimumWidth: visible ? 280 : 0
                    SplitView.preferredWidth: visible ? 360 : 0
                    SplitView.maximumWidth: visible ? 480 : 0
                    color: uiPalette.surface
                    radius: 10
                    border.color: uiPalette.border
                    border.width: 1
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 12
                        RowLayout {
                            Layout.fillWidth: true
                            PanelTitle { text: "DETAILS"; Layout.fillWidth: true }
                            OutlineButton { text: "Hide"; onClicked: window.showInspector = false }
                        }
                        Text { text: window.activeTask; color: uiPalette.text; font.pixelSize: 20; font.bold: true }
                        Text { text: "Readiness, worker configuration, files and output for the selected task appear here."; color: uiPalette.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: uiPalette.border }
                        Repeater {
                            model: ["Route", "Selected model", "Worker readiness", "Expected output"]
                            delegate: ColumnLayout {
                                id: inspectorFact
                                required property string modelData
                                Layout.fillWidth: true
                                Text { text: inspectorFact.modelData; color: uiPalette.muted; font.pixelSize: 12 }
                                Text { text: inspectorFact.modelData === "Worker readiness" ? "Not checked" : "Configured per project"; color: uiPalette.text; font.pixelSize: 14 }
                            }
                        }
                        Item { Layout.fillHeight: true }
                    }
                }
            }

            Rectangle {
                id: timeline
                SplitView.fillWidth: true
                SplitView.minimumHeight: 160
                SplitView.preferredHeight: 270
                SplitView.maximumHeight: 480
                color: "#252538"
                border.color: uiPalette.border
                border.width: 1
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12
                    RowLayout {
                        Layout.fillWidth: true
                        PanelTitle { text: "TIMELINE"; Layout.fillWidth: true }
                        Text { text: "0 segments"; color: uiPalette.muted; font.pixelSize: 12 }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 7
                        color: "#1a1a28"
                        Repeater {
                            model: 22
                            delegate: Rectangle {
                                required property int index
                                width: 2; height: 44 + (index % 5) * 12
                                x: 32 + index * ((parent.width - 80) / 22)
                                anchors.verticalCenter: parent.verticalCenter
                                color: Qt.rgba(uiPalette.accent.r, uiPalette.accent.g, uiPalette.accent.b, 0.65)
                            }
                        }
                        Text { anchors.centerIn: parent; text: "Full-width transcript, audio, subtitle and voice timeline"; color: uiPalette.muted; font.pixelSize: 13 }
                    }
                }
            }
        }
    }
}
