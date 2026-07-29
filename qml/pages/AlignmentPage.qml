import QtQuick
import "../components/shared"
import "../components/alignment"
import "../components/base/colabNotebookUrls.js" as ColabNotebookUrls
import LAStudio

StudioPageFrame {
    id: alignmentPageFrame
    capabilityId: "forced-alignment"
    colabModelSelectionEnabled: true

    function colabNotebookUrl(fileName) {
        return ColabNotebookUrls.forNotebookFile(fileName)
    }

    onColabConfigurationAccepted: function(familyId, openNotebook) {
        if (!AppController.colabAlignment.selectColabModel(familyId)) return
        studioController.saveConfigurationSelection(familyId, "", "", ({}))
        if (openNotebook) {
            var notebook = AppController.colabAlignment.colabNotebookFile
            if (notebook !== "") Qt.openUrlExternally(colabNotebookUrl(notebook))
        }
    }

    function alignmentFamilyItem() {
        if (!studioController.familiesModel || !studioController.selectedFamilyId) return null
        var item = studioController.familiesModel.itemForFamily(studioController.selectedFamilyId)
        return item && item.familyId ? item : null
    }

    contentView: Component {
        AlignmentStudioView {
            studioController: alignmentPageFrame.studioController
            modelId: {
                if (AppController.colabAlignment.colabActive)
                    return AppController.colabAlignment.model
                var item = alignmentPageFrame.alignmentFamilyItem()
                return item ? (item.modelId || "") : ""
            }
            runtimeId: studioController.runtimeId
            runtimeVersion: studioController.runtimeVersion
            selectedFiles: studioController.selectedFiles
            configurationReady: {
                var item = alignmentPageFrame.alignmentFamilyItem()
                return !!(item && item.ready)
            }
            executionBackendReady: configurationReady && (
                                   (AppController.runtimes.getRuntimeKindForVersion(runtimeId, runtimeVersion) === "process" &&
                                    AppController.runtimes.getRuntimeExecutablePathForVersion(runtimeId, runtimeVersion) !== "") ||
                                   (AppController.runtimes.getRuntimeKindForVersion(runtimeId, runtimeVersion) !== "process" &&
                                    AppController.runtimes.getRuntimePathForVersion(runtimeId, runtimeVersion) !== ""))
            selectedModelName: {
                if (AppController.colabAlignment.colabActive)
                    return AppController.colabAlignment.model
                var item = alignmentPageFrame.alignmentFamilyItem()
                return item ? item.displayName : qsTr("No alignment model selected")
            }
            selectedModelDetail: {
                var item = alignmentPageFrame.alignmentFamilyItem()
                if (!item) return qsTr("Choose a forced-alignment model and compatible runtime.")
                var runtime = studioController.runtimeId !== "" ? studioController.runtimeId : qsTr("No runtime selected")
                return qsTr("%1 | %2 | Non-commercial").arg(item.statusTitle || item.statusReason || qsTr("Setup required")).arg(runtime)
            }
            configurationStatus: {
                var item = alignmentPageFrame.alignmentFamilyItem()
                if (!item) return qsTr("Setup required")
                return item.ready ? qsTr("Configuration ready") : (item.statusTitle || item.statusReason || qsTr("Setup required"))
            }
            onConfigureRequested: alignmentPageFrame.openConfiguration(studioController ? studioController.selectedFamilyId : "")
        }
    }
}
