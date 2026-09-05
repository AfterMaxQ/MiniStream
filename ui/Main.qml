import QtQuick
import QtQuick.Controls
import MiniStream

ApplicationWindow {
    id: window
    width: 820
    height: 600
    minimumWidth: 680
    minimumHeight: 500
    visible: true
    title: "MiniStream"
    color: Tokens.background

    onClosing: {
        roleController.releaseRemoteInput()
        roleController.disconnect()
    }
    onActiveChanged: {
        if (!active && roleController.remoteInputActive) {
            roleController.releaseRemoteInput()
        }
    }

    function toggleFullscreen() {
        window.visibility = window.visibility === Window.FullScreen
                           ? Window.Windowed : Window.FullScreen
    }

    Shortcut {
        sequence: "F11"
        enabled: !roleController.remoteInputActive
        onActivated: window.toggleFullscreen()
    }
    Shortcut {
        sequence: "Esc"
        enabled: !roleController.remoteInputActive && window.visibility === Window.FullScreen
        onActivated: window.visibility = Window.Windowed
    }
    Shortcut {
        sequence: "Ctrl+Alt+R"
        enabled: Qt.platform.os !== "osx"
                 && roleController.mode === 2 && roleController.connected
                 && !roleController.remoteInputActive
        onActivated: roleController.toggleRemoteInput()
    }
    Shortcut {
        sequence: "Meta+Alt+R"
        enabled: Qt.platform.os === "osx"
                 && roleController.mode === 2 && roleController.connected
                 && !roleController.remoteInputActive
        onActivated: roleController.toggleRemoteInput()
    }
    Shortcut {
        sequence: "Ctrl+Alt+Shift+R"
        enabled: Qt.platform.os !== "osx"
                 && roleController.remoteInputActive
        onActivated: roleController.releaseRemoteInput()
    }
    Shortcut {
        sequence: "Meta+Alt+Shift+R"
        enabled: Qt.platform.os === "osx"
                 && roleController.remoteInputActive
        onActivated: roleController.releaseRemoteInput()
    }

    Item {
        id: modeSwitch
        anchors.top: parent.top
        anchors.topMargin: Tokens.space16
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(Math.max(0, parent.width - Tokens.space32), 360)
        height: 40
        visible: !roleController.pairing && !roleController.connected
        z: 10

        Rectangle {
            anchors.fill: parent
            radius: Tokens.radius10
            color: Tokens.surface
            border.color: Tokens.border

            Row {
                anchors.fill: parent
                anchors.margins: 3
                spacing: 3

                Rectangle {
                    width: (parent.width - parent.spacing) / 2
                    height: parent.height
                    radius: Tokens.radius6
                    color: roleController.mode === 1 ? Tokens.accent : "transparent"

                    Text {
                        anchors.fill: parent
                        text: "Allow control"
                        color: roleController.controlledAvailable
                               ? (roleController.mode === 1 ? Tokens.text : Tokens.textMuted)
                               : Tokens.border
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: roleController.controlledAvailable
                        onClicked: roleController.setMode(1)
                    }
                }

                Rectangle {
                    width: (parent.width - parent.spacing) / 2
                    height: parent.height
                    radius: Tokens.radius6
                    color: roleController.mode === 2 ? Tokens.accent : "transparent"

                    Text {
                        anchors.fill: parent
                        text: "Remote control"
                        color: roleController.remoteAvailable
                               ? (roleController.mode === 2 ? Tokens.text : Tokens.textMuted)
                               : Tokens.border
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: roleController.remoteAvailable
                        onClicked: roleController.setMode(2)
                    }
                }
            }
        }
    }

    ControlledPage {
        anchors.fill: parent
        controller: roleController
        visible: !roleController.pairing && !roleController.connected && roleController.mode === 1
    }

    RemotePage {
        anchors.fill: parent
        controller: roleController
        visible: !roleController.pairing && !roleController.connected && roleController.mode === 2
    }

    PairingPage {
        anchors.fill: parent
        controller: roleController
        peerLabel: roleController.selectedDeviceLabel
        visible: roleController.pairing
    }

    StreamPage {
        anchors.fill: parent
        controller: roleController
        appWindow: window
        visible: roleController.connected && roleController.mode === 2
    }

    ControlledActivePage {
        anchors.fill: parent
        controller: roleController
        visible: roleController.connected && roleController.mode === 1
    }

}
