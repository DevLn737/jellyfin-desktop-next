import QtQuick
import JellyfinDesktop.Media

// Bottom on-screen display: title, position and a progress bar.
Item {
    id: osd

    required property MpvPlayer player
    property bool shown: true
    signal menuRequested()

    height: 150
    opacity: shown ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 200 } }

    function format(seconds) {
        const total = Math.max(0, Math.floor(seconds))
        const h = Math.floor(total / 3600)
        const m = Math.floor(total / 60) % 60
        const s = total % 60
        const mm = h > 0 && m < 10 ? "0" + m : "" + m
        return (h > 0 ? h + ":" : "") + mm + ":" + (s < 10 ? "0" + s : s)
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.75) }
        }
    }

    Text {
        id: titleText
        anchors.left: parent.left
        anchors.bottom: progress.top
        anchors.margins: 28
        anchors.bottomMargin: 14
        color: "white"
        font.pixelSize: 22
        elide: Text.ElideRight
        width: parent.width - 220
        text: osd.player.title
    }

    Text {
        anchors.right: parent.right
        anchors.bottom: progress.top
        anchors.rightMargin: 28
        anchors.bottomMargin: 14
        color: Qt.rgba(1, 1, 1, 0.85)
        font.pixelSize: 17
        text: osd.format(osd.player.position) + " / " + osd.format(osd.player.duration)
    }

    Rectangle {
        id: progress
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 28
        anchors.bottomMargin: 34
        height: 5
        radius: 3
        color: Qt.rgba(1, 1, 1, 0.25)

        Rectangle {
            height: parent.height
            radius: parent.radius
            color: "#00a4dc"
            width: osd.player.duration > 0 ? parent.width * Math.min(1, osd.player.position / osd.player.duration) : 0
        }
        MouseArea {
            anchors.fill: parent
            anchors.margins: -8
            onClicked: (mouse) => osd.player.seek(osd.player.duration * mouse.x / width)
        }
    }

    Text {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 28
        anchors.bottomMargin: 6
        color: Qt.rgba(1, 1, 1, 0.6)
        font.pixelSize: 13
        text: osd.player.paused ? "Пауза · Пробел — продолжить · M — меню · F — полный экран" : "M — меню · F — полный экран · I — статистика"
        MouseArea { anchors.fill: parent; onClicked: osd.menuRequested() }
    }
}
