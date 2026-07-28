import QtQuick
import QtQuick.Controls
import LAStudio

Item {
    id: root

    property string capabilityId: ""
    property Component contentView: null
    property alias studioController: studioController
    property alias studioContext: studioContext

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

    // Exercised by the offscreen QML smoke test.  It reproduces the exact
    // pre-confirmation model switch that used to freeze the real application
    // and verifies that the controller remains untouched.
    function qmlSmokePendingSelectionIsolated() {
        var controllerFamilyBefore = studioController.selectedFamilyId || ""
        var controllerCommittedBefore = studioController.selectionCommitted
        openConfiguration(controllerFamilyBefore)

        var rows = studioController.families || []
        var currentGalleryFamily = configurationGallery.selectedFamilyId
        var nextFamily = ""
        for (var i = 0; i < rows.length; ++i) {
            if (rows[i].id && rows[i].id !== currentGalleryFamily) {
                nextFamily = rows[i].id
                break
            }
        }
        if (nextFamily !== "")
            configurationGallery.selectedFamilyId = nextFamily

        var isolated = studioController.selectedFamilyId === controllerFamilyBefore
                && studioController.selectionCommitted === controllerCommittedBefore
        configurationDialog.close()
        return nextFamily === "" || isolated
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
                familiesModel: studioController.familiesModel
                onConfigurationAccepted: function(familyId, runtimeId, runtimeVersion, selectedFiles) {
                    studioController.commitConfigurationSelection(familyId, runtimeId, runtimeVersion, selectedFiles)
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
