import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    visible: true
    width: 1180
    height: 760
    title: "QtDiscord"

    property var client: discordClient
    property bool dmsLoaded: false
    readonly property bool loggedIn: !!client.user && !!client.user.id

    // Discord dark palette
    readonly property color cRail: "#1e1f22"
    readonly property color cSidebar: "#2b2d31"
    readonly property color cMain: "#313338"
    readonly property color cHeader: "#313338"
    readonly property color cInput: "#383a40"
    readonly property color cText: "#dbdee1"
    readonly property color cMuted: "#949ba4"
    readonly property color cAccent: "#5865f2"
    readonly property color cHover: "#35373c"
    readonly property color cActive: "#404249"

    function avatar(a) {
        if (!a || !a["avatar"])
            return "https://cdn.discordapp.com/embed/avatars/0.png";
        return "https://cdn.discordapp.com/avatars/" + a["id"] + "/" + a["avatar"] + ".png?size=80";
    }

    function fmtTime(ts) {
        if (!ts) return "";
        var d = new Date(ts);
        if (isNaN(d.getTime())) return "";
        return Qt.formatTime(d, "hh:mm");
    }

    function guildName() {
        for (var i = 0; i < client.guilds.length; ++i)
            if (client.guilds[i].id === client.currentGuildId)
                return client.guilds[i].name;
        return "Direct Messages";
    }

    function channelName() {
        for (var i = 0; i < client.channels.length; ++i)
            if (client.channels[i].id === client.currentChannelId)
                return client.channels[i].name;
        for (var j = 0; j < client.dmChannels.length; ++j)
            if (client.dmChannels[j].id === client.currentChannelId)
                return client.dmChannels[j].name;
        return "";
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0
        visible: root.loggedIn

        // ---- Guild rail ----
        Rectangle {
            Layout.preferredWidth: 72
            Layout.fillHeight: true
            color: root.cRail

            ColumnLayout {
                anchors.fill: parent
                anchors.topMargin: 8
                spacing: 6

                Item {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 48
                    Rectangle {
                        anchors.fill: parent
                        radius: 16
                        color: root.cAccent
                        Text { anchors.centerIn: parent; text: "@"; color: "white"; font.pixelSize: 22; font.bold: true }
                    }
                    MouseArea { anchors.fill: parent; onClicked: client.refreshDms() }
                }

                Rectangle { Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 32; Layout.preferredHeight: 2; color: root.cHover }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 6
                    clip: true
                    model: client.guilds
                    delegate: Item {
                        width: 72; height: 52
                        Rectangle {
                            anchors.centerIn: parent
                            width: 48; height: 48
                            radius: client.currentGuildId === modelData.id ? 14 : 24
                            color: client.currentGuildId === modelData.id ? root.cAccent : root.cHover
                            Text {
                                anchors.centerIn: parent
                                text: modelData.name ? modelData.name[0].toUpperCase() : "?"
                                color: "white"; font.pixelSize: 18; font.bold: true
                            }
                        }
                        MouseArea { anchors.fill: parent; onClicked: client.selectGuild(modelData.id) }
                    }
                }
            }
        }

        // ---- Channel sidebar ----
        Rectangle {
            Layout.preferredWidth: 240
            Layout.fillHeight: true
            color: root.cSidebar

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    color: "transparent"
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.guildName()
                        color: root.cText
                        font.pixelSize: 15
                        font.bold: true
                        elide: Text.ElideRight
                        width: parent.width - 32
                    }
                }

                Text {
                    Layout.leftMargin: 16
                    Layout.topMargin: 8
                    text: "TEXT CHANNELS"
                    color: root.cMuted
                    font.pixelSize: 11
                    font.bold: true
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: client.channels
                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 32
                        radius: 4
                        color: client.currentChannelId === modelData.id ? root.cActive : (chHover.containsMouse ? root.cHover : "transparent")
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            spacing: 6
                            Text { text: "#"; color: root.cMuted; font.pixelSize: 16 }
                            Text { text: modelData.name; color: root.cText; font.pixelSize: 14; elide: Text.ElideRight; Layout.fillWidth: true }
                        }
                        MouseArea { id: chHover; anchors.fill: parent; hoverEnabled: true; onClicked: client.selectChannel(modelData.id) }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: root.cHover
                    Layout.topMargin: 6
                }

                RowLayout {
                    Layout.leftMargin: 16
                    Layout.rightMargin: 8
                    Layout.topMargin: 6
                    Layout.fillWidth: true
                    Text { text: "DIRECT MESSAGES"; color: root.cMuted; font.pixelSize: 11; font.bold: true; Layout.fillWidth: true }
                    Button { text: "+"; implicitWidth: 24; implicitHeight: 24; onClicked: client.refreshDms() }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(contentHeight, 160)
                    clip: true
                    model: client.dmChannels
                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 30
                        radius: 4
                        color: client.currentChannelId === modelData.id ? root.cActive : (dmHover.containsMouse ? root.cHover : "transparent")
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            spacing: 6
                            Text { text: "@"; color: root.cMuted; font.pixelSize: 15 }
                            Text { text: modelData.name; color: root.cText; font.pixelSize: 13; elide: Text.ElideRight; Layout.fillWidth: true }
                        }
                        MouseArea { id: dmHover; anchors.fill: parent; hoverEnabled: true; onClicked: client.selectChannel(modelData.id) }
                    }
                }
            }
        }

        // ---- Main area ----
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: root.cMain

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Header
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    color: root.cHeader
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        Text {
                            text: "# " + root.channelName()
                            color: root.cText
                            font.pixelSize: 16
                            font.bold: true
                            Layout.fillWidth: true
                        }
                        Text { text: client.status; color: root.cMuted; font.pixelSize: 12; Layout.rightMargin: 16 }
                    }
                }

                // Messages
                ListView {
                    id: messageView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: client.activeMessages
                    spacing: 2
                    delegate: Rectangle {
                        width: messageView.width
                        height: msgLayout.implicitHeight + 12
                        color: "transparent"

                        RowLayout {
                            id: msgLayout
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            anchors.topMargin: 6
                            spacing: 12

                            Image {
                                Layout.preferredWidth: 40
                                Layout.preferredHeight: 40
                                Layout.alignment: Qt.AlignTop
                                source: root.avatar(modelData.author)
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                RowLayout {
                                    spacing: 8
                                    Text {
                                        text: modelData.author ? (modelData.author.global_name || modelData.author.username) : "?"
                                        color: "#e3e5e8"
                                        font.pixelSize: 14
                                        font.bold: true
                                    }
                                    Text { text: root.fmtTime(modelData.timestamp); color: root.cMuted; font.pixelSize: 11 }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    textFormat: Text.RichText
                                    text: client.markdownToHtml(modelData.content || "")
                                    color: root.cText
                                    font.pixelSize: 14
                                    wrapMode: Text.Wrap
                                    onLinkActivated: (link) => Qt.openUrlExternally(link)
                                }

                                // Embeds
                                Repeater {
                                    model: modelData.embeds ? modelData.embeds : []
                                    delegate: Rectangle {
                                        Layout.fillWidth: true
                                        Layout.topMargin: 4
                                        implicitHeight: embedCol.implicitHeight + 16
                                        color: "#2b2d31"
                                        radius: 4
                                        Rectangle { width: 4; height: parent.height; color: "#5865f2"; radius: 2 }
                                        ColumnLayout {
                                            id: embedCol
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            anchors.leftMargin: 14
                                            anchors.topMargin: 8
                                            spacing: 3
                                            Text { visible: modelData.title !== undefined && modelData.title !== ""; text: modelData.title || ""; color: "#e3e5e8"; font.bold: true; font.pixelSize: 13; Layout.fillWidth: true; wrapMode: Text.Wrap }
                                            Text { visible: modelData.description !== undefined && modelData.description !== ""; text: modelData.description || ""; color: root.cText; font.pixelSize: 13; Layout.fillWidth: true; wrapMode: Text.Wrap }
                                        }
                                    }
                                }

                                // Attachments
                                Repeater {
                                    model: modelData.attachments ? modelData.attachments : []
                                    delegate: Item {
                                        Layout.fillWidth: true
                                        Layout.topMargin: 4
                                        implicitHeight: {
                                            var ct = modelData.content_type || "";
                                            return ct.indexOf("image") === 0 ? 220 : 24;
                                        }
                                        Image {
                                            visible: (modelData.content_type || "").indexOf("image") === 0
                                            source: modelData.url || ""
                                            width: Math.min(parent.width, 320); height: 220
                                            fillMode: Image.PreserveAspectFit
                                        }
                                        Text {
                                            visible: (modelData.content_type || "").indexOf("image") !== 0
                                            text: "\uD83D\uDCCE " + (modelData.filename || "file")
                                            color: "#00a8fc"
                                            font.pixelSize: 13
                                        }
                                    }
                                }

                                // Reactions
                                RowLayout {
                                    visible: modelData.reactions !== undefined && modelData.reactions.length > 0
                                    spacing: 4
                                    Repeater {
                                        model: modelData.reactions ? modelData.reactions : []
                                        delegate: Rectangle {
                                            implicitWidth: rl.implicitWidth + 14
                                            implicitHeight: 24
                                            radius: 8
                                            color: modelData.me ? "#3b3d44" : "#2b2d31"
                                            border.color: modelData.me ? root.cAccent : "transparent"
                                            RowLayout {
                                                id: rl
                                                anchors.centerIn: parent
                                                spacing: 4
                                                Text { text: modelData.emoji ? (modelData.emoji.name || "?") : "?"; font.pixelSize: 13 }
                                                Text { text: modelData.count; color: root.cText; font.pixelSize: 12 }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Typing
                Text {
                    Layout.leftMargin: 16
                    Layout.bottomMargin: 2
                    text: client.typingText
                    color: root.cMuted
                    font.pixelSize: 12
                    font.italic: true
                    visible: client.typingText.length > 0
                }

                // Composer
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 56
                    color: root.cMain
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 8
                        radius: 6
                        color: root.cInput
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 6
                            spacing: 6
                            Button { text: "\uD83D\uDCCE"; implicitWidth: 32; implicitHeight: 32; onClicked: fileDialog.open() }
                            TextField {
                                id: messageInput
                                Layout.fillWidth: true
                                placeholderText: "Message #" + root.channelName()
                                color: root.cText
                                background: Item {}
                                onTextChanged: if (client.currentChannelId.length > 0 && text.length > 0) client.triggerTyping(client.currentChannelId)
                                onAccepted: root.send()
                            }
                            Button { text: "Send"; implicitHeight: 32; enabled: messageInput.text.length > 0; onClicked: root.send() }
                        }
                    }
                }
            }
        }
    }

    // ---- Login page ----
    Rectangle {
        anchors.fill: parent
        visible: !root.loggedIn
        color: root.cRail

        Rectangle {
            width: 440
            height: loginCol.implicitHeight + 64
            anchors.centerIn: parent
            radius: 10
            color: root.cSidebar

            ColumnLayout {
                id: loginCol
                anchors.fill: parent
                anchors.margins: 32
                spacing: 14

                Text { text: "Welcome back!"; color: root.cText; font.pixelSize: 24; font.bold: true; Layout.alignment: Qt.AlignHCenter }
                Text { text: "Log in with your Discord token"; color: root.cMuted; font.pixelSize: 13; Layout.alignment: Qt.AlignHCenter }

                TextField {
                    id: tokenInput
                    Layout.fillWidth: true
                    placeholderText: "Token"
                    echoMode: TextInput.Password
                    color: root.cText
                    onAccepted: root.doLogin()
                }

                CheckBox {
                    id: userTokenCheck
                    text: "Use user token (self-bot — may violate ToS)"
                }

                Button {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    text: "Log In"
                    enabled: tokenInput.text.length > 0
                    onClicked: root.doLogin()
                }

                Text {
                    Layout.fillWidth: true
                    text: client.status
                    color: root.cMuted
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    FileDialog {
        id: fileDialog
        title: "Upload a file"
        onAccepted: {
            var p = fileDialog.selectedFile.toString().replace(/^file:\/\/\//, "").replace(/\//g, "\\");
            client.sendFile(client.currentChannelId, p, messageInput.text);
            messageInput.text = "";
        }
    }

    function doLogin() {
        if (tokenInput.text.trim().length === 0)
            return;
        client.login(tokenInput.text, !userTokenCheck.checked);
    }

    function send() {
        if (messageInput.text.trim().length === 0)
            return;
        client.sendMessage(client.currentChannelId, messageInput.text);
        messageInput.text = "";
    }

    Connections {
        target: client
        function onStatusChanged() {
            if (client.status === "Connected." && !root.dmsLoaded) {
                root.dmsLoaded = true;
                client.refreshDms();
            }
        }
    }
}