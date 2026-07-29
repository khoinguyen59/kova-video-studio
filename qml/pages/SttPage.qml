import QtQuick
import "../components/shared"
import "../components/stt"
import LAStudio

StudioPageFrame {
    id: sttPageFrame
    capabilityId: "stt"
    colabModelSelectionEnabled: true

    function colabNotebookUrl(fileName) {
        return fileName === "" ? ""
            : "https://colab.research.google.com/github/khoinguyen59/kova-video-studio/blob/codex/remote-inference/notebooks/" + fileName
    }

    onColabConfigurationAccepted: function(familyId, openNotebook) {
        if (!AppController.sttSession.selectColabModel(familyId)) return
        studioController.saveConfigurationSelection(familyId, "", "", ({}))
        if (openNotebook) {
            var notebook = AppController.sttSession.colabNotebookFile
            if (notebook !== "") Qt.openUrlExternally(colabNotebookUrl(notebook))
        }
    }

    contentView: Component {
        SttStudioView {
            studioController: sttPageFrame.studioController
            sttSession: AppController.sttSession

            onBackToGallery: sttPageFrame.openConfiguration(studioController ? studioController.selectedFamilyId : "")
        }
    }
}
