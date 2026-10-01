import QtQuick
import QtQuick.Controls
import MiniStream

Item {
    id: root
    required property var controller
    property string peerLabel: ""
    readonly property bool controlled: controller.mode === 1
    property bool invalidCode: false
    onVisibleChanged: {
        codeInput.clear()
        invalidCode = false
        if (visible && controlled) Qt.callLater(function() { codeInput.forceActiveFocus() })
    }
    function pair() { invalidCode = !controller.pairWithCode(codeInput.text) }

    Rectangle {
        width: Math.min(parent.width - 48, 460)
        height: content.implicitHeight + 64
        anchors.centerIn: parent
        radius: 24
        color: Tokens.surface
        border.color: Tokens.border
        Column {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 32
        spacing: 20
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: badge.implicitWidth + 24
            height: 28
            radius: 14
            color: "#253b54"
            Text { id: badge; anchors.centerIn: parent; text: "ONE-TIME PAIRING"; color: "#9dc8ff"; font.pixelSize: 10; font.letterSpacing: 1.2 }
        }
        Text {
            width: parent.width
            text: root.controlled ? "Welcome your other device" : "Connect your devices"
            color: Tokens.text
            font.pixelSize: 25
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
        Text {
            width: parent.width
            text: root.peerLabel
            visible: text.length > 0
            color: Tokens.textMuted
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 8
            visible: !root.controlled
            Repeater {
                model: 6
                delegate: Rectangle {
                    required property int index
                    width: 46; height: 62; radius: 10
                    color: Tokens.background
                    border.color: Tokens.border
                    Text {
                        anchors.centerIn: parent
                        text: root.controller.pairingCode.charAt(index) || "·"
                        color: Tokens.text
                        font.pixelSize: 30
                        font.family: "monospace"
                        font.weight: Font.DemiBold
                    }
                }
            }
        }
        TextField {
            id: codeInput
            width: parent.width
            visible: root.controlled
            height: 64
            placeholderText: "000000"
            maximumLength: 6
            validator: RegularExpressionValidator { regularExpression: /[0-9]{6}/ }
            inputMethodHints: Qt.ImhDigitsOnly
            horizontalAlignment: Text.AlignHCenter
            color: Tokens.text
            placeholderTextColor: "#525d67"
            selectionColor: Tokens.accent
            font.pixelSize: 32
            font.family: "monospace"
            font.letterSpacing: 10
            background: Rectangle {
                radius: 12
                color: Tokens.background
                border.width: codeInput.activeFocus ? 2 : 1
                border.color: root.invalidCode ? Tokens.error : codeInput.activeFocus ? Tokens.accent : Tokens.border
            }
            onTextChanged: root.invalidCode = false
            onAccepted: if (acceptableInput) root.pair()
        }
        Text {
            width: parent.width
            text: root.invalidCode ? "That code doesn’t match. Check the six digits and try again."
                : root.controlled ? "Type the code displayed on the device requesting control."
                : "Enter these six digits on the computer you want to control."
            color: root.invalidCode ? Tokens.error : Tokens.textMuted
            font.pixelSize: 13
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }
        AppButton {
            width: parent.width
            text: "Pair & connect"
            visible: root.controlled
            enabled: codeInput.acceptableInput && root.controller.pairingCode.length === 6
            onClicked: root.pair()
        }
        BusyIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 28; height: 28
            running: root.visible && !root.controlled
            visible: !root.controlled
        }
        Text {
            width: parent.width
            text: "Your devices remember each other. Next time, just connect."
            color: Tokens.textMuted
            font.pixelSize: 11
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Cancel pairing"
            flat: true
            onClicked: root.controller.cancelPairing()
        }
        }
    }
}
