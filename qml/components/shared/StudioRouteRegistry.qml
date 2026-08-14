pragma Singleton
import QtQuick

QtObject {
    id: root

    // Mapping route IDs to StackLayout indexes
    readonly property var routeMap: {
        "welcome": 0,
        "studio-stt": 1,
        "studio-tts": 2,
        "studio-voice-cloning": 3,
        "studio-voice-design": 4,
        "studio-voice-isolator": 5,
        "studio-alignment": 6,
        "studio-translation": 7,
        "studio-dubbing": 8,
        "studio-llm": 9,
        "models": 10,
        "my-models": 11,
        "developer": 12,
        "settings": 13,
        "media-download": 14,
        "subtitle-ocr": 15,
        "tools-alignment": 6
    }

    // List of route descriptors for Sidebar / navigation
    readonly property var routes: [
        { id: "welcome", label: qsTr("Home"), iconName: "home" },
        { id: "studio-stt", label: qsTr("Speech to Text"), iconName: "mic", homeCard: true, group: qsTr("Transcription"), description: qsTr("Convert audio files or microphone recordings into editable local transcripts."), accent: "#64b5f6" },
        { id: "studio-tts", label: qsTr("Text to Speech"), iconName: "volume", homeCard: true, group: qsTr("Generation"), description: qsTr("Generate natural speech from scripts using models installed on this device."), accent: "#a27eff" },
        { id: "studio-voice-cloning", label: qsTr("Voice Cloning"), iconName: "spark", homeCard: true, group: qsTr("Custom Voice"), description: qsTr("Create speech from a reference voice while keeping the workflow offline."), accent: "#ff8fb3" },
        { id: "studio-voice-design", label: qsTr("Voice Design"), iconName: "waves", homeCard: true, group: qsTr("Voice Control"), description: qsTr("Describe voice characteristics and generate speech with controllable style."), accent: "#7bd88f" },
        { id: "studio-voice-isolator", label: qsTr("Voice Isolator"), iconName: "voice-isolator" },
        { id: "studio-alignment", label: qsTr("Alignment"), iconName: "alignment", homeCard: true, group: qsTr("Timing"), description: qsTr("Match a known transcript to audio and export precise local timestamps."), accent: "#ffa726" },
        { id: "studio-translation", label: qsTr("Translation"), iconName: "translate", homeCard: true, group: qsTr("Localization"), description: qsTr("Translate scripts and subtitles locally, then review each segment."), accent: "#64d8cb" },
        { id: "studio-dubbing", label: qsTr("Dubbing"), iconName: "dubbing", homeCard: true, group: qsTr("Localization"), description: qsTr("Create timestamped translated speech tracks from local audio or video."), accent: "#64d8cb" },
        { id: "studio-llm", label: qsTr("LLM Chat"), iconName: "chat", homeCard: true, group: qsTr("Language Models"), description: qsTr("Chat privately with local language models powered by llama.cpp."), accent: "#a27eff" },
        { id: "models", label: qsTr("Models"), iconName: "gallery" },
        { id: "my-models", label: qsTr("My Models"), iconName: "folder" },
        { id: "developer", label: qsTr("Developer"), iconName: "code" },
        { id: "settings", label: qsTr("Settings"), iconName: "settings" },
        { id: "media-download", label: qsTr("Download"), iconName: "download", homeCard: true, group: qsTr("Media"), description: qsTr("Download supported public media pages locally, then choose files explicitly for a project."), accent: "#f6c453" },
        { id: "subtitle-ocr", label: qsTr("Subtitle OCR"), iconName: "scan", homeCard: true, group: qsTr("Subtitles"), description: qsTr("Extract burned-in image subtitles from video with a reviewed transcript workflow."), accent: "#b18cff" }
    ]

    // Home derives its cards from the same route descriptors used by Sidebar.
    // Adding a feature requires one registry entry, preventing Home/route drift.
    readonly property var homeFeatureCards: routes.filter(function(route) {
        return route.homeCard === true
    })

    readonly property var capabilityRouteMap: {
        "stt": "studio-stt",
        "tts": "studio-tts",
        "voice-cloning": "studio-voice-cloning",
        "voice-design": "studio-voice-design",
        "forced-alignment": "studio-alignment",
        "translation": "studio-translation",
        "dubbing": "studio-dubbing",
        "voice-isolation": "studio-voice-isolator",
        "llm-chat": "studio-llm"
    }

    function getIndex(routeId) {
        var idx = routeMap[routeId]
        return idx !== undefined ? idx : 0
    }

    function getRouteId(index) {
        for (var key in routeMap) {
            if (routeMap[key] === index) return key
        }
        return "welcome"
    }

    function routeForCapability(capabilityId) {
        var routeId = capabilityRouteMap[capabilityId]
        return routeId !== undefined ? routeId : "welcome"
    }
}
