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

    Shortcut {
        sequence: "F11"
        onActivated: window.visibility = window.visibility === Window.FullScreen
                     ? Window.Windowed : Window.FullScreen
    }
    Shortcut {
        sequence: "Ctrl+Shift+F12"
        onActivated: clientController.toggleRemoteInput()
    }
    Shortcut {
        sequence: "Esc"
        onActivated: {
            clientController.releaseRemoteInput()
            if (window.visibility === Window.FullScreen) {
                window.visibility = Window.Windowed
            }
        }
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
