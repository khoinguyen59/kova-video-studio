import QtQuick
import "../components/shared"
import "../components/translation"
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
            Qt.openUrlExternally("https://colab.research.google.com/github/khoinguyen59/kova-video-studio/blob/codex/remote-inference/notebooks/" + file)
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
