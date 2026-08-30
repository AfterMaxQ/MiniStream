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
                text: root.controller.searching ? "Searching" : "Find devices"
                enabled: !root.controller.searching
                onClicked: root.controller.refreshHosts()
            }
        }

        SectionHeader { text: "Nearby devices" }

        Rectangle {
            width: parent.width
            height: Math.min(280, Math.max(96, hostList.contentHeight))
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
                    property var cardLines: String(modelData).split("\n")
                    width: hostList.width
                    height: Math.max(56, details.implicitHeight + Tokens.space16)

                    Row {
                        id: detailsRow
                        anchors.fill: parent
                        anchors.margins: Tokens.space8
                        spacing: Tokens.space12

                        Column {
                            id: details
                            width: Math.max(0, detailsRow.width - connectButton.width - detailsRow.spacing)
                            spacing: Tokens.space4

                            Text {
                                width: parent.width
                                text: cardLines.length > 0 ? cardLines[0] : ""
                                color: Tokens.text
                                font.pixelSize: 14
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: cardLines.length > 1 ? cardLines[1] : ""
                                color: Tokens.textMuted
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
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
                text: root.controller.remoteInputActive ? "Use this device" : "Control remote"
                enabled: root.controller.connected
                onClicked: root.controller.toggleRemoteInput()
            }
            Text {
                width: parent.width - remoteButton.width - Tokens.space12
                visible: root.controller.connected
                text: root.controller.remoteInputActive
                      ? "Input sent to " + root.controller.selectedDeviceLabel
                      : "Input stays on this device."
                color: Tokens.textMuted
                font.pixelSize: 13
                anchors.verticalCenter: parent.verticalCenter
                wrapMode: Text.WordWrap
                elide: Text.ElideRight
            }
        }
    }
}
