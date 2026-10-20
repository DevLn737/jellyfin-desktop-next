import QtQuick
import JellyfinDesktop.Media

// 640x360 window. The video occupies a 320x180 rectangle at (40, 60); a
// half-transparent red square covers its top-left corner; a solid white
// square sits outside the video and must stay untouched by mpv's drawing.
Window {
    id: root
    required property MpvPlayer player
    width: 640
    height: 360
    visible: true
    color: "black"

    MpvVideo {
        objectName: "video"
        x: 40; y: 60; width: 320; height: 180
        player: root.player
    }
    Rectangle {
        x: 40; y: 60; width: 60; height: 40
        color: Qt.rgba(1, 0, 0, 0.5)
    }
    Rectangle {
        x: 480; y: 40; width: 100; height: 100
        color: "white"
        // A rotating child forces Qt to keep drawing with its own state
        // after mpv has rendered in the same frame.
        Rectangle {
            anchors.centerIn: parent
            width: 40; height: 40
            color: "white"
            NumberAnimation on rotation { from: 0; to: 360; duration: 1000; loops: Animation.Infinite }
        }
    }
}
