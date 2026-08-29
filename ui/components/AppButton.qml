import QtQuick
import QtQuick.Controls
import MiniStream

Button {
    id: control
    implicitHeight: 42
    leftPadding: Tokens.space16
    rightPadding: Tokens.space16
    font.pixelSize: 14
    font.weight: Font.DemiBold

    contentItem: Text {
        text: control.text
        color: control.enabled ? Tokens.text : Tokens.textMuted
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        radius: Tokens.radius6
        color: control.enabled
               ? (control.down ? Qt.darker(Tokens.accent, 1.15) : Tokens.accent)
               : Tokens.surfaceRaised
        border.color: control.enabled ? Tokens.accent : Tokens.border
    }
}
