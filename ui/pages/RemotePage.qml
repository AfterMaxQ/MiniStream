import QtQuick
import QtQuick.Controls
import MiniStream

Item {
    id: root
    required property var controller

    Timer {
        interval: 3000
        repeat: true
        triggeredOnStart: true
        running: root.visible && !root.controller.connecting
                 && !root.controller.connected && !root.controller.pairing
        onTriggered: {
            if (!root.controller.searching) root.controller.findDevices()
        }
    }

    Column {
        width: Math.min(parent.width - Tokens.space32 * 2, 620)
        anchors.centerIn: parent
        spacing: Tokens.space24

        Row {
            width: parent.width
            Text {
                width: parent.width - findButton.width
                text: "Remote control"
                color: Tokens.text
                font.pixelSize: 28
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            AppButton {
                id: findButton
                text: root.controller.connecting
                      ? "Connecting"
                      : (root.controller.searching ? "Searching" : "Find devices")
                enabled: !root.controller.searching && !root.controller.connecting
                onClicked: root.controller.findDevices()
            }
        }

        Text {
            width: parent.width
            visible: root.controller.connecting
            text: "Connecting to " + root.controller.selectedDeviceLabel
            color: Tokens.textMuted
            font.pixelSize: 13
            elide: Text.ElideRight
        }

        SectionHeader { text: "Nearby devices · refreshes automatically" }

        Rectangle {
            width: parent.width
            height: Math.min(280, Math.max(96, deviceList.contentHeight))
            radius: Tokens.radius10
            color: Tokens.surface
            border.color: Tokens.border

            ListView {
                id: deviceList
                anchors.fill: parent
                anchors.margins: Tokens.space8
                model: root.controller.hosts
                spacing: Tokens.space4
                clip: true

                delegate: Item {
                    required property int index
                    required property var modelData
                    property var cardLines: String(modelData).split("\n")
                    width: deviceList.width
                    height: Math.max(56, cardDetails.implicitHeight + Tokens.space16)

                    Row {
                        id: cardRow
                        anchors.fill: parent
                        anchors.margins: Tokens.space8
                        spacing: Tokens.space12

                        Column {
                            id: cardDetails
                            width: Math.max(0, cardRow.width - connectButton.width - cardRow.spacing)
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
                            enabled: !root.controller.connecting
                            onClicked: root.controller.connectToDevice(index)
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: deviceList.count === 0
                    text: root.controller.searching
                          ? "Searching local network"
                          : root.controller.statusText
                    color: Tokens.textMuted
                    font.pixelSize: 14
                }
            }
        }

        Row {
            width: parent.width
            spacing: Tokens.space12

            AppButton {
                id: inputButton
                text: root.controller.remoteInputActive ? "Use this device" : "Control remote"
                enabled: root.controller.connected
                onClicked: root.controller.toggleRemoteInput()
            }
            Text {
                width: parent.width - inputButton.width - Tokens.space12
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
