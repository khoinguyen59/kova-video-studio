import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../base"
import LAStudio

// A task-level manual handoff.  It is intentionally opened from Task Controls
// rather than hidden in a result pane: each workflow task owns its declared
// Colab output contract and the controller still validates every path.
Dialog {
    id: root

    required property var dubbing
    property string requestedNodeId: ""
    property var specs: []

    parent: Overlay.overlay
    modal: true
    title: qsTr("Upload completed Colab output")
    width: Math.min(760, Overlay.overlay.width - Theme.paddingXL * 2)
    height: Math.min(650, Overlay.overlay.height - Theme.paddingXL * 2)
    anchors.centerIn: parent
    standardButtons: Dialog.Close

    function openFor(nodeId) {
        requestedNodeId = nodeId || ""
        specs = dubbing ? dubbing.workflowArtifactSpecsForStage(requestedNodeId) : []
        open()
    }

    onOpened: specs = dubbing ? dubbing.workflowArtifactSpecsForStage(requestedNodeId) : []

    contentItem: ColumnLayout {
        spacing: Theme.paddingSmall
        Text {
            Layout.fillWidth: true
            text: qsTr("Upload only the exact files declared below. Each accepted file is copied into this project, then the normal next task can continue. No hidden local fallback is used.")
            color: Theme.textSecondary
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontSmall
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent ? parent.width : 0
                spacing: Theme.paddingMedium
                Repeater {
                    model: root.specs
                    delegate: DubbingArtifactUploadPanel {
                        dubbing: root.dubbing
                        nodeId: modelData.nodeId || ""
                        Layout.fillWidth: true
                    }
                }
                Text {
                    visible: root.specs.length === 0
                    Layout.fillWidth: true
                    text: qsTr("This task has no remote output to upload. Import/Download accepts source media; all model and render tasks expose their own output handoff.")
                    color: Theme.textSecondary
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
