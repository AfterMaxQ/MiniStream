import QtQuick
import QtQuick.Controls
import MiniStream

Item {
    id: root
    required property var controller

    Rectangle {
        id: videoSurface
        anchors.fill: parent
        color: "#050607"

        MouseArea {
            anchors.fill: parent
            z: -1
            hoverEnabled: true
            property real lastX: 0
            property real lastY: 0
            onEntered: {
                lastX = mouseX
                lastY = mouseY
            }
            onPositionChanged: {
                if (root.controller.remoteInputActive) {
                    root.controller.routeMouseMove(mouseX - lastX, mouseY - lastY)
                }
                lastX = mouseX
                lastY = mouseY
            }
            onPressed: {
                if (root.controller.remoteInputActive) {
                    root.controller.routeMouseButton(mouse.button, true)
                }
            }
            onReleased: {
                if (root.controller.remoteInputActive) {
                    root.controller.routeMouseButton(mouse.button, false)
                }
            }
            onWheel: {
                if (root.controller.remoteInputActive) {
                    root.controller.routeMouseWheel(wheel.angleDelta.y)
                }
            }
        }

        Column {
            z: 1
            anchors.centerIn: parent
            width: Math.min(parent.width - Tokens.space32 * 2, 620)
            spacing: Tokens.space12

            Text {
                width: parent.width
                text: root.controller.selectedDeviceLabel.length > 0
                      ? "Controlling · " + root.controller.selectedDeviceLabel
                      : "Connected"
                color: Tokens.text
                font.pixelSize: 20
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
            Text {
                width: parent.width
                text: "Waiting for video"
                color: Tokens.textMuted
                font.pixelSize: 14
                horizontalAlignment: Text.AlignHCenter
            }
            AppButton {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.controller.remoteInputActive ? "Use this device" : "Control remote"
                onClicked: root.controller.toggleRemoteInput()
            }
            AppButton {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Disconnect"
                onClicked: root.controller.disconnect()
            }
        }
    }
}
