import QtQuick
import QtQuick.Controls
import LAStudio

Item {
    id: root

    property string capabilityId: ""
    property Component contentView: null
    property alias studioController: studioController
    property alias studioContext: studioContext
    property bool colabModelSelectionEnabled: false

    signal colabConfigurationAccepted(string familyId, bool openNotebook)

    StudioPageController {
        id: studioController
        capabilityId: root.capabilityId
    }

    StudioContext {
        id: studioContext
        capability: root.capabilityId
        familyId: studioController.selectedFamilyId
        runtimeId: studioController.runtimeId
        runtimeVersion: studioController.runtimeVersion
    }

    function openConfiguration(familyId) {
        // A card click in the picker is only a pending choice.  Keep it local
        // to CapabilityGallery until the user accepts the configuration;
        // otherwise selectionChanged rebuilds the complete studio behind this
        // modal while the pointer event is still being handled.
        var committedFamilyId = studioController.selectedFamilyId || ""
        var requestedFamilyId = familyId || committedFamilyId

        configurationGallery.searchText = ""
        configurationGallery.initialSelectedFiles = ({})
        configurationGallery.selectedFamilyId = requestedFamilyId
        configurationGallery.ensureSelection()

        if (configurationGallery.selectedFamilyId === committedFamilyId) {
            configurationGallery.pendingRuntimeId = studioController.runtimeId || ""
            configurationGallery.pendingRuntimeVersion = studioController.runtimeVersion || ""
            configurationGallery.initialSelectedFiles = studioController.selectedFiles || ({})
        } else {
            configurationGallery.syncPendingRuntime(true)
        }
        configurationDialog.open()
    }

    property bool qmlSmokeSelectionRunning: false
    property bool qmlSmokeSelectionDone: false
    property bool qmlSmokeSelectionPassed: false
    property int qmlSmokeSelectionIndex: 0
    property int qmlSmokeSelectionCount: 0
    property int qmlSmokeSelectionWaitTicks: 0
    property string qmlSmokePendingFamilyId: ""
    property string qmlSmokeControllerFamilyBefore: ""
    property bool qmlSmokeControllerCommittedBefore: false

    // The smoke test deliberately yields to the event loop between every
    // model change. The former synchronous test closed the dialog before QML
    // evaluated its detail bindings and therefore missed the real UI freeze.
    function qmlSmokePendingSelectionIsolated() {
        if (qmlSmokeSelectionDone)
            return qmlSmokeSelectionPassed ? 1 : -1
        if (qmlSmokeSelectionRunning)
            return 0

        qmlSmokeControllerFamilyBefore = studioController.selectedFamilyId || ""
        qmlSmokeControllerCommittedBefore = studioController.selectionCommitted
        qmlSmokeSelectionIndex = 0
        qmlSmokeSelectionCount = 0
        qmlSmokeSelectionWaitTicks = 0
        qmlSmokePendingFamilyId = ""
        qmlSmokeSelectionRunning = true
        openConfiguration(qmlSmokeControllerFamilyBefore)
        qmlSmokeSelectionTimer.start()
        return 0
    }

    function finishQmlSmokeSelection(passed) {
        qmlSmokeSelectionTimer.stop()
        configurationDialog.close()
        qmlSmokeSelectionPassed = passed
        qmlSmokeSelectionDone = true
        qmlSmokeSelectionRunning = false
    }

    Timer {
        id: qmlSmokeSelectionTimer
        interval: 100
        repeat: true

        onTriggered: {
            var rows = studioController.families || []
            if (rows.length < 2) {
                ++root.qmlSmokeSelectionWaitTicks
                if (root.qmlSmokeSelectionWaitTicks > 50)
                    root.finishQmlSmokeSelection(false)
                return
            }

            if (root.qmlSmokePendingFamilyId !== "") {
                if (!configurationGallery.qmlSmokeDetailMatchesSelection()) {
                    root.finishQmlSmokeSelection(false)
                    return
                }
                root.qmlSmokePendingFamilyId = ""
            }

            while (root.qmlSmokeSelectionIndex < rows.length) {
                var familyId = rows[root.qmlSmokeSelectionIndex].id || ""
                ++root.qmlSmokeSelectionIndex
                if (familyId === "")
                    continue
                configurationGallery.selectedFamilyId = familyId
                root.qmlSmokePendingFamilyId = familyId
                ++root.qmlSmokeSelectionCount
                return
            }

            var isolated = studioController.selectedFamilyId
                    === root.qmlSmokeControllerFamilyBefore
                    && studioController.selectionCommitted
                    === root.qmlSmokeControllerCommittedBefore
            root.finishQmlSmokeSelection(isolated
                                         && root.qmlSmokeSelectionCount === rows.length)
        }
    }

    Connections {
        target: studioController
        
        function onConfigurationDialogClosed() {
            configurationDialog.close()
        }
    }

    Dialog {
        id: configurationDialog
        modal: true
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        parent: Overlay.overlay
        width: Math.min(1260, Math.max(980, parent.width - 48))
        height: Math.min(780, Math.max(560, parent.height - 48))
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        background: Rectangle {
            color: Qt.rgba(0.06, 0.06, 0.09, 0.92)
            radius: Theme.radiusMedium
            border.color: Qt.rgba(1, 1, 1, 0.10)
            border.width: 1
        }

        contentItem: Item {
            width: configurationDialog.width
            height: configurationDialog.height

            CapabilityGallery {
                id: configurationGallery
                objectName: "configurationGallery"
                anchors.fill: parent
                capability: root.capabilityId
                modalMode: true
                colabModelSelectionEnabled: root.colabModelSelectionEnabled
                familiesModel: studioController.familiesModel
                onConfigurationAccepted: function(familyId, runtimeId, runtimeVersion, selectedFiles) {
                    studioController.commitConfigurationSelection(familyId, runtimeId, runtimeVersion, selectedFiles)
                }
                onColabConfigurationAccepted: function(familyId, openNotebook) {
                    root.colabConfigurationAccepted(familyId, openNotebook)
                }
            }
        }
    }

    Loader {
        id: contentLoader
        anchors.fill: parent
        sourceComponent: root.contentView

        Binding {
            target: contentLoader.item
            property: "studioContext"
            value: studioContext
        }
        Binding {
            target: contentLoader.item
            property: "studioController"
            value: studioController
        }
    }
}
