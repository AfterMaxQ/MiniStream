import QtQuick
import QtQuick.Controls
import MiniStream

Item {
    id: root
    required property var controller
    required property var appWindow
    property bool toolbarOpen: true
    property bool gameMouse: false
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
        onCaptureFailed: { root.gameMouse = false; root.showControls() }
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
        cursorShape: relativeMouse.active ? Qt.BlankCursor : Qt.ArrowCursor
        function positionRemote(x, y) {
            if (root.gameMouse || !root.routing) return
            const fw = Math.min(width, height * nativeVideo.aspectRatio)
            const fh = fw / nativeVideo.aspectRatio
            const left = (width - fw) / 2
            const top = (height - fh) / 2
            if (fw <= 0 || fh <= 0 || x < left || x > left + fw || y < top || y > top + fh) return
            root.controller.routeMousePosition(Math.round((x - left) / fw * 65535),
                                               Math.round((y - top) / fh * 65535))
        }
        onPositionChanged: function(mouse) { positionRemote(mouse.x, mouse.y) }
        onPressed: function(mouse) {
            root.forceActiveFocus()
            positionRemote(mouse.x, mouse.y)
            root.controller.routeMouseButton(mouse.button, true)
        }
        onReleased: function(mouse) { root.controller.routeMouseButton(mouse.button, false) }
        onCanceled: root.controller.releaseRemoteInput()
        onWheel: function(wheel) { root.controller.routeMouseWheel(wheel.angleDelta.y) }
    }

    Item {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width, 560)
        height: 18
        visible: root.visible && !relativeMouse.active
        HoverHandler { id: topHover }
        Rectangle {
            anchors.top: parent.top
            anchors.topMargin: 4
            anchors.horizontalCenter: parent.horizontalCenter
            width: 48; height: 3; radius: 2
            color: Tokens.text
            opacity: root.toolbarOpen ? 0 : (topHover.hovered ? 0.8 : 0.22)
            Behavior on opacity { NumberAnimation { duration: 140 } }
        }
    }
    Timer {
        interval: 600
        running: topHover.hovered && !root.toolbarOpen && remoteMouse.pressedButtons === Qt.NoButton
        onTriggered: root.showControls()
    }
    Timer {
        interval: 1800
        running: root.visible && root.toolbarOpen && !toolbarHover.hovered && !topHover.hovered
        onTriggered: root.toolbarOpen = false
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
        HoverHandler { id: toolbarHover }
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
                    text: "Resume control"
                    onClicked: root.resumeControl()
                }
                AppButton {
                    text: root.gameMouse ? "Mouse: Game" : "Mouse: Desktop"
                    onClicked: { root.controller.releaseRemoteInput(); root.gameMouse = !root.gameMouse }
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
            Text {
                width: parent.width
                text: (root.gameMouse ? "Game mouse locks the cursor. " : "Hover at the top for controls. ")
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
        visible: !root.toolbarOpen && !root.controller.remoteInputActive
        Text {
            id: localHint
            anchors.centerIn: parent
            text: "Input is local · Click to control remote"
            color: Tokens.text
            font.pixelSize: 14
        }
        MouseArea { anchors.fill: parent; onClicked: root.resumeControl() }
    }
}
