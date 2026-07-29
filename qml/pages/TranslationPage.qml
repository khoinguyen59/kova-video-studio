import QtQuick
import "../components/shared"
import "../components/translation"
import "../components/base/colabNotebookUrls.js" as ColabNotebookUrls
import LAStudio

StudioPageFrame {
    id: translationPageFrame
    capabilityId: "translation"
    colabModelSelectionEnabled: true

    onColabConfigurationAccepted: function(familyId, openNotebook) {
        if (!AppController.translation.selectColabModel(familyId)) return
        studioController.saveConfigurationSelection(familyId, "", "", ({}))
        if (openNotebook) {
            var file = AppController.translation.colabNotebookFile
            Qt.openUrlExternally(ColabNotebookUrls.forNotebookFile(file))
        }
        translationPageFrame.showStudio()
    }

    contentView: Component {
        TranslationStudioView {
            studioController: translationPageFrame.studioController
            onBackToGallery: translationPageFrame.openConfiguration(studioController ? studioController.selectedFamilyId : "")
        }
    }
}
