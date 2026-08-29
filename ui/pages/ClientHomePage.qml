import QtQuick
import QtQuick.Controls
import MiniStream

Item {
    id: root
    required property var controller

    Column {
        width: Math.min(parent.width - Tokens.space32 * 2, 620)
        anchors.centerIn: parent
        spacing: Tokens.space24

        Row {
            width: parent.width
            Text {
                width: parent.width - refreshButton.width
                text: "MiniStream"
                color: Tokens.text
                font.pixelSize: 28
                font.weight: Font.DemiBold
            }
            AppButton {
                id: refreshButton
                text: root.controller.searching ? "Searching" : "Find PCs"
                enabled: !root.controller.searching
                onClicked: root.controller.refreshHosts()
            }
        }

        SectionHeader { text: "Windows PCs" }

        Rectangle {
            width: parent.width
            height: Math.max(96, hostList.contentHeight)
            radius: Tokens.radius10
            color: Tokens.surface
            border.color: Tokens.border

            ListView {
                id: hostList
                anchors.fill: parent
                anchors.margins: Tokens.space8
                model: root.controller.hosts
                spacing: Tokens.space4
                clip: true

                delegate: Item {
                    required property int index
                    required property var modelData
                    width: hostList.width
                    height: 56

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: Tokens.space8
                        anchors.rightMargin: Tokens.space8
                        Text {
                            width: parent.width - connectButton.width - Tokens.space12
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData
                            color: Tokens.text
                            font.pixelSize: 14
                        }
                        AppButton {
                            id: connectButton
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Connect"
                            onClicked: root.controller.connectToHost(index)
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: hostList.count === 0
                    text: root.controller.searching ? "Searching local network" : "No PCs found"
                    color: Tokens.textMuted
                    font.pixelSize: 14
                }
            }
        }

        Row {
            width: parent.width
            spacing: Tokens.space12

            AppButton {
                id: remoteButton
                text: root.controller.remoteInputActive ? "Use this Mac" : "Control remote"
                enabled: root.controller.connected
                onClicked: root.controller.toggleRemoteInput()
            }
            Text {
                width: parent.width - remoteButton.width - Tokens.space12
                visible: root.controller.connected
                text: root.controller.remoteInputActive
                      ? "Keyboard, mouse, and controller are routed to the PC."
                      : "Input stays on this Mac."
                color: Tokens.textMuted
                font.pixelSize: 13
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
            }
        }
    }
}
