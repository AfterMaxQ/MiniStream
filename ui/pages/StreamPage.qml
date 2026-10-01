import QtQuick
import QtQuick.Controls
import MiniStream

Item {
    id: root
    required property var controller
    required property var appWindow
    property bool toolbarOpen: true
    readonly property bool gameMouse: controller.gameMode
    property string captureError: ""
    property bool wasRemote: false
    readonly property bool routing: visible && controller.remoteInputActive && !toolbarOpen
    readonly property string releaseShortcut: Qt.platform.os === "osx"
        ? "Cmd+Option+Shift+R" : "Ctrl+Alt+Shift+R"
    focus: routing

    function showControls() {
        controller.releaseRemoteInput()
        toolbarOpen = true
    }
    function resumeControl() {
        captureError = ""
        toolbarOpen = false
        if (!controller.remoteInputActive) controller.toggleRemoteInput()
        forceActiveFocus()
    }
    onVisibleChanged: {
        if (visible) toolbarOpen = true
        else controller.releaseRemoteInput()
    }
    Connections {
        target: root.controller
        function onStateChanged() {
            if (root.wasRemote && !root.controller.remoteInputActive) root.toolbarOpen = true
            if (!root.wasRemote && root.controller.remoteInputActive) root.toolbarOpen = false
            root.wasRemote = root.controller.remoteInputActive
            if (root.routing) root.forceActiveFocus()
        }
    }
    Keys.enabled: routing
    Keys.onPressed: function(event) {
        if (!event.isAutoRepeat) root.controller.routeKey(event.key, true)
        event.accepted = true
    }
    Keys.onReleased: function(event) {
        if (!event.isAutoRepeat) root.controller.routeKey(event.key, false)
        event.accepted = true
    }

    RelativeMouseCapture {
        id: relativeMouse
        window: root.appWindow
        active: root.routing && root.gameMouse && root.appWindow.active
        onMoved: function(dx, dy) { root.controller.routeMouseMove(dx, dy) }
        onCaptureFailed: function(reason) { root.captureError = reason; root.showControls() }
    }
    Rectangle { anchors.fill: parent; color: "#050607" }
    VideoSurfaceItem {
        id: nativeVideo
        anchors.fill: parent
        bridge: root.controller.videoSurface
        onHdrOutputChanged: root.controller.setHdrOutputAvailable(hdrOutput)
    }
    Text {
        anchors.centerIn: parent
        visible: !nativeVideo.frameAvailable
        text: root.controller.videoStatus
        color: Tokens.textMuted
        font.pixelSize: 16
    }
    MouseArea {
        id: remoteMouse
        anchors.fill: parent
        enabled: root.routing
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        hoverEnabled: true
        preventStealing: true
        cursorShape: root.routing ? Qt.BlankCursor : Qt.ArrowCursor
        function positionRemote(x, y) {
            if (root.gameMouse) return true
            if (!root.routing) return false
            const fw = Math.min(width, height * nativeVideo.aspectRatio)
            const fh = fw / nativeVideo.aspectRatio
            const left = (width - fw) / 2
            const top = (height - fh) / 2
            if (fw <= 0 || fh <= 0 || x < left || x > left + fw || y < top || y > top + fh) return false
            root.controller.routeMousePosition(Math.round((x - left) / fw * 65535),
                                               Math.round((y - top) / fh * 65535))
            return true
        }
        onPositionChanged: function(mouse) { positionRemote(mouse.x, mouse.y) }
        onPressed: function(mouse) {
            root.forceActiveFocus()
            if (positionRemote(mouse.x, mouse.y)) root.controller.routeMouseButton(mouse.button, true)
        }
        onReleased: function(mouse) { root.controller.routeMouseButton(mouse.button, false) }
        onCanceled: root.controller.releaseRemoteInput()
        onWheel: function(wheel) { root.controller.routeMouseWheel(wheel.angleDelta.y) }
    }

    AppButton {
        z: 6
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 12
        visible: root.visible && !root.toolbarOpen && !relativeMouse.active
        text: "Use this device"
        onClicked: root.showControls()
    }
    Rectangle {
        id: toolbar
        z: 5
        width: Math.min(parent.width - 24, 680)
        height: toolbarContent.implicitHeight + 24
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.toolbarOpen ? 12 : -height - 12
        opacity: root.toolbarOpen ? 1 : 0
        visible: opacity > 0
        enabled: root.toolbarOpen
        radius: 12
        color: "#f022252b"
        border.color: Tokens.border
        Behavior on y { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 140 } }
        Column {
            id: toolbarContent
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 12
            spacing: 10
            Text {
                width: parent.width
                text: root.controller.selectedDeviceLabel
                color: Tokens.text
                font.pixelSize: 15
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Flow {
                width: parent.width
                spacing: 8
                AppButton {
                    text: "Control remote"
                    onClicked: root.resumeControl()
                }
                AppButton {
                    text: root.appWindow.visibility === Window.FullScreen ? "Exit fullscreen" : "Fullscreen"
                    onClicked: { root.controller.releaseRemoteInput(); root.appWindow.toggleFullscreen() }
                }
                AppButton {
                    text: "Disconnect"
                    onClicked: root.controller.disconnect()
                }
            }
            Row {
                width: parent.width
                spacing: 10
                Repeater {
                    model: [
                        { game: false, title: "Desktop", detail: "Point, select & drag\nCursor follows your screen position" },
                        { game: true, title: "Game", detail: "Unlimited mouse look\nPhysical keys, sensitivity & gamepad" }
                    ]
                    delegate: Rectangle {
                        required property var modelData
                        width: (toolbarContent.width - 10) / 2
                        height: 92
                        radius: 10
                        color: root.gameMouse === modelData.game ? "#253b54" : Tokens.surface
                        border.color: root.gameMouse === modelData.game ? Tokens.accent : Tokens.border
                        Column {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 6
                            Text { text: modelData.title; color: Tokens.text; font.pixelSize: 15; font.bold: true }
                            Text { text: modelData.detail; color: Tokens.textMuted; font.pixelSize: 11; width: parent.width; wrapMode: Text.WordWrap }
                        }
                        MouseArea { anchors.fill: parent; onClicked: root.controller.gameMode = modelData.game }
                    }
                }
            }
            Text {
                width: parent.width
                visible: root.captureError.length > 0
                text: root.captureError
                color: Tokens.text
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Row {
                visible: root.gameMouse
                spacing: 12
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Sensitivity  " + root.controller.mouseSensitivity.toFixed(2) + "×"
                    color: Tokens.text
                }
                Slider {
                    width: 220
                    from: 0.1; to: 4; stepSize: 0.05
                    value: root.controller.mouseSensitivity
                    onMoved: root.controller.mouseSensitivity = value
                }
            }
            CheckBox {
                text: "Share text clipboard"
                checked: root.controller.sharedClipboard
                onToggled: root.controller.sharedClipboard = checked
            }
            CheckBox {
                visible: root.gameMouse
                text: "Use English keyboard while gaming"
                checked: root.controller.englishKeyboard
                onToggled: root.controller.englishKeyboard = checked
            }
            Text {
                visible: root.gameMouse
                width: parent.width
                text: root.controller.gamepadAvailable ? "Gamepad forwards to the Windows host, including rumble."
                    : "Gamepad input is unavailable on this host."
                color: Tokens.textMuted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Text {
                width: parent.width
                text: (root.gameMouse ? "Game uses raw mouse movement; 1.00× keeps the original counts. "
                      : "Desktop maps the pointer to the streamed display. ")
                      + root.releaseShortcut + " releases all input."
                color: Tokens.textMuted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }
    }
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 20
        width: localHint.implicitWidth + 32
        height: 42
        radius: 21
        color: "#e022252b"
        visible: !root.toolbarOpen && root.gameMouse
        Text {
            id: localHint
            anchors.centerIn: parent
            text: root.releaseShortcut + " · Use this device"
            color: Tokens.text
            font.pixelSize: 14
        }
    }
}
