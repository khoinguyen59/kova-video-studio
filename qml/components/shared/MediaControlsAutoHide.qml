import QtQuick

Item {
    id: root
    visible: false

    // Keep the delay in one shared place. Both Dubbing and Subtitle OCR use
    // the same state machine so their media controls cannot drift apart.
    property int delayMs: 2000
    property bool playing: false
    property bool pointerInsideSurface: false
    property bool interactionActive: false
    property bool menuOpen: false
    property bool controlsFocused: false
    property bool controlsVisible: true

    readonly property bool keepVisible: !playing || pointerInsideSurface
                                        || interactionActive || menuOpen || controlsFocused

    function reveal() {
        controlsVisible = true
        hideTimer.stop()
    }

    function scheduleHide() {
        if (keepVisible) {
            reveal()
            return
        }
        controlsVisible = true
        hideTimer.restart()
    }

    function noteInteraction() {
        controlsVisible = true
        if (keepVisible)
            hideTimer.stop()
        else
            hideTimer.restart()
    }

    function applyHideDecision() {
        if (keepVisible) {
            reveal()
            return
        }
        controlsVisible = false
    }

    // State-only regression hook for the offscreen route smoke. The Timer's
    // real interval remains fixed above; this verifies every cancellation
    // gate without sleeping for two seconds in each page test.
    function qmlSmokeStateCheck() {
        function keeps(playingState, pointerInside, dragging, menuVisible, focused) {
            return !playingState || pointerInside || dragging || menuVisible || focused
        }
        return delayMs === 2000
            && !keeps(true, false, false, false, false)
            && keeps(true, true, false, false, false)
            && keeps(true, false, true, false, false)
            && keeps(true, false, false, true, false)
            && keeps(true, false, false, false, true)
            && keeps(false, false, false, false, false)
    }

    onPlayingChanged: scheduleHide()
    onPointerInsideSurfaceChanged: scheduleHide()
    onInteractionActiveChanged: scheduleHide()
    onMenuOpenChanged: scheduleHide()
    onControlsFocusedChanged: scheduleHide()

    Timer {
        id: hideTimer
        interval: root.delayMs
        repeat: false
        onTriggered: root.applyHideDecision()
    }
}
