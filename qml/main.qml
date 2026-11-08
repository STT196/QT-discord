import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ApplicationWindow {
    id: root
    visible: true
    width: 1100
    height: 700
    title: "QtDiscord"

    property var client: discordClient
    property string statusText: client.status

    color: "#2b2d31"

    readonly property string channelName: {
        for (var i = 0; i < client.channels.length; ++i) {
            if (client.channels[i].id === client.currentChannelId)
                return client.channels[i].name;
        }
        return "";
    }

    Rectangle {
        anchors.fill: parent
        color: "#2b2d31"

        // Top bar
        Rectangle {
            id: topBar
            width: parent.width
            height: 56
            color: "#2f3136"

            Row {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 12

                Rectangle {
                    width: 48; height: 48
                    color: "#7289da"
                    radius: 24

                    Text {
                        anchors.centerIn: parent
                        text: client.user.username ? client.user.username[0].toUpperCase() : "Q"
                        color: "white"
                        font.bold: true
                        font.pixelSize: 24
                    }
                }

                Text {
                    text: root.statusText
                    color: "#dcddde"
                    font.pixelSize: 16
                }
            }
        }

        // Main split
        Row {
            anchors.top: topBar.bottom
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 0

            // Guild sidebar (narrow)
            Rectangle {
                width: 64
                color: "#2f3136"

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: client.guilds
                    delegate: Rectangle {
                        width: 64; height: 64
                        color: client.currentGuildId === modelData.id ? "#36393f" : "transparent"
                        radius: 16

                        Text {
                            anchors.centerIn: parent
                            text: modelData.name ? modelData.name[0].toUpperCase() : "?"
                            color: "#dcddde"
                            font.pixelSize: 20
                            font.bold: true
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: client.selectGuild(modelData.id)
                        }
                    }
                }
            }

            // Channel list
            Rectangle {
                width: 240
                color: "#2f3136"

                Column {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0

                    Text {
                        Layout.leftMargin: 16
                        Layout.topMargin: 12
                        text: "Text Channels"
                        color: "#8e9297"
                        font.pixelSize: 12
                        font.bold: true
                    }

                    ListView {
                        id: channelView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: client.channels
                        currentIndex: 0
                        delegate: Rectangle {
                            width: 240; height: 36
                            color: client.currentChannelId === modelData.id ? "#36393f" : "transparent"

                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 8

                                Text {
                                    text: "#"
                                    color: "#72767d"
                                    font.pixelSize: 14
                                }

                                Text {
                                    text: modelData.name
                                    color: "#dcddde"
                                    font.pixelSize: 14
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: client.selectChannel(modelData.id)
                            }
                        }
                    }
                }
            }

            // Message area
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#36393f"

                Column {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0

                    // Header
                    Rectangle {
                        Layout.fillWidth: true
                        height: 56
                        color: "#2f3136"

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 12

                            Text {
                                text: "# " + root.channelName
                                color: "#dcddde"
                                font.pixelSize: 18
                                font.bold: true
                            }

                            Button {
                                text: "Load older"
                                enabled: client.activeMessages.length > 0
                                onClicked: client.loadMoreMessages()
                            }
                        }
                    }

                    // Messages
                    ListView {
                        id: messageView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.bottomMargin: 56
                        model: client.activeMessages
                        delegate: Rectangle {
                            width: parent.width
                            height: 60
                            color: "transparent"

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 60
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                spacing: 2

                                Row {
                                    spacing: 4

                                    Text {
                                        text: modelData.author ? modelData.author.username : "?"
                                        color: "#7289da"
                                        font.pixelSize: 13
                                        font.bold: true
                                    }

                                    Text {
                                        text: modelData.timestamp ? modelData.timestamp : ""
                                        color: "#8e9297"
                                        font.pixelSize: 11
                                    }
                                }

                                Text {
                                    text: modelData.content
                                    color: "#dcddde"
                                    font.pixelSize: 14
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                }

                // Send bar
                Rectangle {
                    Layout.fillWidth: true
                    height: 56
                    color: "#40444b"

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10

                        TextField {
                            id: messageInput
                            width: parent.width - 100
                            height: 40
                            placeholderText: "Message @QtDiscord..."
                            color: "white"
                            font.pixelSize: 14

                            onAccepted: send()
                        }

                        Button {
                            width: 80
                            height: 40
                            text: "Send"
                            enabled: messageInput.text.length > 0 && messageInput.text.trim().length > 0
                            onClicked: send()
                        }
                    }
                }
            }
        }
    }

    function send() {
        if (!messageInput || messageInput.text.trim().length === 0)
            return;
        client.sendMessage(client.currentChannelId, messageInput.text);
        messageInput.text = "";
    }

    Connections {
        target: client
        function onErrorOccurred(msg) {
            root.statusText = "Error: " + msg;
        }
    }
}
