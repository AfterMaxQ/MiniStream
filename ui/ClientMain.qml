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

    onClosing: clientController.releaseRemoteInput()

    function toggleFullscreen() {
        window.visibility = window.visibility === Window.FullScreen
                           ? Window.Windowed : Window.FullScreen
    }

    Shortcut {
        sequence: "F11"
        enabled: !clientController.remoteInputActive
        onActivated: window.toggleFullscreen()
    }
    Shortcut {
        sequence: "Ctrl+Alt+R"
        enabled: Qt.platform.os !== "osx"
        onActivated: clientController.toggleRemoteInput()
    }
    Shortcut {
        sequence: "Meta+Alt+R"
        enabled: Qt.platform.os === "osx"
        onActivated: clientController.toggleRemoteInput()
    }
    Shortcut {
        sequence: "Ctrl+Alt+F"
        enabled: Qt.platform.os !== "osx"
        onActivated: window.toggleFullscreen()
    }
    Shortcut {
        sequence: "Meta+Alt+F"
        enabled: Qt.platform.os === "osx"
        onActivated: window.toggleFullscreen()
    }
    Shortcut {
        sequence: "Esc"
        enabled: !clientController.remoteInputActive && window.visibility === Window.FullScreen
        onActivated: window.visibility = Window.Windowed
    }

    Loader {
        anchors.fill: parent
        sourceComponent: clientController.pairing ? pairingPage : homePage
    }

    Component {
        id: homePage
        ClientHomePage { controller: clientController }
    }
    Component {
        id: pairingPage
        PairingPage { controller: clientController }
    }
}
