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
                width: parent.width
                text: "Allow control"
                color: Tokens.text
                font.pixelSize: 28
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: root.controller.deviceLabel
                color: Tokens.textMuted
                font.pixelSize: 14
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: root.controller.ready ? root.controller.broadcastStatus
                                             : root.controller.statusText
                color: root.controller.ready ? Tokens.success : Tokens.warning
                font.pixelSize: 14
                wrapMode: Text.WordWrap
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
                StatusRow { width: parent.width; label: "Input"; ready: root.controller.inputReady; detail: root.controller.inputDetail }
                StatusRow { width: parent.width; label: "Network"; ready: root.controller.networkReady; detail: root.controller.networkDetail }
            }
        }

        Row {
            width: parent.width
            spacing: Tokens.space12

            AppButton {
                id: checkButton
                text: "Check again"
                onClicked: root.controller.refresh()
                visible: !root.controller.broadcasting
            }
            Item {
                width: Math.max(0, parent.width - checkButton.width - actionButton.width - Tokens.space12)
                height: 1
            }
            AppButton {
                id: actionButton
                text: root.controller.broadcasting ? "Stop broadcast" : "Allow control"
                enabled: root.controller.broadcasting || root.controller.ready
                onClicked: root.controller.broadcasting
                           ? root.controller.stopBroadcast()
                           : root.controller.startBroadcast()
            }
        }

        AppButton {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.controller.permissionActionAvailable
            text: "Grant permissions"
            onClicked: root.controller.openPermissionSettings()
        }
    }
}
