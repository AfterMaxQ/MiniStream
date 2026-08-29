import QtQuick
import MiniStream

Item {
    id: root
    required property var controller

    Column {
        width: 420
        anchors.centerIn: parent
        spacing: Tokens.space24

        Text {
            width: parent.width
            text: "Confirm pairing code"
            color: Tokens.text
            font.pixelSize: 24
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
        }
        Text {
            width: parent.width
            text: root.controller.pairingCode
            color: Tokens.text
            font.pixelSize: 44
            font.family: "monospace"
            font.letterSpacing: 4
            horizontalAlignment: Text.AlignHCenter
        }
        Text {
            width: parent.width
            text: "Confirm the same code is shown on the Windows PC."
            color: Tokens.textMuted
            font.pixelSize: 14
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Tokens.space12
            AppButton { text: "Cancel"; onClicked: root.controller.cancelPairing() }
            AppButton { text: "Confirm"; onClicked: root.controller.confirmPairing() }
        }
    }
}
