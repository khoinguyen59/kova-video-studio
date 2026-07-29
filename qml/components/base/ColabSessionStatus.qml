import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LAStudio

RowLayout {
    id: root

    property var session: null
    property bool showDisconnected: false

    visible: !!session && (showDisconnected || session.checking
                           || session.active || session.lastError !== "")
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

    Button {
        visible: !!root.session && root.session.active
        enabled: !!root.session && !root.session.checking
        text: root.session && root.session.checking ? qsTr("Checking…") : qsTr("Check connection")
        font.pixelSize: 10
        onClicked: root.session.checkConnection()
        contentItem: Text {
            text: parent.text
            color: parent.enabled ? Theme.textPrimary : Theme.textSecondary
            font: parent.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: Theme.radiusSmall
            color: parent.down ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.75)
                               : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.22)
            border.color: Qt.rgba(Theme.accentLight.r, Theme.accentLight.g, Theme.accentLight.b, 0.65)
        }
    }
}
