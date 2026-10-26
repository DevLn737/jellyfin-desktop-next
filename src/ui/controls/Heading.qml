import QtQuick
import Jfd.Ui.Theme

// A page title.
Text {
    id: heading
    // Colours of the nearest colour scope (the video player has its own).
    readonly property var colors: Theme.colorsFor(heading)

    color: colors.text
    font.pixelSize: Theme.fontSizeTitle
    font.weight: Theme.fontWeightBold
    elide: Text.ElideRight
}
