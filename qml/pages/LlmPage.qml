import QtQuick
import "../components/shared"
import "../components/llm"
import "../components/base/colabNotebookUrls.js" as ColabNotebookUrls
import LAStudio

StudioPageFrame {
    id: frame
    capabilityId: "llm-chat"
    colabModelSelectionEnabled: true

    onColabConfigurationAccepted: function(familyId, openNotebook) {
        if (!AppController.llmChat.selectColabModel(familyId)) return
        studioController.saveConfigurationSelection(familyId, "", "", ({}))
        if (openNotebook) {
            var file = AppController.llmChat.colabNotebookFile
            Qt.openUrlExternally(ColabNotebookUrls.forNotebookFile(file))
        }
        frame.showStudio()
    }

    contentView: Component {
        LlmChatStudioView {
            studioController: frame.studioController
            onBackToGallery: frame.openConfiguration(frame.studioController ? frame.studioController.selectedFamilyId : "")
        }
    }
}
