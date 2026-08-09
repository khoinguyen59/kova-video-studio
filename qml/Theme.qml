pragma Singleton
import QtQuick

QtObject {
    signal requestShowDownloads()

    readonly property color background:  "#1e1e2e"
    readonly property color surface:     "#2a2a3e"
    readonly property color surfaceAlt:  "#35354a"
    readonly property color border:      "#47475f"
    readonly property color accent:      "#7c4dff"
    readonly property color primary:     accent
    readonly property color accentLight: "#a27eff"
    // These are deliberately brighter than the old muted values.  They are
    // also used by the application palette so native Controls do not fall
    // back to dark system text on a dark surface.
    readonly property color textPrimary: "#f3f1ff"
    readonly property color textSecondary: "#c7c2dc"
    readonly property color danger:      "#ef5350"
    readonly property color error:       danger
    readonly property color success:     "#66bb6a"
    readonly property color warning:     "#ffa726"

    readonly property int radiusSmall:  8
    readonly property int radiusMedium: 12
    readonly property int radiusLarge:  16

    readonly property int paddingSmall:  8
    readonly property int paddingMedium: 12
    readonly property int paddingLarge:  16
    readonly property int paddingXL:     24

    readonly property int fontSmall:  12
    readonly property int fontMedium: 14
    readonly property int fontLarge:  18
    readonly property int fontTitle:  24
    readonly property int fontXLarge: fontTitle

    readonly property int sidebarWidth: 220
    readonly property int iconSize:    20
}
