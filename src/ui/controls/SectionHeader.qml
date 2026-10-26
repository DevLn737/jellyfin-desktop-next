import QtQuick
import Jfd.Ui.Theme

// The title above a group of content.
Text {
    id: header
    // Colours of the nearest colour scope (the video player has its own).
    readonly property var colors: Theme.colorsFor(header)

    color: colors.text
    font.pixelSize: Theme.fontSizeLarge
    font.weight: Theme.fontWeightBold
    elide: Text.ElideRight
}
