import QtQuick
import QtQuick.Controls
import MiniStream

Item {
    id: root
    required property var controller
    property string peerLabel: ""
    readonly property bool controlled: controller.mode === 1
    property bool invalidCode: false
    onVisibleChanged: { codeInput.clear(); invalidCode = false }
    function pair() { invalidCode = !controller.pairWithCode(codeInput.text) }

    Column {
        width: Math.min(parent.width - Tokens.space32 * 2, 420)
        anchors.centerIn: parent
        spacing: Tokens.space24

        Text {
            width: parent.width
            text: root.peerLabel.length > 0 ? root.peerLabel : "Pair device"
            color: Tokens.text
            font.pixelSize: 24
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            text: root.controller.pairingCode
            visible: !root.controlled
            color: Tokens.text
            font.pixelSize: 44
            font.family: "monospace"
            font.letterSpacing: 4
            horizontalAlignment: Text.AlignHCenter
        }
        TextField {
            id: codeInput
            width: parent.width
            visible: root.controlled
            placeholderText: "6-digit code from the other device"
            maximumLength: 6
            validator: RegularExpressionValidator { regularExpression: /[0-9]{6}/ }
            inputMethodHints: Qt.ImhDigitsOnly
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 28
            onTextChanged: root.invalidCode = false
            onAccepted: if (acceptableInput) root.pair()
        }
        Text {
            width: parent.width
            text: root.invalidCode ? "Code does not match. Try again."
                : root.controlled ? "Enter the code shown on the remote-control device."
                : "Enter this code once on the device you want to control. Future connections are automatic."
            color: root.invalidCode ? Tokens.warning : Tokens.textMuted
            font.pixelSize: 14
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Tokens.space12
            AppButton { text: "Cancel"; onClicked: root.controller.cancelPairing() }
            AppButton {
                text: "Pair"
                visible: root.controlled
                enabled: codeInput.acceptableInput && root.controller.pairingCode.length === 6
                onClicked: root.pair()
            }
        }
    }
}
