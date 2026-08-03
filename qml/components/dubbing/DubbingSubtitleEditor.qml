pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "../base"
import LAStudio

Dialog {
    id: root

    required property var dubbing
    property var configuration: dubbing.subtitleConfiguration
    property var style: configuration.style || ({})
    property string untimedStrategy: "existing-segment"

    width: Math.min(760, parent ? parent.width - Theme.paddingXL * 2 : 760)
    height: Math.min(650, parent ? parent.height - Theme.paddingXL * 2 : 650)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    parent: Overlay.overlay
    modal: true
    title: qsTr("Dubbing subtitles")
    standardButtons: Dialog.Close

    function number(value, fallback) {
        return value === undefined || value === null ? fallback : Number(value)
    }
    function updateStyle() {
        var next = {
            fontFamily: fontFamilyField.text.trim(), fontFile: fontFileField.text.trim(),
            fontSize: fontSizeBox.value, fontWeight: fontWeightBox.value,
            textColor: textColorField.text.trim(), outlineColor: outlineColorField.text.trim(),
            outlineWidth: outlineBox.value, shadowColor: shadowColorField.text.trim(),
            shadowOffset: shadowBox.value, backgroundColor: backgroundColorField.text.trim(),
            backgroundOpacity: backgroundOpacityBox.value / 100.0,
            alignment: alignmentBox.currentValue, maxWidth: maxWidthBox.value / 100.0,
            lineSpacing: lineSpacingBox.value / 100.0, safeMargin: safeMarginBox.value / 100.0,
            positionX: positionXBox.value / 100.0, positionY: positionYBox.value / 100.0
        }
        dubbing.setSubtitleStyle(next)
    }
    function qmlSmokeLayoutCheck() {
        return visible && width > 360 && height > 360 && width <= (parent ? parent.width : width)
            && height <= (parent ? parent.height : height)
            && subtitleImportButton.width > 0 && subtitleImportButton.enabled
            && burnInBox.width > 0 && fontFamilyField.width > 0
            && alignmentBox.width > 0 && subtitleTextSourceBox.width > 0 && safeMarginBox.width > 0
    }

    onOpened: {
        configuration = dubbing.subtitleConfiguration
        style = configuration.style || ({})
    }

    contentItem: ScrollView {
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            spacing: Theme.paddingMedium

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 86
                radius: Theme.radiusSmall
                color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.10)
                border.color: Qt.rgba(Theme.accentLight.r, Theme.accentLight.g, Theme.accentLight.b, 0.32)
                RowLayout {
                    anchors.fill: parent; anchors.margins: Theme.paddingMedium; spacing: Theme.paddingMedium
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 2
                        Text { text: qsTr("IMPORT REVIEWED SUBTITLES"); color: Theme.textPrimary; font.bold: true; font.pixelSize: Theme.fontSmall }
                        Text { Layout.fillWidth: true; text: qsTr("SRT, VTT and ASS/SSA keep supplied timing. TXT/Markdown maps one line to each existing reviewed segment; timestamps are never invented."); color: Theme.textSecondary; wrapMode: Text.WordWrap; font.pixelSize: 11 }
                    }
                    PrimaryButton { id: subtitleImportButton; text: qsTr("Import file"); iconName: "folder"; onClicked: subtitleImportDialog.open() }
                }
            }

            RowLayout {
                Layout.fillWidth: true; spacing: Theme.paddingMedium
                Label { text: qsTr("Untimed import"); color: Theme.textSecondary }
                ComboBox {
                    id: untimedStrategyBox
                    Layout.fillWidth: true
                    model: [{ value: "existing-segment", text: qsTr("Line per existing reviewed segment") },
                            { value: "alignment", text: qsTr("Run forced alignment first") }]
                    textRole: "text"; valueRole: "value"
                    onActivated: root.untimedStrategy = currentValue
                }
            }
            Text { Layout.fillWidth: true; visible: untimedStrategyBox.currentValue === "alignment"; color: Theme.warning; wrapMode: Text.WordWrap; font.pixelSize: 11; text: qsTr("Import is blocked until forced alignment creates a reviewed timed transcript. This prevents guessed subtitle timing.") }

            RowLayout {
                Layout.fillWidth: true; spacing: Theme.paddingMedium
                Label { text: qsTr("Rendered text"); color: Theme.textSecondary }
                ComboBox {
                    id: subtitleTextSourceBox
                    Layout.fillWidth: true
                    model: [{ value: "target", text: qsTr("Translated / target text") },
                            { value: "source", text: qsTr("Reviewed source text (STT, OCR, or imported)") }]
                    textRole: "text"; valueRole: "value"
                    currentIndex: (root.configuration.textSource || "target") === "source" ? 1 : 0
                    onActivated: root.dubbing.setSubtitleTextSource(currentValue)
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Qt.rgba(1,1,1,0.08) }
            Text { text: qsTr("PREVIEW STYLE"); color: Theme.textPrimary; font.bold: true; font.pixelSize: Theme.fontSmall }

            GridLayout {
                Layout.fillWidth: true; columns: width >= 580 ? 2 : 1; columnSpacing: Theme.paddingMedium; rowSpacing: Theme.paddingSmall
                Field { label: qsTr("Font family"); field: TextField { id: fontFamilyField; text: root.style.fontFamily || "Arial"; onEditingFinished: root.updateStyle() } }
                Field { label: qsTr("Font file (optional)"); field: RowLayout { TextField { id: fontFileField; Layout.fillWidth: true; text: root.style.fontFile || ""; onEditingFinished: root.updateStyle() } Button { text: qsTr("Choose"); onClicked: fontFileDialog.open() } } }
                Field { label: qsTr("Size"); field: SpinBox { id: fontSizeBox; from: 8; to: 180; value: root.number(root.style.fontSize, 42); onValueModified: root.updateStyle() } }
                Field { label: qsTr("Weight"); field: SpinBox { id: fontWeightBox; from: 100; to: 900; stepSize: 100; value: root.number(root.style.fontWeight, 600); onValueModified: root.updateStyle() } }
                Field { label: qsTr("Text color"); field: TextField { id: textColorField; text: root.style.textColor || "#FFFFFFFF"; onEditingFinished: root.updateStyle() } }
                Field { label: qsTr("Outline color / width"); field: RowLayout { TextField { id: outlineColorField; Layout.fillWidth: true; text: root.style.outlineColor || "#D9000000"; onEditingFinished: root.updateStyle() } SpinBox { id: outlineBox; from: 0; to: 16; value: root.number(root.style.outlineWidth, 2); onValueModified: root.updateStyle() } } }
                Field { label: qsTr("Shadow color / offset"); field: RowLayout { TextField { id: shadowColorField; Layout.fillWidth: true; text: root.style.shadowColor || "#99000000"; onEditingFinished: root.updateStyle() } SpinBox { id: shadowBox; from: 0; to: 24; value: root.number(root.style.shadowOffset, 2); onValueModified: root.updateStyle() } } }
                Field { label: qsTr("Background / opacity (0–100)"); field: RowLayout { TextField { id: backgroundColorField; Layout.fillWidth: true; text: root.style.backgroundColor || "#00000000"; onEditingFinished: root.updateStyle() } SpinBox { id: backgroundOpacityBox; from: 0; to: 100; value: Math.round(root.number(root.style.backgroundOpacity, 0) * 100); onValueModified: root.updateStyle() } } }
                Field { label: qsTr("Alignment"); field: ComboBox { id: alignmentBox; model: [{value:"top", text:qsTr("Top")}, {value:"bottom", text:qsTr("Bottom")}, {value:"custom", text:qsTr("Custom normalized X/Y")}]; textRole: "text"; valueRole: "value"; Component.onCompleted: currentIndex = (root.style.alignment || "bottom") === "top" ? 0 : (root.style.alignment || "bottom") === "custom" ? 2 : 1; onActivated: root.updateStyle() } }
                Field { label: qsTr("Max width / line spacing (percent)"); field: RowLayout { SpinBox { id: maxWidthBox; from: 10; to: 100; value: Math.round(root.number(root.style.maxWidth, .82) * 100); onValueModified: root.updateStyle() } SpinBox { id: lineSpacingBox; from: 50; to: 300; value: Math.round(root.number(root.style.lineSpacing, 1) * 100); onValueModified: root.updateStyle() } } }
                Field { label: qsTr("Safe margin (percent)"); field: SpinBox { id: safeMarginBox; from: 0; to: 25; value: Math.round(root.number(root.style.safeMargin, .06) * 100); onValueModified: root.updateStyle() } }
                Field { label: qsTr("Custom position X / Y (percent)"); field: RowLayout { enabled: alignmentBox.currentValue === "custom"; SpinBox { id: positionXBox; from: 0; to: 100; value: Math.round(root.number(root.style.positionX, .5) * 100); onValueModified: root.updateStyle() } SpinBox { id: positionYBox; from: 0; to: 100; value: Math.round(root.number(root.style.positionY, .9) * 100); onValueModified: root.updateStyle() } } }
            }

            CheckBox {
                id: burnInBox
                text: qsTr("Burn the styled subtitles into rendered MP4")
                checked: root.configuration.burnIn === true
                onToggled: dubbing.setSubtitleBurnIn(checked)
            }
            Text { Layout.fillWidth: true; color: Theme.textSecondary; wrapMode: Text.WordWrap; font.pixelSize: 11; text: burnInBox.checked ? qsTr("Burn-in encodes the video so subtitles are always visible. Max width and line spacing apply to both the preview and rendered MP4. The Sidecar export remains a separate UTF-8 SRT/VTT file.") : qsTr("MP4 keeps a selectable subtitle track. Enable burn-in for a permanent styled overlay.") }
        }
    }

    FileDialog {
        id: subtitleImportDialog
        title: qsTr("Import subtitles into Dubbing")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Subtitle files (*.srt *.vtt *.ass *.ssa *.txt *.md *.markdown)"), qsTr("All files (*)")]
        onAccepted: dubbing.importSubtitles(AppController.files.urlToLocalPath(selectedFile.toString()), root.untimedStrategy)
    }
    FileDialog {
        id: fontFileDialog
        title: qsTr("Select optional subtitle font file")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Font files (*.ttf *.otf *.ttc)"), qsTr("All files (*)")]
        onAccepted: { fontFileField.text = AppController.files.urlToLocalPath(selectedFile.toString()); root.updateStyle() }
    }

    component Field: ColumnLayout {
        id: fieldRoot
        required property string label
        required property Item field
        Layout.fillWidth: true
        spacing: 3
        // `data: [field]` reparents the supplied control.  Its visual children can
        // consequently be constructed before their `parent` is stable, so a
        // parent lookup here can intermittently dereference null.  Bind to the
        // named component root instead.
        Text { text: fieldRoot.label; color: Theme.textSecondary; font.pixelSize: 10; font.bold: true }
        data: [field]
    }
}
