.pragma library

// Keep the repository and branch used by every "Open in Colab" action in one
// audited place. Controllers decide the exact notebook filename from the
// selected model; this helper only turns that filename into a URL.
var notebookBaseUrl = "https://colab.research.google.com/github/khoinguyen59/kova-video-studio/blob/codex/remote-inference/notebooks/"

function forNotebookFile(fileName) {
    return fileName === "" ? "" : notebookBaseUrl + fileName
}
