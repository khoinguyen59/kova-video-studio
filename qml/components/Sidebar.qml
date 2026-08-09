import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LAStudio
import "base"

Rectangle {
    id: root

    property int currentIndex: 0
    property bool isCollapsed: true
    readonly property bool labelsVisible: !isCollapsed
    readonly property int collapsedWidth: 64
    readonly property int expandedWidth: Theme.sidebarWidth
    property bool activitiesActive: false
    property bool downloadsActive: false
    property bool communityActive: false

    signal navigated(string routeId)
    signal communityClicked()
    signal workflowsClicked()
    signal downloadsClicked()

    color: Qt.darker(Theme.background, 1.02)

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Theme.paddingMedium
        anchors.bottomMargin: Theme.paddingMedium
        spacing: Theme.paddingSmall

        Rectangle {
            Layout.alignment: root.isCollapsed ? Qt.AlignHCenter : Qt.AlignLeft
            Layout.leftMargin: root.isCollapsed ? 0 : Theme.paddingMedium
            Layout.preferredWidth: 38
            Layout.preferredHeight: 38
            radius: 9
            color: Qt.rgba(0.49, 0.30, 1.0, 0.13)
            border.color: Qt.rgba(0.49, 0.30, 1.0, 0.28)
            border.width: 1

            Image {
                anchors.centerIn: parent
                source: "qrc:/LAStudio/icons/app_icon_32.png"
                width: 24
                height: 24
                fillMode: Image.PreserveAspectFit
            }

            AppToolTip {
                text: "LA Studio"
                visible: logoHover.hovered
            }

            HoverHandler {
                id: logoHover
                cursorShape: Qt.PointingHandCursor
            }
        }

        Button {
            id: collapseButton
            Layout.alignment: root.isCollapsed ? Qt.AlignHCenter : Qt.AlignLeft
            Layout.leftMargin: root.isCollapsed ? 0 : Theme.paddingMedium
            Layout.preferredWidth: 38
            Layout.preferredHeight: 30
            flat: true
            Accessible.name: root.isCollapsed ? qsTr("Expand navigation") : qsTr("Collapse navigation")
            onClicked: root.isCollapsed = !root.isCollapsed

            contentItem: LineIcon {
                anchors.centerIn: parent
                name: root.isCollapsed ? "chevron-right" : "chevron-left"
                color: collapseButton.hovered ? Theme.textPrimary : Theme.textSecondary
                width: 18
                height: 18
            }
            background: Rectangle {
                radius: 8
                color: collapseButton.hovered ? Qt.rgba(1, 1, 1, 0.06) : "transparent"
            }
            AppToolTip {
                text: root.isCollapsed ? qsTr("Expand navigation") : qsTr("Collapse navigation")
                visible: collapseButton.hovered
            }
        }

        Rectangle {
            Layout.alignment: root.isCollapsed ? Qt.AlignHCenter : Qt.AlignLeft
            Layout.leftMargin: root.isCollapsed ? 0 : Theme.paddingMedium
            Layout.preferredWidth: root.isCollapsed ? 28 : parent.width - Theme.paddingLarge * 2
            Layout.preferredHeight: 1
            color: Qt.rgba(1, 1, 1, 0.07)
        }

        ScrollView {
            id: primaryRoutesScroll

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: Theme.paddingSmall
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                width: primaryRoutesScroll.availableWidth
                spacing: 6

                Repeater {
                    model: StudioRouteRegistry.routes.filter(function(r) { return r.id !== "settings"; })

                    delegate: Item {
                        required property var modelData
                        required property int index

                        readonly property bool isCurrent: root.currentIndex === StudioRouteRegistry.getIndex(modelData.id)

                        Layout.fillWidth: true
                        Layout.preferredHeight: 46
                        activeFocusOnTab: true
                        Accessible.role: Accessible.Button
                        Accessible.name: modelData.label
                        Accessible.description: isCurrent ? qsTr("Current page") : qsTr("Open %1").arg(modelData.label)
                        Accessible.onPressAction: root.navigated(modelData.id)

                        Keys.onReturnPressed: root.navigated(modelData.id)
                        Keys.onSpacePressed: root.navigated(modelData.id)

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            width: 3
                            height: isCurrent ? 24 : 0
                            radius: 2
                            color: Theme.accentLight

                            Behavior on height {
                                NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
                            }
                        }

                        Rectangle {
                            id: navButton
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: root.isCollapsed ? Math.round((parent.width - width) / 2) : Theme.paddingSmall
                            width: 42
                            height: 42
                            radius: 9
                            color: {
                                if (isCurrent) return Qt.rgba(0.49, 0.30, 1.0, 0.16)
                                if (navMouse.containsMouse) return Qt.rgba(1, 1, 1, 0.055)
                                return "transparent"
                            }
                            border.color: isCurrent ? Qt.rgba(0.49, 0.30, 1.0, 0.34) : "transparent"
                            border.width: 1

                            LineIcon {
                                anchors.centerIn: parent
                                name: modelData.iconName
                                color: isCurrent ? Theme.accentLight : (navMouse.containsMouse ? Theme.textPrimary : Theme.textSecondary)
                                width: 21
                                height: 21
                                strokeWidth: 1.8
                            }

                            Behavior on color { ColorAnimation { duration: 120 } }
                            Behavior on border.color { ColorAnimation { duration: 120 } }
                        }

                        Text {
                            anchors.left: navButton.right
                            anchors.leftMargin: Theme.paddingMedium
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.paddingSmall
                            anchors.verticalCenter: parent.verticalCenter
                            visible: root.labelsVisible
                            text: modelData.label
                            color: isCurrent ? Theme.textPrimary : Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                            font.bold: isCurrent
                            elide: Text.ElideRight
                        }

                        MouseArea {
                            id: navMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.navigated(modelData.id)
                        }

                        AppToolTip {
                            text: modelData.label
                            visible: navMouse.containsMouse
                        }
                    }
                }
            }
        }

        Item {
            id: workflowsItem
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Activity")
            Accessible.description: activeCount > 0
                                    ? qsTr("Open activity. %1 active workflows.").arg(activeCount)
                                    : qsTr("Open activity")
            Accessible.onPressAction: root.workflowsClicked()

            Keys.onReturnPressed: root.workflowsClicked()
            Keys.onSpacePressed: root.workflowsClicked()

            readonly property bool isActive: root.activitiesActive
            readonly property int activeCount: AppController.workflows.sessionCount + AppController.workflows.runningCount

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                width: 3
                height: workflowsItem.isActive ? 24 : 0
                radius: 2
                color: Theme.accentLight

                Behavior on height {
                    NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
                }
            }

            Rectangle {
                id: workflowsNavButton
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: root.isCollapsed ? Math.round((parent.width - width) / 2) : Theme.paddingSmall
                width: 42
                height: 42
                radius: 9
                color: {
                    if (workflowsItem.isActive) return Qt.rgba(0.49, 0.30, 1.0, 0.16)
                    if (workflowsMouse.containsMouse) return Qt.rgba(1, 1, 1, 0.055)
                    if (workflowsItem.activeCount > 0) return Qt.rgba(1.0, 0.65, 0.15, 0.10)
                    return "transparent"
                }
                border.color: {
                    if (workflowsItem.isActive) return Qt.rgba(0.49, 0.30, 1.0, 0.34)
                    if (workflowsItem.activeCount > 0) return Qt.rgba(1.0, 0.65, 0.15, 0.38)
                    return "transparent"
                }
                border.width: 1

                LineIcon {
                    anchors.centerIn: parent
                    name: "activity"
                    color: workflowsItem.isActive ? Theme.accentLight : (workflowsItem.activeCount > 0 ? Theme.warning : (workflowsMouse.containsMouse ? Theme.textPrimary : Theme.textSecondary))
                    width: 21
                    height: 21
                    strokeWidth: 1.8
                }

                Rectangle {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.topMargin: -4
                    anchors.rightMargin: -5
                    width: 16
                    height: 16
                    radius: 8
                    color: Theme.warning
                    border.color: Qt.rgba(0, 0, 0, 0.25)
                    border.width: 1
                    visible: workflowsItem.activeCount > 0

                    Text {
                        anchors.centerIn: parent
                        text: workflowsItem.activeCount > 9 ? "9+" : workflowsItem.activeCount
                        color: Theme.background
                        font.pixelSize: 9
                        font.bold: true
                    }
                }

                Behavior on color { ColorAnimation { duration: 120 } }
                Behavior on border.color { ColorAnimation { duration: 120 } }
            }

            Text {
                anchors.left: workflowsNavButton.right
                anchors.leftMargin: Theme.paddingMedium
                anchors.right: parent.right
                anchors.rightMargin: Theme.paddingSmall
                anchors.verticalCenter: parent.verticalCenter
                visible: root.labelsVisible
                text: qsTr("Activity")
                color: workflowsItem.isActive ? Theme.textPrimary : Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                font.bold: workflowsItem.isActive
                elide: Text.ElideRight
            }

            MouseArea {
                id: workflowsMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.workflowsClicked()
            }

            AppToolTip {
                text: workflowsItem.activeCount > 0 ? qsTr("Activity (%1 active)").arg(workflowsItem.activeCount) : qsTr("Activity")
                visible: workflowsMouse.containsMouse
            }
        }

        Item {
            id: communityItem
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Community and feedback")
            Accessible.description: qsTr("Open the community and feedback page")
            Accessible.onPressAction: root.communityClicked()

            Keys.onReturnPressed: root.communityClicked()
            Keys.onSpacePressed: root.communityClicked()

            readonly property bool isActive: root.communityActive
            readonly property bool spotlight: !isActive && !communityMouse.containsMouse
            // These values are driven by the animations below. Do not create
            // initial bindings here: a PropertyAnimation is also a value
            // source, and binding both causes undefined animation behaviour.
            property real pulseOpacity
            property real pulseScale
            property real buttonScale
            property real buttonRotation
            property real badgeLift
            property real orbitRotation

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                width: 3
                height: communityItem.isActive ? 24 : 0
                radius: 2
                color: Theme.accentLight

                Behavior on height {
                    NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
                }
            }

            Rectangle {
                anchors.centerIn: communityNavButton
                width: 54
                height: 54
                radius: 14
                color: Qt.rgba(0.49, 0.30, 1.0, 0.08)
                border.color: Qt.rgba(0.64, 0.49, 1.0, 0.70)
                border.width: 1
                opacity: communityItem.spotlight ? communityItem.pulseOpacity : 0
                scale: communityItem.spotlight ? communityItem.pulseScale : 0.92
                rotation: communityNavButton.rotation

                Behavior on opacity { NumberAnimation { duration: 160 } }
                Behavior on scale { NumberAnimation { duration: 160 } }
                Behavior on rotation { NumberAnimation { duration: 120 } }
            }

            Item {
                anchors.centerIn: communityNavButton
                width: 58
                height: 58
                rotation: communityItem.spotlight ? communityItem.orbitRotation : 0
                opacity: communityItem.spotlight ? 1 : 0

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    width: 7
                    height: 7
                    radius: 4
                    color: Theme.accentLight
                    border.color: "#ffffff"
                    border.width: 1
                }

                Behavior on opacity { NumberAnimation { duration: 160 } }
            }

            Rectangle {
                id: communityNavButton
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: root.isCollapsed ? Math.round((parent.width - width) / 2) : Theme.paddingSmall
                width: 42
                height: 42
                radius: 9
                scale: communityItem.spotlight ? communityItem.buttonScale : 1.0
                rotation: communityItem.spotlight ? communityItem.buttonRotation : 0
                color: {
                    if (communityItem.isActive) return Qt.rgba(0.49, 0.30, 1.0, 0.16)
                    if (communityMouse.containsMouse) return Qt.rgba(1, 1, 1, 0.055)
                    if (communityItem.spotlight) return Qt.rgba(0.49, 0.30, 1.0, 0.13)
                    return "transparent"
                }
                border.color: {
                    if (communityItem.isActive) return Qt.rgba(0.49, 0.30, 1.0, 0.34)
                    if (communityItem.spotlight) return Qt.rgba(0.64, 0.49, 1.0, 0.55)
                    return "transparent"
                }
                border.width: 1

                LineIcon {
                    anchors.centerIn: parent
                    name: "users"
                    color: (communityItem.isActive || communityItem.spotlight) ? Theme.accentLight : (communityMouse.containsMouse ? Theme.textPrimary : Theme.textSecondary)
                    width: 21
                    height: 21
                    strokeWidth: 1.8
                }

                Rectangle {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.topMargin: -5 + communityItem.badgeLift
                    anchors.rightMargin: -8
                    width: 24
                    height: 14
                    radius: 5
                    color: Theme.accent
                    border.color: Qt.rgba(1, 1, 1, 0.22)
                    border.width: 1
                    visible: !communityItem.isActive
                    opacity: communityMouse.containsMouse ? 1.0 : 0.92

                    Text {
                        anchors.centerIn: parent
                        text: "FB"
                        color: "#ffffff"
                        font.pixelSize: 8
                        font.bold: true
                    }
                }

                Behavior on color { ColorAnimation { duration: 120 } }
                Behavior on border.color { ColorAnimation { duration: 120 } }
                Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                Behavior on rotation { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
            }

            Text {
                anchors.left: communityNavButton.right
                anchors.leftMargin: Theme.paddingMedium
                anchors.right: parent.right
                anchors.rightMargin: Theme.paddingSmall
                anchors.verticalCenter: parent.verticalCenter
                visible: root.labelsVisible
                text: qsTr("Community & Feedback")
                color: communityItem.isActive ? Theme.textPrimary : Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                font.bold: communityItem.isActive
                elide: Text.ElideRight
            }

            SequentialAnimation on pulseOpacity {
                running: communityItem.spotlight
                loops: Animation.Infinite
                NumberAnimation { from: 0.18; to: 0.18; duration: 0 }
                NumberAnimation { to: 0.48; duration: 900; easing.type: Easing.InOutQuad }
                NumberAnimation { to: 0.18; duration: 900; easing.type: Easing.InOutQuad }
            }

            SequentialAnimation on pulseScale {
                running: communityItem.spotlight
                loops: Animation.Infinite
                NumberAnimation { from: 0.92; to: 0.92; duration: 0 }
                NumberAnimation { to: 1.07; duration: 900; easing.type: Easing.InOutQuad }
                NumberAnimation { to: 0.92; duration: 900; easing.type: Easing.InOutQuad }
            }

            SequentialAnimation on buttonScale {
                running: communityItem.spotlight
                loops: Animation.Infinite
                NumberAnimation { from: 1.0; to: 1.0; duration: 0 }
                PauseAnimation { duration: 450 }
                NumberAnimation { to: 1.14; duration: 180; easing.type: Easing.OutCubic }
                NumberAnimation { to: 0.96; duration: 120; easing.type: Easing.InOutQuad }
                NumberAnimation { to: 1.06; duration: 120; easing.type: Easing.OutCubic }
                NumberAnimation { to: 1.0; duration: 140; easing.type: Easing.OutCubic }
                PauseAnimation { duration: 1900 }
            }

            SequentialAnimation on buttonRotation {
                running: communityItem.spotlight
                loops: Animation.Infinite
                NumberAnimation { from: 0; to: 0; duration: 0 }
                PauseAnimation { duration: 360 }
                NumberAnimation { to: -13; duration: 80; easing.type: Easing.OutCubic }
                NumberAnimation { to: 13; duration: 95; easing.type: Easing.InOutQuad }
                NumberAnimation { to: -10; duration: 85; easing.type: Easing.InOutQuad }
                NumberAnimation { to: 9; duration: 80; easing.type: Easing.InOutQuad }
                NumberAnimation { to: -5; duration: 75; easing.type: Easing.InOutQuad }
                NumberAnimation { to: 0; duration: 130; easing.type: Easing.OutCubic }
                PauseAnimation { duration: 1395 }
            }

            SequentialAnimation on badgeLift {
                running: communityItem.spotlight
                loops: Animation.Infinite
                NumberAnimation { from: 0; to: 0; duration: 0 }
                PauseAnimation { duration: 520 }
                NumberAnimation { to: -5; duration: 150; easing.type: Easing.OutCubic }
                NumberAnimation { to: 1; duration: 130; easing.type: Easing.InOutQuad }
                NumberAnimation { to: -2; duration: 100; easing.type: Easing.OutCubic }
                NumberAnimation { to: 0; duration: 140; easing.type: Easing.OutCubic }
                PauseAnimation { duration: 1940 }
            }

            NumberAnimation on orbitRotation {
                running: communityItem.spotlight
                loops: Animation.Infinite
                from: 0
                to: 360
                duration: 1350
                easing.type: Easing.InOutQuad
            }

            MouseArea {
                id: communityMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.communityClicked()
            }

            AppToolTip {
                text: qsTr("Community & Feedback")
                visible: communityMouse.containsMouse
            }
        }

        Item {
            id: downloadsItem
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Downloads")
            Accessible.description: qsTr("Open downloads")
            Accessible.onPressAction: root.downloadsClicked()

            Keys.onReturnPressed: root.downloadsClicked()
            Keys.onSpacePressed: root.downloadsClicked()

            readonly property bool isActive: root.downloadsActive

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                width: 3
                height: downloadsItem.isActive ? 24 : 0
                radius: 2
                color: Theme.accentLight

                Behavior on height {
                    NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
                }
            }

            Rectangle {
                id: downloadsNavButton
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: root.isCollapsed ? Math.round((parent.width - width) / 2) : Theme.paddingSmall
                width: 42
                height: 42
                radius: 9
                color: {
                    if (downloadsItem.isActive) return Qt.rgba(0.49, 0.30, 1.0, 0.16)
                    if (downloadsMouse.containsMouse) return Qt.rgba(1, 1, 1, 0.055)
                    return "transparent"
                }
                border.color: downloadsItem.isActive ? Qt.rgba(0.49, 0.30, 1.0, 0.34) : "transparent"
                border.width: 1

                LineIcon {
                    anchors.centerIn: parent
                    name: "download"
                    color: downloadsItem.isActive ? Theme.accentLight : (downloadsMouse.containsMouse ? Theme.textPrimary : Theme.textSecondary)
                    width: 21
                    height: 21
                    strokeWidth: 1.8
                }

                Behavior on color { ColorAnimation { duration: 120 } }
                Behavior on border.color { ColorAnimation { duration: 120 } }
            }

            Text {
                anchors.left: downloadsNavButton.right
                anchors.leftMargin: Theme.paddingMedium
                anchors.right: parent.right
                anchors.rightMargin: Theme.paddingSmall
                anchors.verticalCenter: parent.verticalCenter
                visible: root.labelsVisible
                text: qsTr("Downloads")
                color: downloadsItem.isActive ? Theme.textPrimary : Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                font.bold: downloadsItem.isActive
                elide: Text.ElideRight
            }

            MouseArea {
                id: downloadsMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.downloadsClicked()
            }

            AppToolTip {
                text: qsTr("Downloads")
                visible: downloadsMouse.containsMouse
            }
        }

        Item {
            id: settingsItem
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: settingsItem.modelData.label
            Accessible.description: settingsItem.isCurrent
                                    ? qsTr("Current page")
                                    : qsTr("Open %1").arg(settingsItem.modelData.label)
            Accessible.onPressAction: root.navigated(settingsItem.modelData.id)

            Keys.onReturnPressed: root.navigated(settingsItem.modelData.id)
            Keys.onSpacePressed: root.navigated(settingsItem.modelData.id)

            readonly property var modelData: {
                for (var i = 0; i < StudioRouteRegistry.routes.length; i++) {
                    if (StudioRouteRegistry.routes[i].id === "settings") {
                        return StudioRouteRegistry.routes[i];
                    }
                }
                return { id: "settings", label: "Settings", iconName: "settings" };
            }
            readonly property bool isCurrent: root.currentIndex === StudioRouteRegistry.getIndex(modelData.id)

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                width: 3
                height: settingsItem.isCurrent ? 24 : 0
                radius: 2
                color: Theme.accentLight

                Behavior on height {
                    NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
                }
            }

            Rectangle {
                id: settingsNavButton
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: root.isCollapsed ? Math.round((parent.width - width) / 2) : Theme.paddingSmall
                width: 42
                height: 42
                radius: 9
                color: {
                    if (settingsItem.isCurrent) return Qt.rgba(0.49, 0.30, 1.0, 0.16)
                    if (settingsMouse.containsMouse) return Qt.rgba(1, 1, 1, 0.055)
                    return "transparent"
                }
                border.color: settingsItem.isCurrent ? Qt.rgba(0.49, 0.30, 1.0, 0.34) : "transparent"
                border.width: 1

                LineIcon {
                    anchors.centerIn: parent
                    name: settingsItem.modelData.iconName
                    color: settingsItem.isCurrent ? Theme.accentLight : (settingsMouse.containsMouse ? Theme.textPrimary : Theme.textSecondary)
                    width: 21
                    height: 21
                    strokeWidth: 1.8
                }

                Behavior on color { ColorAnimation { duration: 120 } }
                Behavior on border.color { ColorAnimation { duration: 120 } }
            }

            Text {
                anchors.left: settingsNavButton.right
                anchors.leftMargin: Theme.paddingMedium
                anchors.right: parent.right
                anchors.rightMargin: Theme.paddingSmall
                anchors.verticalCenter: parent.verticalCenter
                visible: root.labelsVisible
                text: settingsItem.modelData.label
                color: settingsItem.isCurrent ? Theme.textPrimary : Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                font.bold: settingsItem.isCurrent
                elide: Text.ElideRight
            }

            MouseArea {
                id: settingsMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.navigated(settingsItem.modelData.id)
            }

            AppToolTip {
                text: settingsItem.modelData.label
                visible: settingsMouse.containsMouse
            }
        }
    }
}
