import QtQuick
import MiniStream

Item {
    id: root
    required property var controller

    Column {
        width: Math.min(parent.width - Tokens.space32 * 2, 520)
        anchors.centerIn: parent
        spacing: Tokens.space24

        Text {
            width: parent.width
            text: "This device is being controlled"
            color: Tokens.text
            font.pixelSize: 28
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Text {
            width: parent.width
            text: "MiniStream is sending this screen, system audio, and accepted input to the connected controller."
            color: Tokens.textMuted
            font.pixelSize: 14
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        AppButton {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Stop allowing control"
            onClicked: root.controller.disconnect()
        }
    }
}
