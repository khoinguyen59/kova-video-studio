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

    readonly property bool keepVisible: pointerInsideSurface || interactionActive
                                        || menuOpen || controlsFocused

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

    // State-machine regression hook for the offscreen route smoke. It drives
    // this real component's schedule/reveal/expiry paths without shortening
    // the production timer, so the test covers leave, re-enter, drag, menu,
    // keyboard focus and pause guards without sleeping for two seconds on
    // every route.
    function qmlSmokeStateCheck() {
        var saved = {
            playing: playing,
            pointerInsideSurface: pointerInsideSurface,
            interactionActive: interactionActive,
            menuOpen: menuOpen,
            controlsFocused: controlsFocused,
            controlsVisible: controlsVisible
        }
        var passed = delayMs === 2000

        playing = true
        pointerInsideSurface = false
        interactionActive = false
        menuOpen = false
        controlsFocused = false
        controlsVisible = true
        scheduleHide()
        // The bar remains available for the entire two-second grace period.
        passed = passed && controlsVisible
        applyHideDecision()
        passed = passed && !controlsVisible

        // Entering before an old timer would expire cancels the hide; even an
        // explicit expiry check must leave the controls visible (no flicker).
        pointerInsideSurface = true
        applyHideDecision()
        passed = passed && controlsVisible
        pointerInsideSurface = false
        scheduleHide()
        pointerInsideSurface = true
        applyHideDecision()
        passed = passed && controlsVisible

        pointerInsideSurface = false
        interactionActive = true
        applyHideDecision()
        passed = passed && controlsVisible
        interactionActive = false
        menuOpen = true
        applyHideDecision()
        passed = passed && controlsVisible
        menuOpen = false
        controlsFocused = true
        applyHideDecision()
        passed = passed && controlsVisible
        controlsFocused = false
        playing = false
        applyHideDecision()
        // Pause must not pin controls over an ROI forever. Once pointer,
        // drag/menu and focus have all left, the normal two-second hide rule
        // applies identically after play, pause and seek.
        passed = passed && !controlsVisible

        // The smoke is invoked during app startup. Restore the component
        // exactly and stop the test-created timer before the real route runs.
        hideTimer.stop()
        playing = saved.playing
        pointerInsideSurface = saved.pointerInsideSurface
        interactionActive = saved.interactionActive
        menuOpen = saved.menuOpen
        controlsFocused = saved.controlsFocused
        controlsVisible = saved.controlsVisible
        hideTimer.stop()
        return passed
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
