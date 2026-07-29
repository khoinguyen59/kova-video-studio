.pragma library

// Keep the repository and release branch used by every "Open in Colab" action
// in one audited place. Controllers decide the exact notebook filename from
// the selected model; this helper only turns that filename into a URL.  Do not
// point this at a development branch: a model selection must open the notebook
// that is committed on the repository's main release branch.
var notebookBaseUrl = "https://colab.research.google.com/github/khoinguyen59/kova-video-studio/blob/main/notebooks/"

function forNotebookFile(fileName) {
    return fileName === "" ? "" : notebookBaseUrl + fileName
}
