pragma Singleton
import QtQuick

QtObject {
    readonly property int space4: 4
    readonly property int space8: 8
    readonly property int space12: 12
    readonly property int space16: 16
    readonly property int space24: 24
    readonly property int space32: 32

    readonly property int radius6: 6
    readonly property int radius10: 10
    readonly property int radius14: 14

    readonly property int motionFast: 120
    readonly property int motionNormal: 180
    readonly property int motionSlow: 220

    readonly property color background: "#111315"
    readonly property color surface: "#191c1f"
    readonly property color surfaceRaised: "#22262a"
    readonly property color border: "#343a40"
    readonly property color text: "#f2f4f5"
    readonly property color textMuted: "#9aa2a9"
    readonly property color accent: "#4e8bd8"
    readonly property color success: "#48a868"
    readonly property color warning: "#d29a41"
    readonly property color error: "#d45b5b"
}
