import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LAStudio
import ".."

Dialog {
    id: root

    property var nodes: []
    property var nodeConfigurations: ({})
    property string nodeId: ""
    property string capabilityId: ""
    property string applyError: ""
    // The host explicitly returns whether it accepted this exact selection.
    // Keeping this as a callback rather than a signal result avoids relying
    // on mutation of a cross-component QVariantMap.  Close/Cancel therefore
    // always discard the pending card selection.
    property var configurationApplier: null
    readonly property string selectedFamilyLabel: {
        var family = gallery.selectedFamilyItem()
        return gallery.hasFamilyValue(family)
                ? (family.displayName || family.familyId) : ""
    }

    function applySelectedConfiguration() {
        applyError = ""
        if (!gallery.canUseSelectedFamily()) {
            applyError = qsTr("Install the selected model files and a compatible runtime before applying it.")
            return false
        }

        var selected = gallery.selectedConfiguration()
        var result = root.configurationApplier
                ? root.configurationApplier(root.nodeId, selected.familyId,
                                            selected.runtimeId, selected.runtimeVersion,
                                            selected.selectedFiles)
                : ({ accepted: false,
                     error: qsTr("No Dubbing configuration handler is available.") })
        if (!result.accepted) {
            applyError = result.error || qsTr("LA Studio could not save the selected model configuration.")
            return false
        }

        root.close()
        return true
    }

    function openFor(value) {
        var item = root.nodes ? root.nodes.find(function(entry) { return entry.id === value }) : null
        if (!item || item.configurable !== true) return

        openForCapability(value, item.capabilityId || "stt", item)
    }

    function openForCapability(value, capability, nodeDefinition) {
        root.nodeId = value
        root.capabilityId = capability
        modelController.familiesModel.setCapability(root.capabilityId)

        var item = nodeDefinition
                || (root.nodes ? root.nodes.find(function(entry) { return entry.id === value }) : null)
                || ({})
        var saved = root.nodeConfigurations[root.nodeId] || {}
        var preferredFamilyId = saved.familyId || item.selectedFamilyId
                                || item.defaultFamilyId || ""
        var recommended = preferredFamilyId !== ""
                          ? modelController.familiesModel.configurationForFamily(preferredFamilyId)
                          : modelController.familiesModel.recommendedConfiguration()
        var familyId = preferredFamilyId || recommended.familyId
            || modelController.familiesModel.firstFamilyId()

        gallery.selectedFamilyId = familyId
        gallery.initialSelectedFiles = saved.selectedFiles || recommended.selectedFiles || ({})
        gallery.pendingRuntimeId = saved.runtimeId || recommended.runtimeId || ""
        gallery.pendingRuntimeVersion = saved.runtimeVersion || recommended.runtimeVersion || ""
        root.applyError = ""
        root.open()
    }

    StudioPageController {
        id: modelController
        capabilityId: root.capabilityId
        autoLoadOnSync: false
    }

    parent: Overlay.overlay
    modal: true
    padding: 0
    // A model choice is now committed only by the explicit Apply button.  Do
    // not silently dismiss the pending selection because the user clicked the
    // dimmed overlay while reading the model details.
    closePolicy: Popup.CloseOnEscape
    width: Math.min(1260, Math.max(980, parent ? parent.width - Theme.paddingXL * 2 : 1100))
    height: Math.min(780, Math.max(560, parent ? parent.height - Theme.paddingXL * 2 : 680))
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    standardButtons: Dialog.NoButton

    background: Rectangle {
        color: Theme.background
        radius: Theme.radiusMedium
        border.color: Qt.rgba(1, 1, 1, 0.10)
        border.width: 1
    }

    contentItem: CapabilityGallery {
        id: gallery
        capability: root.capabilityId
        modalMode: true
        familiesModel: modelController.familiesModel
        // The gallery's primary "Use" action is an explicit alternative to
        // the footer Apply button.  It must use the same transactional path,
        // never close the dialog with an uncommitted card selection.
        onConfigurationAccepted: root.applySelectedConfiguration()
    }

    footer: Rectangle {
        implicitHeight: 76
        color: Theme.surfaceAlt
        border.color: Qt.rgba(1, 1, 1, 0.08)
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.paddingMedium
            spacing: Theme.paddingSmall

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    objectName: "workflowNodeModelSelectionSummary"
                    Layout.fillWidth: true
                    text: root.selectedFamilyLabel !== ""
                          ? qsTr("Selected: %1").arg(root.selectedFamilyLabel)
                          : qsTr("Select a model")
                    color: root.selectedFamilyLabel !== "" ? Theme.textPrimary : Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    font.bold: root.selectedFamilyLabel !== ""
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    visible: root.applyError !== ""
                    text: root.applyError
                    color: Theme.danger
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
            }

            PrimaryButton {
                objectName: "workflowNodeModelCancelButton"
                text: qsTr("Cancel")
                quiet: true
                onClicked: root.close()
            }

            PrimaryButton {
                objectName: "workflowNodeModelApplyButton"
                text: qsTr("Apply selected model")
                iconName: "check"
                enabled: gallery.canUseSelectedFamily()
                toolTip: enabled
                         ? qsTr("Save this exact model and runtime for the Dubbing task")
                         : qsTr("Install the selected model files and a compatible runtime first")
                onClicked: root.applySelectedConfiguration()
            }
        }
    }
}
