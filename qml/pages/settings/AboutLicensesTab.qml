import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LAStudio
import "../../components/base"

ScrollView {
    id: root

    clip: true
    contentWidth: availableWidth
    ScrollBar.vertical.policy: ScrollBar.AsNeeded

    readonly property int contentMaxWidth: 960

    function openInstalledLicenses() {
        if (AppController.licensesDir !== "") {
            Qt.openUrlExternally("file:///" + AppController.licensesDir)
        }
    }

    ColumnLayout {
        width: Math.min(root.contentMaxWidth, Math.max(0, root.availableWidth - Theme.paddingMedium * 2))
        anchors.left: parent.left
        anchors.leftMargin: Theme.paddingMedium
        spacing: Theme.paddingLarge

        Item { Layout.preferredHeight: 2 }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: applicationLayout.implicitHeight + Theme.paddingLarge * 2
            radius: Theme.radiusMedium
            color: Qt.rgba(0.49, 0.30, 1.0, 0.08)
            border.color: Qt.rgba(0.49, 0.30, 1.0, 0.25)
            border.width: 1

            ColumnLayout {
                id: applicationLayout
                anchors.fill: parent
                anchors.margins: Theme.paddingLarge
                spacing: Theme.paddingMedium

                Text {
                    text: qsTr("LA Studio %1").arg(Qt.application.version)
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontLarge
                    font.bold: true
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("LA Studio is distributed under the GNU Affero General Public License v3.0 only (AGPL-3.0-only).")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.paddingSmall

                    PrimaryButton {
                        text: qsTr("View source repository")
                        iconName: "external-link"
                        quiet: true
                        implicitHeight: 32
                        onClicked: Qt.openUrlExternally("https://github.com/dduongtrandai/LA-Studio")
                    }

                    PrimaryButton {
                        text: qsTr("Open installed licenses")
                        iconName: "file"
                        quiet: true
                        implicitHeight: 32
                        onClicked: root.openInstalledLicenses()
                    }

                    Item { Layout.fillWidth: true }
                }
            }
        }

        Text {
            text: qsTr("Included legal documents")
            color: Theme.textPrimary
            font.pixelSize: Theme.fontMedium
            font.bold: true
        }

        Repeater {
            model: [
                {
                    title: qsTr("Application license"),
                    detail: qsTr("AGPL-3.0 license text for LA Studio."),
                    file: "AGPL-3.0.txt"
                },
                {
                    title: qsTr("Third-party notices"),
                    detail: qsTr("Attribution and license information for bundled and downloaded components."),
                    file: "THIRD-PARTY-NOTICES.md"
                },
                {
                    title: qsTr("Written source offer"),
                    detail: qsTr("How to obtain the corresponding source for this released build."),
                    file: "SOURCE-OFFER.txt"
                },
                {
                    title: qsTr("Runtime component licenses"),
                    detail: qsTr("Qt, curl, zlib, bzip2, 7-Zip, GPL and LGPL license texts."),
                    file: ""
                }
            ]

            delegate: Rectangle {
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: row.implicitHeight + Theme.paddingMedium * 2
                radius: Theme.radiusSmall
                color: Qt.rgba(255, 255, 255, 0.018)
                border.color: Theme.surfaceAlt
                border.width: 1

                RowLayout {
                    id: row
                    anchors.fill: parent
                    anchors.margins: Theme.paddingMedium
                    spacing: Theme.paddingMedium

                    LineIcon {
                        name: "file"
                        color: Theme.accentLight
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            text: modelData.title
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSmall
                            font.bold: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: modelData.detail
                            color: Theme.textSecondary
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }
                    }

                    PrimaryButton {
                        text: qsTr("Open")
                        quiet: true
                        implicitWidth: 72
                        implicitHeight: 30
                        onClicked: root.openInstalledLicenses()
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: Theme.paddingSmall
            text: qsTr("The installed files are the authoritative license texts for this copy. Model-specific terms are shown before a model download starts; some model licenses may require attribution, restrict commercial use, or require separate permission.")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }

        Item { Layout.preferredHeight: Theme.paddingLarge }
    }
}
