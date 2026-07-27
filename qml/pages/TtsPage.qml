import QtQuick
import "../components/shared"
import "../components/tts"

StudioPageFrame {
    id: ttsPageFrame
    capabilityId: "tts"

    contentView: Component {
        TtsStudioView {
            studioController: ttsPageFrame.studioController
            family: {
                var fams = studioController.families
                for (var i = 0; i < fams.length; i++) {
                    if (fams[i].id === studioController.selectedFamilyId) return fams[i]
                }
                return null
            }
            families: studioController.families
            selectedFamilyId: studioController.selectedFamilyId
            studioReady: studioController.studioReady || AppController.gatewayTts.gatewayActive || AppController.colabTts.colabActive
            studioTitle: studioController.studioHeaderTitle
            modalSelectionTitle: studioController.modalSelectionTitle
            modalSelectionValue: studioController.modalSelectionValue
            modalSelectionDetail: studioController.modalSelectionDetail
            onBackToGallery: ttsPageFrame.openConfiguration(selectedFamilyId)
            onReloadRequested: studioController.reload()
            onEjectRequested: studioController.unload()
            onModelSwitchRequested: function(nextFamilyId) {
                studioController.selectFamily(nextFamilyId)
                studioController.commitSelection()
            }
            onRuntimeSwitchRequested: function(nextRuntimeId) {
                studioController.selectRuntime(nextRuntimeId, "")
                studioController.commitSelection()
            }
        }
    }
}
