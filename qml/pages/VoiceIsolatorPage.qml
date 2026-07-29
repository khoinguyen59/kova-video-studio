import QtQuick
import "../components/shared"
import "../components/voiceisolator"

StudioPageFrame {
    id: isolatorPageFrame
    capabilityId: "voice-isolation"
    colabModelSelectionEnabled: true

    function colabNotebookUrl(fileName) {
        return fileName === "" ? ""
            : "https://colab.research.google.com/github/khoinguyen59/kova-video-studio/blob/codex/remote-inference/notebooks/" + fileName
    }

    onColabConfigurationAccepted: function(familyId, openNotebook) {
        if (!AppController.colabVoiceIsolator.selectColabModel(familyId)) return
        studioController.saveConfigurationSelection(familyId, "", "", ({}))
        if (openNotebook) {
            var notebook = AppController.colabVoiceIsolator.colabNotebookFile
            if (notebook !== "") Qt.openUrlExternally(colabNotebookUrl(notebook))
        }
    }

    contentView: Component {
        VoiceIsolatorStudioView {
            studioController: isolatorPageFrame.studioController
            family: {
                var families = studioController.families
                for (var i = 0; i < families.length; ++i) {
                    if (families[i].id === studioController.selectedFamilyId) return families[i]
                }
                return null
            }
            families: studioController.families
            selectedFamilyId: studioController.selectedFamilyId
            studioReady: AppController.colabVoiceIsolator.colabActive || studioController.studioReady
            studioTitle: AppController.colabVoiceIsolator.colabActive
                         ? qsTr("Direct Colab Voice Isolation · %1").arg(AppController.colabVoiceIsolator.model)
                         : studioController.studioHeaderTitle
            modalSelectionTitle: studioController.modalSelectionTitle
            modalSelectionValue: studioController.modalSelectionValue
            modalSelectionDetail: studioController.modalSelectionDetail

            onBackToGallery: isolatorPageFrame.openConfiguration(selectedFamilyId)
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
