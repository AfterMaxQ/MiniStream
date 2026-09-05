import QtQuick
import QtQuick.Controls
import MiniStream

Item {
    id: root
    required property var controller
    focus: controller.remoteInputActive

    Connections {
        target: root.controller
        function onStateChanged() {
            if (root.controller.remoteInputActive) {
                root.forceActiveFocus()
            }
        }
    }

    Keys.enabled: root.controller.remoteInputActive
    Keys.onPressed: function(event) {
        if (!event.isAutoRepeat) {
            root.controller.routeKey(event.key, true)
        }
        event.accepted = true
    }
    Keys.onReleased: function(event) {
        if (!event.isAutoRepeat) {
            root.controller.routeKey(event.key, false)
        }
        event.accepted = true
    }

    Rectangle {
        id: videoSurface
        anchors.fill: parent
        color: "#050607"

        VideoSurfaceItem {
            id: nativeVideo
            anchors.fill: parent
            bridge: root.controller.videoSurface
        }

        Text {
            anchors.centerIn: parent
            visible: !nativeVideo.frameAvailable
            text: root.controller.videoStatus
            color: Tokens.textMuted
            font.pixelSize: 14
        }

        MouseArea {
            id: remoteMouse
            anchors.fill: parent
            z: 0
            enabled: root.controller.remoteInputActive
            acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
            hoverEnabled: true
            preventStealing: true
            property real lastX: 0
            property real lastY: 0
            onEnabledChanged: {
                lastX = mouseX
                lastY = mouseY
            }
            onEntered: {
                lastX = mouseX
                lastY = mouseY
            }
            onPositionChanged: function(mouse) {
                if (root.controller.remoteInputActive) {
                    root.controller.routeMouseMove(mouseX - lastX, mouseY - lastY)
                }
                lastX = mouseX
                lastY = mouseY
            }
            onPressed: function(mouse) {
                root.forceActiveFocus()
                if (root.controller.remoteInputActive) {
                    root.controller.routeMouseButton(mouse.button, true)
                }
            }
            onReleased: function(mouse) {
                if (root.controller.remoteInputActive) {
                    root.controller.routeMouseButton(mouse.button, false)
                }
            }
            onCanceled: root.controller.releaseRemoteInput()
            onWheel: function(wheel) {
                if (root.controller.remoteInputActive) {
                    root.controller.routeMouseWheel(wheel.angleDelta.y)
                }
            }
        }

        Column {
            z: 1
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Tokens.space16
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
