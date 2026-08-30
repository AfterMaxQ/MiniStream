import QtQuick
import MiniStream

Item {
    id: root
    required property int mode
    property bool controlledAvailable: true
    property bool remoteAvailable: true
    signal modeSelected(int mode)

    implicitHeight: 40
    width: Math.min(parent ? Math.max(0, parent.width - Tokens.space32) : 360, 360)

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
                color: root.mode === 1 ? Tokens.accent : "transparent"

                Text {
                    anchors.fill: parent
                    text: "Allow control"
                    color: root.controlledAvailable
                           ? (root.mode === 1 ? Tokens.text : Tokens.textMuted)
                           : Tokens.border
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: root.controlledAvailable
                    onClicked: root.modeSelected(1)
                }
            }

            Rectangle {
                width: (parent.width - parent.spacing) / 2
                height: parent.height
                radius: Tokens.radius6
                color: root.mode === 2 ? Tokens.accent : "transparent"

                Text {
                    anchors.fill: parent
                    text: "Remote control"
                    color: root.remoteAvailable
                           ? (root.mode === 2 ? Tokens.text : Tokens.textMuted)
                           : Tokens.border
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: root.remoteAvailable
                    onClicked: root.modeSelected(2)
                }
            }
        }
    }
}
