import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs
import QtMultimedia
import QtCore

ApplicationWindow {
    id: root
    visible: true
    width: 1280
    height: 820
    color: root.cBase
    title: root.homeView ? "Friends" : "QtDiscord"

    palette.window: root.cBase
    palette.windowText: root.cText
    palette.base: root.cSidebar
    palette.alternateBase: root.cHover
    palette.text: root.cText
    palette.button: root.cHover
    palette.buttonText: root.cText
    palette.brightText: "#ffffff"
    palette.highlight: root.cAccent
    palette.highlightedText: "#ffffff"
    palette.placeholderText: root.cMuted

    property var client: discordClient
    property bool dmsLoaded: false
    readonly property bool loggedIn: !!client.user && !!client.user.id
    property bool showMembers: true
    property bool homeView: true        // Friends/DM home (landing)
    property bool guildView: false      // guild channel context
    property bool showActiveCard: true  // dismissible "Active Now" promo
    property string dmSearch: ""
    property string msgSearch: ""
    property bool showPins: false
    property int voiceSeconds: 0
    property string voiceMenuUserId: ""
    property string incomingCallId: ""
    MediaDevices { id: mediaDevices }

    readonly property var memberList: {
        var arr = [];
        for (var i = 0; i < client.members.length; ++i) {
            var m = client.members[i];
            var c = {};
            for (var k in m) c[k] = m[k];
            if (m.voice_channel && ("" + m.voice_channel).length > 0)
                c._group = "IN VOICE";
            else if (m.status === "offline" || !m.status)
                c._group = "OFFLINE";
            else
                c._group = "ONLINE";
            arr.push(c);
        }
        var grank = { "IN VOICE": 0, ONLINE: 1, OFFLINE: 2 };
        arr.sort(function(a, b) {
            var ga = grank[a._group], gb = grank[b._group];
            if (ga !== gb) return ga - gb;
            var na = ((a.nick || a.global_name || a.username || "") + "").toLowerCase();
            var nb = ((b.nick || b.global_name || b.username || "") + "").toLowerCase();
            return na < nb ? -1 : (na > nb ? 1 : 0);
        });
        return arr;
    }

    function voiceUsers(channelId) {
        var out = [];
        if (!client.voiceParticipants) return out;
        for (var i = 0; i < client.voiceParticipants.length; ++i) {
            var p = client.voiceParticipants[i];
            if (p && p.channelId === channelId) out.push(p);
        }
        return out;
    }
    function voiceChannelName() {
        for (var i = 0; i < client.channels.length; ++i)
            if (client.channels[i].id === client.voiceChannelId)
                return client.channels[i].name;
        for (var i = 0; i < client.dmChannels.length; ++i)
            if (client.dmChannels[i].id === client.voiceChannelId)
                return client.dmChannels[i].name;
        return "";
    }
    function voiceGuildName() {
        for (var i = 0; i < client.guilds.length; ++i)
            if (client.guilds[i].id === client.voiceGuildId)
                return client.guilds[i].name;
        return "";
    }
    function dmChannelName(channelId) {
        for (var i = 0; i < client.dmChannels.length; ++i)
            if (client.dmChannels[i].id === channelId)
                return client.dmChannels[i].name;
        return "";
    }
    function fmtElapsed(s) {
        var m = Math.floor(s / 60);
        var ss = s % 60;
        return m + ":" + (ss < 10 ? "0" : "") + ss;
    }
    function inputDeviceNames() {
        var a = ["Default"];
        var d = mediaDevices.audioInputs;
        for (var i = 0; i < d.length; ++i) a.push(d[i].description);
        return a;
    }
    function outputDeviceNames() {
        var a = ["Default"];
        var d = mediaDevices.audioOutputs;
        for (var i = 0; i < d.length; ++i) a.push(d[i].description);
        return a;
    }

    function statusColor(s) {
        if (s === "online") return light ? "#248046" : "#3d9e60";
        if (s === "idle") return light ? "#c27c0e" : "#ffcb6e";
        if (s === "dnd") return light ? "#da373c" : "#dc4247";
        return light ? "#80848e" : "#9d9ea5";
    }

    // DARK tokens extracted from the installed Discord client (docs/discord-theme-dark.json).
    // rgba() alpha is written as #AARRGGBB so it composites over the layer below.
    readonly property bool light: client.theme === "light"
    readonly property color cBase: light ? "#f2f3f5" : "#121214"       // --background-base-lowest
    readonly property color cRail: light ? "#e3e5e8" : "#1a1a1e"       // --panel-bg
    readonly property color cSidebar: light ? "#f2f3f5" : "#202024"    // --background-base-low
    readonly property color cMain: light ? "#ffffff" : "#31323b"       // --chat-background
    readonly property color cHeader: light ? "#ffffff" : "#31323b"
    readonly property color cInput: light ? "#ebedef" : "#383a43"      // --channeltextarea-background
    readonly property color cText: light ? "#2e3338" : "#efeff1"       // --text-default
    readonly property color cTextStrong: light ? "#060607" : "#fbfbfb" // --text-strong
    readonly property color cMuted: light ? "#5c5e66" : "#96979e"      // --text-muted
    readonly property color cAccent: "#5865f2"                         // --background-brand
    readonly property color cHover: light ? "#e3e5e8" : "#1F97979F"    // --interactive-background-hover
    readonly property color cActive: light ? "#d4d7dc" : "#3397979F"   // --interactive-background-selected
    readonly property color cBorder: light ? "#e3e5e8" : "#1F97979F"   // --border-subtle
    readonly property color cMsgHover: light ? "#f5f6f7" : "#1F97979F" // --message-background-hover
    readonly property color cMentionBg: light ? "#e8eafd" : "#3D5865F2" // --mention-background
    readonly property color cEmbed: light ? "#f2f3f5" : "#242429"      // --embed-background
    readonly property color cCard: light ? "#ffffff" : "#242429"       // --card-background-default
    readonly property color cLink: light ? "#006ce7" : "#4d96ee"       // --text-link
    readonly property color cUserPanel: light ? "#e3e5e8" : "#121214"

    function avatar(a) {
        if (!a || !a["avatar"])
            return "https://cdn.discordapp.com/embed/avatars/0.png";
        return "https://cdn.discordapp.com/avatars/" + a["id"] + "/" + a["avatar"] + ".png?size=80";
    }

    function guildIcon(g) {
        if (!g || !g["icon"])
            return "";
        return "https://cdn.discordapp.com/icons/" + g["id"] + "/" + g["icon"] + ".png?size=96";
    }

    function isImageAttachment(att) {
        if (!att)
            return false;
        var ct = att["content_type"] || "";
        if (ct.indexOf("image") === 0)
            return true;
        var fn = (att["filename"] || "").toLowerCase();
        return /\.(png|jpe?g|gif|webp|bmp)$/.test(fn);
    }

    function attachmentUrl(att) {
        // Prefer the original CDN URL: media.discordapp.net transcodes to WebP,
        // which this Qt build has no image plugin for ("Unsupported image format").
        return att["url"] || att["proxy_url"] || "";
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

    function currentIsDm() {
        for (var j = 0; j < client.dmChannels.length; ++j)
            if (client.dmChannels[j].id === client.currentChannelId)
                return true;
        return false;
    }

    function dmAvatar(d) {
        var r = (d && d.recipients) ? d.recipients : [];
        if (r.length > 0) return avatar(r[0]);
        return "https://cdn.discordapp.com/embed/avatars/0.png";
    }

    function dmSubtitle(d) {
        if (!d) return "";
        if (d.type === 3) {
            var n = d.recipients ? d.recipients.length : 0;
            return n + " Members";
        }
        return "Direct Message";
    }

    function myName() {
        var u = client.user || {};
        return u.global_name || u.username || "You";
    }

    readonly property var visibleDms: {
        var q = dmSearch.toLowerCase();
        var out = [];
        for (var i = 0; i < client.dmChannels.length; ++i) {
            var d = client.dmChannels[i];
            if (q.length === 0 || ((d.name || "") + "").toLowerCase().indexOf(q) >= 0)
                out.push(d);
        }
        return out;
    }

    // ---- Logged-in shell ----
    RowLayout {
        id: shell
        anchors.fill: parent
        spacing: 0
        visible: root.loggedIn

        // ---- Left column: rail + sidebar + user panel ----
        ColumnLayout {
            id: leftColumn
            Layout.preferredWidth: 72 + 240
            Layout.minimumWidth: 72 + 240
            Layout.maximumWidth: 72 + 240
            Layout.fillWidth: false
            Layout.fillHeight: true
            spacing: 0

            RowLayout {
                id: innerRow
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // ---- Guild rail ----
                Rectangle {
                    id: railPanel
                    Layout.preferredWidth: 72
                    Layout.fillHeight: true
                    color: root.cRail

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.topMargin: 8
                        spacing: 6

                        // Home / Friends
                        Item {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: 48
                            Layout.preferredHeight: 48
                            Rectangle {
                                anchors.fill: parent
                                radius: root.homeView ? 14 : 24
                                color: root.homeView ? root.cAccent : root.cHover
                                Text {
                                    anchors.centerIn: parent
                                    text: "\u2302"
                                    color: root.homeView ? "#ffffff" : root.cText
                                    font.pixelSize: 22
                                    font.bold: true
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: { root.homeView = true; root.guildView = false; }
                            }
                        }

                        Rectangle { Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 32; Layout.preferredHeight: 2; color: root.cBorder }

                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 6
                            clip: true
                            model: client.guilds
                            delegate: Item {
                                width: 72; height: 52
                                Rectangle {
                                    visible: root.guildView && client.currentGuildId === modelData.id
                                    width: 4; height: 32; radius: 2
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: root.cTextStrong
                                }
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 48; height: 48
                                    radius: (root.guildView && client.currentGuildId === modelData.id) ? 14 : 24
                                    color: (root.guildView && client.currentGuildId === modelData.id) ? root.cAccent : root.cHover
                                    clip: true
                                    Image {
                                        anchors.fill: parent
                                        visible: modelData.icon && modelData.icon.length > 0
                                        source: root.guildIcon(modelData)
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                    }
                                    Text {
                                        anchors.centerIn: parent
                                        visible: !(modelData.icon && modelData.icon.length > 0)
                                        text: modelData.name ? modelData.name[0].toUpperCase() : "?"
                                        color: "white"; font.pixelSize: 18; font.bold: true
                                    }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: { root.homeView = false; root.guildView = true; client.selectGuild(modelData.id); }
                                }
                            }
                        }

                        // Add server
                        Item {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: 48
                            Layout.preferredHeight: 48
                            Rectangle {
                                anchors.fill: parent
                                radius: 24
                                color: addHover.containsMouse ? root.cAccent : root.cHover
                                Text {
                                    anchors.centerIn: parent
                                    text: "+"
                                    color: addHover.containsMouse ? "#ffffff" : (root.light ? "#248046" : "#3d9e60")
                                    font.pixelSize: 26
                                }
                            }
                            MouseArea { id: addHover; anchors.fill: parent; hoverEnabled: true; onClicked: client.refreshDms() }
                        }

                        Item { Layout.preferredHeight: 4 }
                    }
                }

                // ---- Sidebar (DM home or guild channels) ----
                Rectangle {
                    id: sidebarPanel
                    Layout.preferredWidth: 240
                    Layout.fillHeight: true
                    color: root.cSidebar

                    // ===== DM sidebar (home) =====
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0
                        visible: !root.guildView

                        // Find or start a conversation
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.leftMargin: 10
                            Layout.rightMargin: 10
                            Layout.topMargin: 10
                            Layout.preferredHeight: 32
                            radius: 4
                            color: root.cBase
                            TextInput {
                                id: dmSearchInput
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                verticalAlignment: TextInput.AlignVCenter
                                color: root.cText
                                font.pixelSize: 13
                                clip: true
                                onTextChanged: root.dmSearch = text
                            }
                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                verticalAlignment: Text.AlignVCenter
                                text: "Find or start a conversation"
                                color: root.cMuted
                                font.pixelSize: 13
                                visible: dmSearchInput.text.length === 0
                            }
                        }

                        // Primary nav
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 8
                            spacing: 1

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                Layout.leftMargin: 10
                                Layout.rightMargin: 10
                                radius: 4
                                color: mrHover.containsMouse ? root.cHover : "transparent"
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    spacing: 12
                                    Text { text: "\u2709"; color: root.cMuted; font.pixelSize: 15 }
                                    Text { text: "Message Requests"; color: root.cText; font.pixelSize: 14; Layout.fillWidth: true }
                                }
                                MouseArea { id: mrHover; anchors.fill: parent; hoverEnabled: true }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                Layout.leftMargin: 10
                                Layout.rightMargin: 10
                                radius: 4
                                color: nitroHover.containsMouse ? root.cHover : "transparent"
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    spacing: 12
                                    Text { text: "\u25C6"; color: root.cMuted; font.pixelSize: 15 }
                                    Text { text: "Nitro"; color: root.cText; font.pixelSize: 14; Layout.fillWidth: true }
                                }
                                MouseArea { id: nitroHover; anchors.fill: parent; hoverEnabled: true }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                Layout.leftMargin: 10
                                Layout.rightMargin: 10
                                radius: 4
                                color: shopHover.containsMouse ? root.cHover : "transparent"
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    spacing: 12
                                    Text { text: "\u25A3"; color: root.cMuted; font.pixelSize: 15 }
                                    Text { text: "Shop"; color: root.cText; font.pixelSize: 14; Layout.fillWidth: true }
                                    Rectangle {
                                        Layout.preferredWidth: promoText.implicitWidth + 10
                                        Layout.preferredHeight: 16
                                        radius: 3
                                        color: root.cHover
                                        Text { id: promoText; anchors.centerIn: parent; text: "PROMO"; color: root.cMuted; font.pixelSize: 9; font.bold: true }
                                    }
                                }
                                MouseArea { id: shopHover; anchors.fill: parent; hoverEnabled: true }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                Layout.leftMargin: 10
                                Layout.rightMargin: 10
                                radius: 4
                                color: questsHover.containsMouse ? root.cHover : "transparent"
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    spacing: 12
                                    Text { text: "\u25CE"; color: root.cMuted; font.pixelSize: 15 }
                                    Text { text: "Quests"; color: root.cText; font.pixelSize: 14; Layout.fillWidth: true }
                                }
                                MouseArea { id: questsHover; anchors.fill: parent; hoverEnabled: true }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            Layout.leftMargin: 10
                            Layout.rightMargin: 10
                            Layout.topMargin: 8
                            color: root.cBorder
                        }

                        // Direct Messages header
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 16
                            Layout.rightMargin: 10
                            Layout.topMargin: 10
                            Layout.bottomMargin: 4
                            spacing: 4
                            Text {
                                text: "DIRECT MESSAGES"
                                color: root.cMuted
                                font.pixelSize: 11
                                font.bold: true
                                Layout.fillWidth: true
                            }
                            Rectangle {
                                Layout.preferredWidth: 18
                                Layout.preferredHeight: 18
                                radius: 3
                                color: dmPlusHover.containsMouse ? root.cHover : "transparent"
                                Text { anchors.centerIn: parent; text: "+"; color: root.cMuted; font.pixelSize: 16 }
                                MouseArea { id: dmPlusHover; anchors.fill: parent; hoverEnabled: true; onClicked: client.refreshDms() }
                            }
                        }

                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.leftMargin: 8
                            Layout.rightMargin: 8
                            clip: true
                            spacing: 2
                            model: root.visibleDms
                            delegate: Rectangle {
                                width: ListView.view.width
                                height: 44
                                radius: 4
                                color: (client.currentChannelId === modelData.id && !root.homeView ? root.cActive
                                     : (dmItemHover.containsMouse ? root.cHover : "transparent"))
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 6
                                    anchors.rightMargin: 6
                                    spacing: 10
                                    Rectangle {
                                        Layout.preferredWidth: 32
                                        Layout.preferredHeight: 32
                                        radius: 16
                                        clip: true
                                        color: root.cHover
                                        Image {
                                            anchors.fill: parent
                                            source: root.dmAvatar(modelData)
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                        }
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 0
                                        Text {
                                            Layout.fillWidth: true
                                            text: modelData.name || "Direct Message"
                                            color: root.cText
                                            font.pixelSize: 14
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: root.dmSubtitle(modelData)
                                            color: root.cMuted
                                            font.pixelSize: 12
                                            elide: Text.ElideRight
                                        }
                                    }
                                    Rectangle {
                                        id: dmCallBtn
                                        Layout.preferredWidth: 28
                                        Layout.preferredHeight: 28
                                        radius: 4
                                        color: root.cAccent
                                        Text { anchors.centerIn: parent; text: "\uD83D\uDCDE"; color: "#ffffff"; font.pixelSize: 14 }
                                    }
                                }
                                Rectangle {
                                    width: 8
                                    height: 8
                                    radius: 4
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.rightMargin: 6
                                    color: root.cAccent
                                    visible: client.unreadChannelIds.indexOf(modelData.id) >= 0
                                }
                                MouseArea {
                                    id: dmItemHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: { root.homeView = false; root.guildView = false; client.selectChannel(modelData.id); }
                                }
                                MouseArea {
                                    id: dmCallMouse
                                    anchors.fill: dmCallBtn
                                    hoverEnabled: true
                                    onClicked: {
                                        root.homeView = false;
                                        root.guildView = false;
                                        client.selectChannel(modelData.id);
                                        client.startDmCall(modelData.id);
                                    }
                                }
                            }
                        }
                    }

                    // ===== Guild channel sidebar =====
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0
                        visible: root.guildView

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
                            text: "CHANNELS"
                            color: root.cMuted
                            font.pixelSize: 11
                            font.bold: true
                        }

                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: client.channels
                            delegate: Column {
                                id: chDelegate
                                width: ListView.view.width
                                height: implicitHeight
                                spacing: 0
                                property var voiceUsers: modelData.type === 2 ? root.voiceUsers(modelData.id) : []

                                Rectangle {
                                    id: chRow
                                    width: parent.width
                                    height: modelData.type === 4 ? 36 : 32
                                    radius: 4
                                    color: modelData.type === 4 ? "transparent"
                                         : (client.currentChannelId === modelData.id ? root.cActive
                                            : (modelData.type === 2 && client.voiceChannelId === modelData.id ? root.cActive
                                            : (chHover.containsMouse ? root.cHover : "transparent")))
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 10
                                        spacing: 6
                                        Text {
                                            visible: modelData.type === 4
                                            text: modelData.name.toUpperCase()
                                            color: root.cMuted
                                            font.pixelSize: 12
                                            font.bold: true
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            visible: modelData.type === 0
                                            text: "#"
                                            color: client.currentChannelId === modelData.id ? root.cTextStrong : root.cMuted
                                            font.pixelSize: 16
                                        }
                                        Text {
                                            visible: modelData.type === 2
                                            text: "\uD83D\uDD0A"
                                            color: client.voiceChannelId === modelData.id ? (root.light ? "#248046" : "#23a55a") : root.cMuted
                                            font.pixelSize: 16
                                        }
                                        Text {
                                            visible: modelData.type !== 4
                                            text: modelData.name
                                            color: client.currentChannelId === modelData.id ? root.cTextStrong
                                                 : (modelData.type === 2 && client.voiceChannelId === modelData.id ? (root.light ? "#248046" : "#23a55a")
                                                 : (chHover.containsMouse ? root.cTextStrong : root.cMuted))
                                            font.pixelSize: 14
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                        Text {
                                            visible: modelData.type === 2 && client.voiceChannelId === modelData.id && client.voiceConnected
                                            text: root.fmtElapsed(root.voiceSeconds)
                                            color: root.light ? "#248046" : "#23a55a"
                                            font.pixelSize: 11
                                        }
                                    }
                                    Rectangle {
                                        width: 8
                                        height: 8
                                        radius: 4
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.rightMargin: 6
                                        color: root.cAccent
                                        visible: client.unreadChannelIds.indexOf(modelData.id) >= 0
                                    }
                                    MouseArea {
                                        id: chHover
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        enabled: modelData.type !== 4
                                        onClicked: {
                                            if (modelData.type === 2) {
                                                if (client.voiceChannelId === modelData.id)
                                                    client.leaveVoice();
                                                else
                                                    client.joinVoice(client.currentGuildId, modelData.id);
                                                client.selectChannel(modelData.id);
                                            } else {
                                                client.selectChannel(modelData.id);
                                            }
                                        }
                                    }
                                }

                                Repeater {
                                    model: chDelegate.voiceUsers
                                    delegate: Item {
                                        width: chDelegate.width
                                        height: 24
                                        property var menuUserId: modelData.userId
                                        MouseArea {
                                            anchors.fill: parent
                                            acceptedButtons: Qt.RightButton
                                            onClicked: {
                                                root.voiceMenuUserId = menuUserId;
                                                voiceUserMenu.popup();
                                            }
                                        }
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 34
                                            anchors.rightMargin: 10
                                            spacing: 6
                                            Image {
                                                Layout.preferredWidth: 16
                                                Layout.preferredHeight: 16
                                                source: modelData.avatarUrl && modelData.avatarUrl.length > 0
                                                        ? modelData.avatarUrl
                                                        : root.avatar({ id: modelData.userId, avatar: modelData.avatar })
                                                fillMode: Image.PreserveAspectCrop
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                text: modelData.name
                                                color: root.cText
                                                font.pixelSize: 12
                                                elide: Text.ElideRight
                                            }
                                            Text {
                                                visible: modelData.selfMute || modelData.mute
                                                text: "\uD83D\uDD07"
                                                font.pixelSize: 10
                                                color: root.cMuted
                                            }
                                            Text {
                                                visible: modelData.selfDeaf || modelData.deaf
                                                text: "\uD83C\uDFA7"
                                                font.pixelSize: 10
                                                color: root.cMuted
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

                Rectangle {
                    id: voiceConnectedPanel
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 112 : 0
                    visible: client.inVoice
                    color: root.cUserPanel

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: client.voiceGuildId.length > 0 ? "Voice Connected" : "Call"
                                color: root.light ? "#248046" : "#23a55a"
                                font.pixelSize: 12
                                font.bold: true
                            }
                            Text {
                                text: client.voiceConnected ? root.fmtElapsed(root.voiceSeconds) : client.voiceStatus
                                color: root.cMuted
                                font.pixelSize: 11
                            }
                            Rectangle {
                                Layout.preferredWidth: 28
                                Layout.preferredHeight: 22
                                radius: 4
                                color: hangupHover.containsMouse ? "#3d1f23" : "transparent"
                                Text { anchors.centerIn: parent; text: "\u2715"; color: "#dc4247"; font.pixelSize: 13 }
                                MouseArea { id: hangupHover; anchors.fill: parent; hoverEnabled: true; onClicked: client.leaveVoice() }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: root.voiceChannelName()
                            color: root.cText
                            font.pixelSize: 13
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: root.voiceGuildName()
                            color: root.cMuted
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            visible: client.voiceGuildId.length > 0
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Rectangle {
                                Layout.preferredWidth: 28
                                Layout.preferredHeight: 26
                                radius: 4
                                color: client.selfMute ? "#3d1f23" : (panelMicHover.containsMouse ? root.cHover : "transparent")
                                Text {
                                    anchors.centerIn: parent
                                    text: client.selfMute ? "\uD83D\uDD07" : "\uD83C\uDFA4"
                                    font.pixelSize: 14
                                }
                                MouseArea {
                                    id: panelMicHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: client.setSelfMute(!client.selfMute)
                                }
                            }
                            Rectangle {
                                Layout.preferredWidth: 18
                                Layout.preferredHeight: 26
                                radius: 4
                                color: micChevronHover.containsMouse ? root.cHover : "transparent"
                                Text { anchors.centerIn: parent; text: "\u25B4"; color: root.cMuted; font.pixelSize: 10 }
                                MouseArea { id: micChevronHover; anchors.fill: parent; hoverEnabled: true; onClicked: micPopup.open() }
                            }
                            Rectangle {
                                Layout.preferredWidth: 28
                                Layout.preferredHeight: 26
                                radius: 4
                                color: client.selfDeaf ? "#3d1f23" : (panelDeafHover.containsMouse ? root.cHover : "transparent")
                                Text {
                                    anchors.centerIn: parent
                                    text: client.selfDeaf ? "\uD83D\uDD07" : "\uD83C\uDFA7"
                                    font.pixelSize: 14
                                }
                                MouseArea {
                                    id: panelDeafHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: client.setSelfDeaf(!client.selfDeaf)
                                }
                            }
                            Rectangle {
                                Layout.preferredWidth: 18
                                Layout.preferredHeight: 26
                                radius: 4
                                color: deafChevronHover.containsMouse ? root.cHover : "transparent"
                                Text { anchors.centerIn: parent; text: "\u25B4"; color: root.cMuted; font.pixelSize: 10 }
                                MouseArea { id: deafChevronHover; anchors.fill: parent; hoverEnabled: true; onClicked: deafPopup.open() }
                            }
                            Item { Layout.fillWidth: true }
                        }
                    }

                    Popup {
                        id: micPopup
                        x: 8
                        y: -height - 8
                        width: 280
                        padding: 12
                        background: Rectangle { color: root.cCard; radius: 8; border.width: 1; border.color: root.cBorder }
                        contentItem: ColumnLayout {
                            spacing: 10
                            Text { text: "Input Device"; color: root.cMuted; font.pixelSize: 11; font.bold: true }
                            ComboBox {
                                Layout.fillWidth: true
                                model: root.inputDeviceNames()
                                currentIndex: Math.max(0, root.inputDeviceNames().indexOf(voiceSettings.inputDevice))
                                onActivated: voiceSettings.inputDevice = currentText
                            }
                            Text { text: "Input Profile"; color: root.cMuted; font.pixelSize: 11; font.bold: true }
                            Text { text: "Voice Isolation"; color: root.cText; font.pixelSize: 13 }
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "Input Volume"; color: root.cText; font.pixelSize: 12 }
                                Slider {
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 100
                                    stepSize: 1
                                    value: voiceSettings.inputVolume
                                    onMoved: voiceSettings.inputVolume = value
                                }
                            }
                            Text {
                                text: "Voice Settings"
                                color: root.cLink
                                font.pixelSize: 12
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: { micPopup.close(); voiceSettingsDialog.open(); }
                                }
                            }
                        }
                    }

                    Popup {
                        id: deafPopup
                        x: 8
                        y: -height - 8
                        width: 280
                        padding: 12
                        background: Rectangle { color: root.cCard; radius: 8; border.width: 1; border.color: root.cBorder }
                        contentItem: ColumnLayout {
                            spacing: 10
                            Text { text: "Output Device"; color: root.cMuted; font.pixelSize: 11; font.bold: true }
                            ComboBox {
                                Layout.fillWidth: true
                                model: root.outputDeviceNames()
                                currentIndex: Math.max(0, root.outputDeviceNames().indexOf(voiceSettings.outputDevice))
                                onActivated: voiceSettings.outputDevice = currentText
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "Output Volume"; color: root.cText; font.pixelSize: 12 }
                                Slider {
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 100
                                    stepSize: 1
                                    value: voiceSettings.outputVolume
                                    onMoved: voiceSettings.outputVolume = value
                                }
                            }
                            Text {
                                text: "Voice Settings"
                                color: root.cLink
                                font.pixelSize: 12
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: { deafPopup.close(); voiceSettingsDialog.open(); }
                                }
                            }
                        }
                    }

                    Settings {
                        id: voiceSettings
                        category: "Voice"
                        property string inputDevice: ""
                        property string outputDevice: ""
                        property int inputVolume: 100
                        property int outputVolume: 100
                    }
                }

                // ---- User panel (spans rail + sidebar) ----
                Rectangle {
                    id: userPanel
                Layout.fillWidth: true
                Layout.preferredHeight: 52
                color: root.cUserPanel

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8

                    Item {
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        Rectangle {
                            anchors.fill: parent
                            radius: 16
                            clip: true
                            color: root.cHover
                            Image {
                                anchors.fill: parent
                                source: root.avatar(client.user)
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                            }
                        }
                        Rectangle {
                            width: 10; height: 10; radius: 5
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            color: root.light ? "#248046" : "#3d9e60"
                            border.width: 2
                            border.color: root.cUserPanel
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            Layout.fillWidth: true
                            text: root.myName()
                            color: root.cText
                            font.pixelSize: 13
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        Text { text: "Online"; color: root.cMuted; font.pixelSize: 11 }
                    }

                    Rectangle {
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        radius: 4
                        color: client.selfMute ? "#3d1f23" : (micHover.containsMouse ? root.cHover : "transparent")
                        Text { anchors.centerIn: parent; text: client.selfMute ? "\uD83D\uDD07" : "\uD83C\uDFA4"; font.pixelSize: 14 }
                        MouseArea { id: micHover; anchors.fill: parent; hoverEnabled: true; onClicked: client.setSelfMute(!client.selfMute) }
                    }
                    Rectangle {
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        radius: 4
                        color: client.selfDeaf ? "#3d1f23" : (deafHover.containsMouse ? root.cHover : "transparent")
                        Text { anchors.centerIn: parent; text: client.selfDeaf ? "\uD83D\uDD07" : "\uD83C\uDFA7"; font.pixelSize: 14 }
                        MouseArea { id: deafHover; anchors.fill: parent; hoverEnabled: true; onClicked: client.setSelfDeaf(!client.selfDeaf) }
                    }
                    Rectangle {
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        radius: 4
                        color: gearHover.containsMouse ? root.cHover : "transparent"
                        Text { anchors.centerIn: parent; text: "\u2699"; color: root.cMuted; font.pixelSize: 16 }
                        MouseArea {
                            id: gearHover
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: client.setTheme(client.theme === "light" ? "dark" : "light")
                        }
                    }
                }
            }
        }

        // ---- Main area ----
        Rectangle {
            id: mainArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: root.cMain

            // ===== Friends home =====
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                visible: root.homeView

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    color: root.cMain
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: root.cBorder
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 8
                        Text { text: "\uD83D\uDC65"; font.pixelSize: 16 }
                        Text { text: "Friends"; color: root.cTextStrong; font.pixelSize: 15; font.bold: true }
                        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 20; Layout.leftMargin: 6; Layout.rightMargin: 6; color: root.cBorder }

                        Rectangle {
                            Layout.preferredWidth: allText.implicitWidth + 18
                            Layout.preferredHeight: 26
                            radius: 4
                            color: root.cActive
                            Text { id: allText; anchors.centerIn: parent; text: "All"; color: root.cTextStrong; font.pixelSize: 14 }
                        }
                        Text { text: "Pending"; color: root.cMuted; font.pixelSize: 14; Layout.leftMargin: 6 }
                        Rectangle {
                            Layout.preferredWidth: afText.implicitWidth + 22
                            Layout.preferredHeight: 26
                            radius: 4
                            color: root.cAccent
                            Text { id: afText; anchors.centerIn: parent; text: "Add Friend"; color: "#ffffff"; font.pixelSize: 14 }
                        }

                        Item { Layout.fillWidth: true }
                    }
                }

                Item {
                    id: emptyItem
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Text {
                        id: emptyText
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 64, 520)
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: "There are no friends online at this time. Check back later!"
                        color: root.cMuted
                        font.pixelSize: 15
                    }
                }
            }

            // ===== Chat (guild channel or DM) =====
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                visible: !root.homeView

                // Header
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    color: root.cHeader
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: root.cBorder
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        Text {
                            text: (client.currentChannelIsDm ? "@ " : "# ") + root.channelName()
                            color: root.cText
                            font.pixelSize: 16
                            font.bold: true
                            Layout.fillWidth: true
                        }
                        Rectangle {
                            Layout.preferredWidth: 190
                            Layout.preferredHeight: 28
                            height: 28
                            radius: 4
                            color: root.cBase
                            TextInput {
                                id: msgSearchInput
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                verticalAlignment: TextInput.AlignVCenter
                                color: root.cText
                                font.pixelSize: 13
                                clip: true
                                selectByMouse: true
                                onTextChanged: root.msgSearch = text
                            }
                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                verticalAlignment: Text.AlignVCenter
                                text: "Search"
                                color: root.cMuted
                                font.pixelSize: 13
                                visible: msgSearchInput.text.length === 0
                            }
                        }
                        Button { text: "Pinned"; implicitHeight: 28; onClicked: { root.showPins = !root.showPins; if (root.showPins) client.fetchPins(client.currentChannelId); } }
                        Button { text: "Members"; implicitHeight: 28; onClicked: root.showMembers = !root.showMembers }
                        Rectangle {
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 28
                            radius: 4
                            visible: client.currentChannelIsDm
                            color: root.cAccent
                            Text { anchors.centerIn: parent; text: "\uD83D\uDCDE"; color: "#ffffff"; font.pixelSize: 14 }
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: client.startDmCall(client.currentChannelId)
                            }
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
                    model: root.msgSearch.length === 0
                        ? client.activeMessages
                        : client.activeMessages.filter(function(m) {
                            return (m.content || "").toLowerCase().indexOf(root.msgSearch.toLowerCase()) !== -1;
                        })
                    spacing: 2
                    onCountChanged: positionViewAtEnd()
                    delegate: Rectangle {
                        width: messageView.width
                        height: msgLayout.implicitHeight + 12
                        property bool mentionsMe: {
                            if (!modelData.mentions) return false;
                            for (var i = 0; i < modelData.mentions.length; ++i)
                                if (client.user && modelData.mentions[i].id === client.user.id) return true;
                            return false;
                        }
                        color: msgHoverHandler.hovered ? root.cMsgHover
                             : (mentionsMe ? root.cMentionBg : "transparent")
                        HoverHandler { id: msgHoverHandler }

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
                                        color: root.cTextStrong
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
                                        color: root.cEmbed
                                        radius: 4
                                        Rectangle { width: 4; height: parent.height; color: root.cAccent; radius: 2 }
                                        ColumnLayout {
                                            id: embedCol
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            anchors.leftMargin: 14
                                            anchors.topMargin: 8
                                            spacing: 3
                                            Text { visible: modelData.title !== undefined && modelData.title !== ""; text: modelData.title || ""; color: root.cTextStrong; font.bold: true; font.pixelSize: 13; Layout.fillWidth: true; wrapMode: Text.Wrap }
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
                                        implicitHeight: root.isImageAttachment(modelData) ? 220 : 24
                                        Image {
                                            id: attImg
                                            visible: root.isImageAttachment(modelData)
                                            source: root.attachmentUrl(modelData)
                                            width: Math.min(parent.width, 320); height: 220
                                            fillMode: Image.PreserveAspectFit
                                            asynchronous: true
                                            onStatusChanged: {
                                                if (status === Image.Error)
                                                    source = "";
                                            }
                                        }
                                        Text {
                                            visible: !root.isImageAttachment(modelData) || attImg.source === ""
                                            text: "\uD83D\uDCCE " + (modelData.filename || "file")
                                            color: root.cLink
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
                                            color: modelData.me ? "#3D5865F2" : root.cHover
                                            border.color: modelData.me ? root.cAccent : root.cBorder
                                            RowLayout {
                                                id: rl
                                                anchors.centerIn: parent
                                                spacing: 4
                                                Text { text: modelData.emoji ? (modelData.emoji.name || "?") : "?"; font.pixelSize: 13 }
                                                Text { text: modelData.count; color: modelData.me ? "#b9c8ff" : root.cMuted; font.pixelSize: 12 }
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
                        radius: 8
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
                                placeholderText: "Message " + (client.currentChannelIsDm ? "@" : "#") + root.channelName()
                                color: root.cText
                                background: Item {}
                                onTextChanged: if (client.currentChannelId.length > 0 && text.length > 0) client.triggerTyping(client.currentChannelId)
                                onAccepted: root.send()
                            }
                            Button {
                                id: sendButton
                                text: "Send"
                                implicitWidth: 64
                                implicitHeight: 32
                                enabled: messageInput.text.length > 0
                                onClicked: root.send()
                                background: Rectangle {
                                    radius: 4
                                    color: !sendButton.enabled ? root.cHover
                                         : (sendButton.pressed ? "#3a48a3" : (sendButton.hovered ? "#4452bb" : root.cAccent))
                                }
                                contentItem: Text {
                                    text: sendButton.text
                                    color: sendButton.enabled ? "#ffffff" : root.cMuted
                                    font.pixelSize: 13
                                    font.bold: true
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---- Right panel: Active Now (home) / Members (guild) ----
        Rectangle {
            id: rightPanel
            Layout.preferredWidth: root.guildView ? (root.showMembers ? 240 : 0) : 300
            Layout.fillHeight: true
            visible: root.guildView ? root.showMembers : true
            color: root.cSidebar
            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 1
                color: root.cBorder
            }

            // ===== Active Now =====
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12
                visible: !root.guildView

                Text { text: "Active Now"; color: root.cTextStrong; font.pixelSize: 16; font.bold: true }

                Rectangle {
                    visible: root.showActiveCard
                    Layout.fillWidth: true
                    Layout.preferredHeight: activeCol.implicitHeight + 32
                    radius: 8
                    color: root.cCard
                    ColumnLayout {
                        id: activeCol
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 10
                        Text {
                            Layout.fillWidth: true
                            text: "This section could be better"
                            color: root.cTextStrong
                            font.pixelSize: 14
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "We can personalise 'Active Now' and other parts of Discord based on who you talk to and what games you play."
                            color: root.cMuted
                            font.pixelSize: 12
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                        }
                        Button {
                            id: yesBtn
                            Layout.fillWidth: true
                            Layout.preferredHeight: 32
                            text: "Yes, count me in!"
                            onClicked: root.showActiveCard = false
                            background: Rectangle {
                                radius: 4
                                color: yesBtn.pressed ? "#3a48a3" : (yesBtn.hovered ? "#4452bb" : root.cAccent)
                            }
                            contentItem: Text {
                                text: "Yes, count me in!"
                                color: "#ffffff"
                                font.pixelSize: 12
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                        Button {
                            id: noBtn
                            Layout.fillWidth: true
                            Layout.preferredHeight: 32
                            text: "No, thanks"
                            onClicked: root.showActiveCard = false
                            background: Rectangle { radius: 4; color: noBtn.hovered ? root.cActive : root.cHover }
                            contentItem: Text {
                                text: "No, thanks"
                                color: root.cText
                                font.pixelSize: 12
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }

                Text { text: "It's quiet for now..."; color: root.cTextStrong; font.pixelSize: 14; font.bold: true }
                Text {
                    Layout.fillWidth: true
                    text: "When a friend starts an activity\u2014like playing a game or hanging out on voice\u2014we'll show it here!"
                    color: root.cMuted
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }
                Item { Layout.fillHeight: true }
            }

            // ===== Members =====
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                visible: root.guildView

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 14
                    Layout.rightMargin: 8
                    Layout.topMargin: 10
                    Text {
                        text: "MEMBERS \u2014 " + client.members.length
                        color: root.cMuted
                        font.pixelSize: 11
                        font.bold: true
                        Layout.fillWidth: true
                    }
                    Button { text: "\u21BB"; implicitWidth: 26; implicitHeight: 24; onClicked: client.refreshMembers(client.currentGuildId) }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: root.memberList
                    section.property: "_group"
                    section.criteria: ViewSection.FullString
                    section.delegate: Item {
                        width: ListView.view.width
                        height: 26
                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.bottom: parent.bottom
                            text: section
                            color: root.cMuted
                            font.pixelSize: 11
                            font.bold: true
                        }
                    }
                    delegate: Item {
                        width: ListView.view.width
                        height: 42
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 8
                            spacing: 8
                            Item {
                                Layout.preferredWidth: 30
                                Layout.preferredHeight: 30
                                Image {
                                    anchors.fill: parent
                                    source: root.avatar(modelData)
                                    fillMode: Image.PreserveAspectCrop
                                }
                                Rectangle {
                                    width: 11; height: 11; radius: 6
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    color: root.statusColor(modelData.status)
                                    border.width: 2
                                    border.color: root.cSidebar
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: (modelData.nick && modelData.nick.length > 0) ? modelData.nick
                                      : ((modelData.global_name && modelData.global_name.length > 0) ? modelData.global_name : modelData.username)
                                color: (modelData.status === "offline") ? root.cMuted : root.cText
                                font.pixelSize: 13
                                elide: Text.ElideRight
                            }
                            Text {
                                visible: modelData.voice_channel && ("" + modelData.voice_channel).length > 0
                                text: (modelData.voice_muted || modelData.voice_deaf) ? "\uD83D\uDD07" : "\uD83D\uDD0A"
                                color: root.cMuted
                                font.pixelSize: 11
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.RightButton
                            property var memberData: modelData
                            onClicked: function(mouse) {
                                if (mouse.button === Qt.RightButton) {
                                    memberMenu.memberData = memberData;
                                    memberMenu.popup();
                                }
                            }
                        }
                        Menu {
                            id: memberMenu
                            property var memberData: null
                            MenuItem {
                                text: "Mute"
                                visible: memberMenu.memberData && client.user && memberMenu.memberData.id === client.user.id && !client.selfMute
                                onClicked: client.setSelfMute(true)
                            }
                            MenuItem {
                                text: "Unmute"
                                visible: memberMenu.memberData && client.user && memberMenu.memberData.id === client.user.id && client.selfMute
                                onClicked: client.setSelfMute(false)
                            }
                            MenuItem {
                                text: "Deafen"
                                visible: memberMenu.memberData && client.user && memberMenu.memberData.id === client.user.id && !client.selfDeaf
                                onClicked: client.setSelfDeaf(true)
                            }
                            MenuItem {
                                text: "Undeafen"
                                visible: memberMenu.memberData && client.user && memberMenu.memberData.id === client.user.id && client.selfDeaf
                                onClicked: client.setSelfDeaf(false)
                            }
                            MenuItem {
                                text: "Copy User ID"
                                onClicked: if (memberMenu.memberData) client.copyToClipboard(memberMenu.memberData.id)
                            }
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        id: pinsPanel
        visible: root.showPins
        z: 100
        width: 380
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        color: root.cSidebar
        Rectangle { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 1; color: root.cBorder }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                Text { text: "Pinned Messages"; color: root.cTextStrong; font.bold: true; font.pixelSize: 15; Layout.fillWidth: true }
                Button { text: "Close"; implicitHeight: 26; onClicked: root.showPins = false }
            }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: client.pins
                spacing: 8
                delegate: Rectangle {
                    width: ListView.view.width
                    height: pinText.implicitHeight + 16
                    radius: 6
                    color: root.cBase
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 2
                        Text { text: (modelData.author && (modelData.author.global_name || modelData.author.username)) || "Unknown"; color: root.cTextStrong; font.bold: true; font.pixelSize: 13; elide: Text.ElideRight; Layout.fillWidth: true }
                        Text { id: pinText; text: (modelData.content && modelData.content.length > 0) ? modelData.content : "[attachment]"; color: root.cText; font.pixelSize: 13; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    }
                }
            }
        }
    }

    // ---- Login page ----
    Rectangle {
        anchors.fill: parent
        visible: !root.loggedIn
        color: root.cBase

        Rectangle {
            width: 440
            height: loginCol.implicitHeight + 64
            anchors.centerIn: parent
            radius: 16
            color: root.cCard

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
                    text: "Use user token (self-bot \u2014 may violate ToS)"
                }

                Button {
                    id: loginButton
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    text: "Log In"
                    enabled: tokenInput.text.length > 0
                    onClicked: root.doLogin()
                    background: Rectangle {
                        radius: 4
                        color: !loginButton.enabled ? root.cHover
                             : (loginButton.pressed ? "#3a48a3" : (loginButton.hovered ? "#4452bb" : root.cAccent))
                    }
                    contentItem: Text {
                        text: loginButton.text
                        color: loginButton.enabled ? "#ffffff" : root.cMuted
                        font.pixelSize: 14
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
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

    Dialog {
        id: voiceSettingsDialog
        title: "Voice Settings"
        modal: true
        width: 440
        padding: 16
        anchors.centerIn: Overlay.overlay
        background: Rectangle { color: root.cCard; radius: 8; border.width: 1; border.color: root.cBorder }
        contentItem: ColumnLayout {
            spacing: 12
            Text { text: "Voice Settings"; color: root.cTextStrong; font.pixelSize: 18; font.bold: true }
            Text { text: "Input Device"; color: root.cMuted; font.pixelSize: 11; font.bold: true }
            ComboBox {
                Layout.fillWidth: true
                model: root.inputDeviceNames()
                currentIndex: Math.max(0, root.inputDeviceNames().indexOf(voiceSettings.inputDevice))
                onActivated: voiceSettings.inputDevice = currentText
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Input Volume"; color: root.cText; font.pixelSize: 12 }
                Slider { Layout.fillWidth: true; from: 0; to: 100; stepSize: 1; value: voiceSettings.inputVolume; onMoved: voiceSettings.inputVolume = value }
            }
            Text { text: "Output Device"; color: root.cMuted; font.pixelSize: 11; font.bold: true }
            ComboBox {
                Layout.fillWidth: true
                model: root.outputDeviceNames()
                currentIndex: Math.max(0, root.outputDeviceNames().indexOf(voiceSettings.outputDevice))
                onActivated: voiceSettings.outputDevice = currentText
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Output Volume"; color: root.cText; font.pixelSize: 12 }
                Slider { Layout.fillWidth: true; from: 0; to: 100; stepSize: 1; value: voiceSettings.outputVolume; onMoved: voiceSettings.outputVolume = value }
            }
            Text {
                Layout.fillWidth: true
                text: "Audio is not captured or played in this build yet."
                color: root.cMuted
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }
            Button { text: "Done"; onClicked: voiceSettingsDialog.close() }
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

    Timer {
        id: voiceTimer
        interval: 1000
        repeat: true
        running: client.inVoice && client.voiceConnected
        onTriggered: root.voiceSeconds++
    }

    Timer {
        id: membersTimer
        interval: 2000
        repeat: false
        onTriggered: if (client.loggedIn && client.currentGuildId.length > 0) client.refreshMembers(client.currentGuildId)
    }

    Connections {
        target: client
        function onStatusChanged() {
            if (client.status === "Connected." && !root.dmsLoaded) {
                root.dmsLoaded = true;
                client.refreshDms();
                membersTimer.start();
            }
        }
        function onCurrentGuildIdChanged() {
            if (client.currentGuildId.length > 0 && client.status === "Connected.")
                client.refreshMembers(client.currentGuildId);
        }
        function onCurrentChannelIdChanged() {
            root.msgSearch = "";
            msgSearchInput.text = "";
            root.showPins = false;
        }
        function onVoiceChanged() {
            if (!client.voiceConnected)
                root.voiceSeconds = 0;
        }
        function onDmCallCreated(channelId, ringingUsers) {
            if (client.user && ringingUsers.indexOf(client.user.id) >= 0)
                root.incomingCallId = channelId;
        }
        function onDmCallDeleted(channelId) {
            if (root.incomingCallId === channelId)
                root.incomingCallId = "";
        }
    }

    Menu {
        id: voiceUserMenu
        Slider {
            id: voiceUserVolume
            from: 0; to: 1; value: 0.5
            stepSize: 0.05
            Layout.preferredWidth: 140
            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; anchors.leftMargin: 6; text: "User Volume"; color: "#c1c1c1"; font.pixelSize: 11 }
        }
        MenuItem {
            text: "Server Mute"
            onTriggered: client.editVoiceMember(voiceMenuUserId, true, false, "", "")
        }
        MenuItem {
            text: "Server Unmute"
            onTriggered: client.editVoiceMember(voiceMenuUserId, false, false, "", "")
        }
        MenuItem {
            text: "Server Deafen"
            onTriggered: client.editVoiceMember(voiceMenuUserId, false, true, "", "")
        }
        MenuItem {
            text: "Server Undeafen"
            onTriggered: client.editVoiceMember(voiceMenuUserId, false, false, "", "")
        }
        MenuItem {
            text: "Disconnect"
            onTriggered: client.editVoiceMember(voiceMenuUserId, false, false, "", "")
        }
        MenuItem {
            text: "Copy User ID"
            onTriggered: client.copyToClipboard(voiceMenuUserId)
        }
    }

    Rectangle {
        id: incomingCallBanner
        visible: root.incomingCallId.length > 0
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 16
        width: 340
        height: 64
        radius: 8
        color: root.cUserPanel
        border.color: root.cAccent
        z: 1000
        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text {
                    text: "Incoming call"
                    color: root.cMuted
                    font.pixelSize: 11
                }
                Text {
                    Layout.fillWidth: true
                    text: root.dmChannelName(root.incomingCallId)
                    color: root.cText
                    font.pixelSize: 14
                    font.bold: true
                    elide: Text.ElideRight
                }
            }
            Button {
                text: "Accept"
                implicitHeight: 28
                onClicked: {
                    var cid = root.incomingCallId;
                    root.incomingCallId = "";
                    client.joinVoice("", cid);
                }
            }
            Button {
                text: "Decline"
                implicitHeight: 28
                onClicked: {
                    var cid = root.incomingCallId;
                    root.incomingCallId = "";
                    client.stopDmRinging(cid);
                }
            }
        }
    }
}


