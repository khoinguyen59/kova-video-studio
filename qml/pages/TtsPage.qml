import QtQuick
import "../components/shared"
import "../components/tts"
import LAStudio

StudioPageFrame {
    id: ttsPageFrame
    capabilityId: "tts"
    colabModelSelectionEnabled: true

    function colabNotebookUrl(fileName) {
        return fileName === "" ? ""
            : "https://colab.research.google.com/github/khoinguyen59/kova-video-studio/blob/codex/remote-inference/notebooks/" + fileName
    }

    onColabConfigurationAccepted: function(familyId, openNotebook) {
        if (!AppController.colabTts.selectColabModel(familyId)) return
        studioController.saveConfigurationSelection(familyId, "", "", ({}))
        if (openNotebook) {
            var notebook = AppController.colabTts.colabNotebookFile
            if (notebook !== "") Qt.openUrlExternally(colabNotebookUrl(notebook))
        }
    }

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
