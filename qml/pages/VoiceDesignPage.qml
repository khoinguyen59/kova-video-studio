import QtQuick
import LAStudio
import "../components/shared"
import "../components/voicedesign"
import "../components/base/colabNotebookUrls.js" as ColabNotebookUrls

StudioPageFrame {
    id: voiceDesignPageFrame
    capabilityId: "voice-design"
    colabModelSelectionEnabled: true

    function colabNotebookUrl(fileName) {
        return ColabNotebookUrls.forNotebookFile(fileName)
    }

    onColabConfigurationAccepted: function(familyId, openNotebook) {
        if (!AppController.colabVoiceDesign.selectColabModel(familyId)) return
        studioController.saveConfigurationSelection(familyId, "", "", ({}))
        if (openNotebook) {
            var notebook = AppController.colabVoiceDesign.colabNotebookFile
            if (notebook !== "") Qt.openUrlExternally(colabNotebookUrl(notebook))
        }
    }

    contentView: Component {
        VoiceDesignStudioView {
            studioController: voiceDesignPageFrame.studioController
            family: {
                var fams = studioController.families
                for (var i = 0; i < fams.length; i++) {
                    if (fams[i].id === studioController.selectedFamilyId) return fams[i]
                }
                return null
            }
            families: studioController.families
            selectedFamilyId: studioController.selectedFamilyId
            studioReady: studioController.studioReady || AppController.colabVoiceDesign.colabActive
            studioTitle: studioController.studioHeaderTitle
            modalSelectionTitle: studioController.modalSelectionTitle
            modalSelectionValue: studioController.modalSelectionValue
            modalSelectionDetail: studioController.modalSelectionDetail
            onBackToGallery: voiceDesignPageFrame.openConfiguration(selectedFamilyId)
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
