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
    focus: true
    title: "MiniStream"
    color: Tokens.background

    onClosing: {
        roleController.releaseRemoteInput()
        roleController.disconnect()
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
        sequence: "Ctrl+Alt+R"
        enabled: Qt.platform.os !== "osx" && !roleController.remoteInputActive
        onActivated: roleController.toggleRemoteInput()
    }
    Shortcut {
        sequence: "Meta+Alt+R"
        enabled: Qt.platform.os === "osx" && !roleController.remoteInputActive
        onActivated: roleController.toggleRemoteInput()
    }
    Shortcut {
        sequence: "Ctrl+Alt+F"
        enabled: Qt.platform.os !== "osx" && !roleController.remoteInputActive
        onActivated: window.toggleFullscreen()
    }
    Shortcut {
        sequence: "Meta+Alt+F"
        enabled: Qt.platform.os === "osx" && !roleController.remoteInputActive
        onActivated: window.toggleFullscreen()
    }
    Shortcut {
        sequence: "Esc"
        enabled: !roleController.remoteInputActive && window.visibility === Window.FullScreen
        onActivated: window.visibility = Window.Windowed
    }

    RoleModeSwitch {
        id: modeSwitch
        anchors.top: parent.top
        anchors.topMargin: Tokens.space16
        anchors.horizontalCenter: parent.horizontalCenter
        mode: roleController.mode
        controlledAvailable: roleController.controlledAvailable
        remoteAvailable: roleController.remoteAvailable
        visible: !roleController.pairing
        onModeSelected: roleController.setMode(mode)
    }

    Loader {
        anchors.fill: parent
        sourceComponent: roleController.pairing ? pairingPage
                         : roleController.connected ? streamPage
                         : roleController.mode === 1 ? controlledPage : remotePage
    }

    Component {
        id: controlledPage
        ControlledPage { controller: roleController }
    }
    Component {
        id: remotePage
        RemotePage { controller: roleController }
    }
    Component {
        id: pairingPage
        PairingPage {
            controller: roleController
            peerLabel: roleController.selectedDeviceLabel
        }
    }
    Component {
        id: streamPage
        StreamPage { controller: roleController }
    }
}
