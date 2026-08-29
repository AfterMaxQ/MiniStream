import QtQuick
import MiniStream

Item {
    id: root
    property string label: ""
    property bool ready: false
    property string detail: ""
    implicitHeight: 48

    Row {
        anchors.fill: parent
        spacing: Tokens.space12

        Rectangle {
            width: 8
            height: 8
            radius: 4
            anchors.verticalCenter: parent.verticalCenter
            color: root.ready ? Tokens.success : Tokens.warning
        }

        Text {
            width: 92
            anchors.verticalCenter: parent.verticalCenter
            text: root.label
            color: Tokens.text
            font.pixelSize: 14
            font.weight: Font.Medium
        }

        Text {
            width: parent.width - 124
            anchors.verticalCenter: parent.verticalCenter
            text: root.detail
            color: Tokens.textMuted
            font.pixelSize: 13
            elide: Text.ElideRight
        }
    }
}
