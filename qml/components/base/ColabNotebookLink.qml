import QtQuick
import QtQuick.Layouts
import LAStudio
import ".."
import "colabNotebookUrls.js" as ColabNotebookUrls

ColumnLayout {
    id: root

    property string notebookFile: ""
    // Internal builds publish these notebooks to the GitHub branch used by
    // this application. Colab opens that exact file directly; no worker token
    // is ever part of this URL.
    readonly property string colabNotebookUrl: ColabNotebookUrls.forNotebookFile(notebookFile)

    Layout.fillWidth: true
    spacing: Theme.paddingSmall

    Text {
        Layout.fillWidth: true
        visible: root.notebookFile !== ""
        text: qsTr("Notebook to run in Colab: %1").arg(root.notebookFile)
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSmall
        wrapMode: Text.WordWrap
    }

    Text {
        Layout.fillWidth: true
        visible: root.notebookFile !== ""
        text: qsTr("GitHub-backed notebook: open the named file on this branch, run it with a Colab GPU runtime, then copy only the temporary worker URL and token shown by the notebook. Sign in to GitHub only if this repository is private.")
        color: Theme.warning
        font.pixelSize: 10
        wrapMode: Text.WordWrap
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.paddingSmall

        PrimaryButton {
            text: qsTr("Open this notebook in Colab")
            iconName: "cloud"
            quiet: true
            onClicked: Qt.openUrlExternally(root.colabNotebookUrl)
        }
        PrimaryButton {
            text: qsTr("Open notebook folder")
            iconName: "folder"
            quiet: true
            onClicked: AppController.openColabNotebooksDirectory()
        }
        Item { Layout.fillWidth: true }
    }
}
