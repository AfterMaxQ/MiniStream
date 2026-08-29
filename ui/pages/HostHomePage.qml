import QtQuick
import QtQuick.Controls
import MiniStream

Item {
    id: root
    required property var controller

    Column {
        width: Math.min(parent.width - Tokens.space32 * 2, 560)
        anchors.centerIn: parent
        spacing: Tokens.space24

        Column {
            width: parent.width
            spacing: Tokens.space8

            Text {
                text: "MiniStream Host"
                color: Tokens.text
                font.pixelSize: 28
                font.weight: Font.DemiBold
            }
            Text {
                text: root.controller.ready ? "Ready" : "Setup required"
                color: root.controller.ready ? Tokens.success : Tokens.warning
                font.pixelSize: 14
            }
        }

        Rectangle {
            width: parent.width
            height: statusColumn.implicitHeight + Tokens.space24
            radius: Tokens.radius10
            color: Tokens.surface
            border.color: Tokens.border

            Column {
                id: statusColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: Tokens.space12

                StatusRow { width: parent.width; label: "Video"; ready: root.controller.videoReady; detail: root.controller.videoDetail }
                StatusRow { width: parent.width; label: "Audio"; ready: root.controller.audioReady; detail: root.controller.audioDetail }
                StatusRow { width: parent.width; label: "Controller"; ready: root.controller.controllerReady; detail: root.controller.controllerDetail }
                StatusRow { width: parent.width; label: "Network"; ready: root.controller.networkReady; detail: root.controller.networkDetail }
            }
        }

        Row {
            width: parent.width
            spacing: Tokens.space12

            AppButton {
                text: "Check again"
                onClicked: root.controller.refresh()
            }
            Item { width: parent.width - 236; height: 1 }
            AppButton {
                text: root.controller.hosting ? "Stop Host" : "Start Host"
                enabled: root.controller.ready
                onClicked: root.controller.hosting
                           ? root.controller.stopHost()
                           : root.controller.startHost()
            }
        }
    }
}
