import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LAStudio

// Shared status/action surface for every Direct Colab panel.  Feature panels
// own their URL/token draft fields and Connect/Replace action, while this
// component always exposes the real session state plus Check and Disconnect.
// It never receives or renders the bearer token.
ColumnLayout {
    id: root

    property var session: null
    property bool showDisconnected: true
    property bool showDisconnect: true
    // Dubbing handles these signals through its controller so the workflow
    // snapshot stays in sync; all other feature surfaces use their session.
    property bool useExternalActions: false
    signal checkRequested()
    signal disconnectRequested()

    function requestCheck() {
        if (root.useExternalActions) root.checkRequested()
        else if (root.session) root.session.checkConnection()
    }

    function requestDisconnect() {
        if (root.useExternalActions) root.disconnectRequested()
        else if (root.session) root.session.disconnectTemporaryWorker()
    }

    visible: !!session && (showDisconnected || session.checking
                           || session.active || session.lastError !== "")
    spacing: Theme.paddingSmall

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.paddingSmall

        Rectangle {
            Layout.preferredWidth: 8
            Layout.preferredHeight: 8
            radius: 4
            color: {
                if (!root.session) return Theme.textSecondary
                if (root.session.checking) return Theme.warning
                if (root.session.active && root.session.verified) return Theme.success
                if (root.session.lastError !== "") return Theme.danger
                return Theme.textSecondary
            }
        }

        Text {
            Layout.fillWidth: true
            text: {
                if (!root.session) return qsTr("Colab worker is not connected")
                if (root.session.checking) return root.session.verificationMessage
                if (root.session.active && root.session.verified)
                    return root.session.verificationMessage
                if (root.session.lastError !== "") return root.session.lastError
                return qsTr("Colab worker is not connected")
            }
            color: {
                if (!root.session) return Theme.textSecondary
                if (root.session.checking) return Theme.warning
                if (root.session.active && root.session.verified) return Theme.success
                if (root.session.lastError !== "") return Theme.danger
                return Theme.textSecondary
            }
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }
    }

    Text {
        Layout.fillWidth: true
        visible: !!root.session && root.session.active && root.session.verified
        text: {
            if (!root.session) return ""
            var checked = root.session.verifiedAt === ""
                    ? qsTr("time unavailable") : root.session.verifiedAt
            return qsTr("Worker: %1\nVerified route: %2 / %3 / %4\nChecked: %5")
                    .arg(root.session.workerUrl)
                    .arg(root.session.expectedCapability)
                    .arg(root.session.expectedModel)
                    .arg(root.session.expectedVariant)
                    .arg(checked)
        }
        color: Theme.textSecondary
        font.pixelSize: 10
        wrapMode: Text.WrapAnywhere
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.paddingSmall

        Button {
            id: checkButton
            visible: !!root.session
            enabled: !!root.session && !root.session.checking
            text: root.session && root.session.checking ? qsTr("Checking…") : qsTr("Check connection")
            font.pixelSize: 10
            onClicked: root.requestCheck()
            contentItem: Text {
                text: parent.text
                color: parent.enabled ? Theme.textPrimary : Theme.textSecondary
                font: parent.font
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: Theme.radiusSmall
                color: checkButton.down ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.75)
                                        : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.22)
                border.color: Qt.rgba(Theme.accentLight.r, Theme.accentLight.g, Theme.accentLight.b, 0.65)
            }
            ToolTip {
                text: qsTr("Connect or Replace the Worker URL and Session token first.")
                visible: checkButton.hovered && !checkButton.enabled
            }
        }

        Button {
            id: disconnectButton
            visible: root.showDisconnect && !!root.session && root.session.active
            enabled: !!root.session && !root.session.checking
            text: qsTr("Disconnect")
            font.pixelSize: 10
            onClicked: root.requestDisconnect()
            contentItem: Text {
                text: parent.text
                color: parent.enabled ? Theme.textPrimary : Theme.textSecondary
                font: parent.font
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: Theme.radiusSmall
                color: disconnectButton.down ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.75)
                                             : Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.16)
                border.color: Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.55)
            }
        }

        Item { Layout.fillWidth: true }
    }
}
