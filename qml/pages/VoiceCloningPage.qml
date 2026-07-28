import QtQuick
import LAStudio
import "../components/shared"
import "../components/voicecloning"

StudioPageFrame {
    id: cloningPageFrame
    capabilityId: "voice-cloning"
    colabModelSelectionEnabled: true

    function colabNotebookUrl(fileName) {
        return fileName === "" ? ""
            : "https://colab.research.google.com/github/khoinguyen59/kova-video-studio/blob/codex/remote-inference/notebooks/" + fileName
    }

    onColabConfigurationAccepted: function(familyId, openNotebook) {
        if (!AppController.colabVoiceClone.selectColabModel(familyId)) return
        studioController.saveConfigurationSelection(familyId, "", "", ({}))
        if (openNotebook) {
            var notebook = AppController.colabVoiceClone.colabNotebookFile
            if (notebook !== "") Qt.openUrlExternally(colabNotebookUrl(notebook))
        }
    }

    contentView: Component {
        VoiceCloningStudioView {
            studioController: cloningPageFrame.studioController
            family: {
                var fams = studioController.families
                for (var i = 0; i < fams.length; i++) {
                    if (fams[i].id === studioController.selectedFamilyId) return fams[i]
                }
                return null
            }
            families: studioController.families
            selectedFamilyId: studioController.selectedFamilyId
            studioReady: studioController.studioReady || AppController.colabVoiceClone.colabActive
            studioTitle: studioController.studioHeaderTitle
            modalSelectionTitle: studioController.modalSelectionTitle
            modalSelectionValue: studioController.modalSelectionValue
            modalSelectionDetail: studioController.modalSelectionDetail
            onBackToGallery: cloningPageFrame.openConfiguration(selectedFamilyId)
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
