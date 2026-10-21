import QtQuick
import Jfd.Ui.Theme

// A square icon from Icons, drawn sharp at any scale.
Image {
    property int size: Theme.iconSize

    width: size
    height: size
    sourceSize.width: size * 2
    sourceSize.height: size * 2
    fillMode: Image.PreserveAspectFit
    smooth: true
}
