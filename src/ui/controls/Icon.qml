import QtQuick
import Jfd.Ui.Theme

// A square icon from Icons, drawn sharp at any scale. The icon files are
// light; on a light colour scheme the dark twin in icons-dark/ is shown,
// so `color` (the text colour by default) decides which one reads better.
Item {
    id: icon

    property url source
    property int size: Theme.iconSize
    // What the icon sits on is lighter or darker than this colour; only its
    // brightness counts.
    property color color: Theme.colorsFor(icon).text
    property alias status: image.status
    readonly property bool darkIcon: (0.299 * color.r + 0.587 * color.g + 0.114 * color.b) < 0.5

    width: size
    height: size

    Image {
        id: image
        anchors.fill: parent
        source: icon.darkIcon ? icon.source.toString().replace("/icons/", "/icons-dark/") : icon.source
        sourceSize.width: icon.size * 2
        sourceSize.height: icon.size * 2
        fillMode: Image.PreserveAspectFit
        smooth: true
    }
}
