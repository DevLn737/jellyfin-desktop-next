import QtQuick

// The app's own mark (packaging/icons has the same drawing for the window,
// the taskbar and the installers).
Image {
    property int size: 32
    width: size
    height: size
    source: Qt.resolvedUrl("brand/logo.svg")
    sourceSize.width: size * 2
    sourceSize.height: size * 2
    smooth: true
    Accessible.role: Accessible.Graphic
    Accessible.name: Qt.application.name
}
