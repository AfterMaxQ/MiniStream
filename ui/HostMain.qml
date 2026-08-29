import QtQuick
import QtQuick.Controls
import MiniStream

ApplicationWindow {
    id: window
    width: 760
    height: 560
    minimumWidth: 640
    minimumHeight: 480
    visible: true
    title: "MiniStream Host"
    color: Tokens.background

    Shortcut {
        sequence: "F11"
        onActivated: window.visibility = window.visibility === Window.FullScreen
                     ? Window.Windowed : Window.FullScreen
    }
    Shortcut {
        sequence: "Esc"
        enabled: window.visibility === Window.FullScreen
        onActivated: window.visibility = Window.Windowed
    }

    Loader {
        anchors.fill: parent
        sourceComponent: hostController.pairing ? pairingPage : homePage
    }

    Component {
        id: homePage
        HostHomePage { controller: hostController }
    }
    Component {
        id: pairingPage
        PairingPage { controller: hostController }
    }
}
