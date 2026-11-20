import QtQuick
import Jfd.Ui.Theme

// A title as the film's or series' logo when the server has one, otherwise
// as large text. The logo keeps its proportions inside maxWidth x
// maxHeight and sits at the left; the text wraps to two lines at most.
// `text` is also the accessible name, so a logo still reads as the title.
Item {
    id: title

    property string text
    // An image source for the logo (empty: the text is shown).
    property string source
    property int maxWidth: 480
    property int maxHeight: 140
    property int fontSize: Theme.fontSizeHeadline
    property int fontWeight: Theme.fontWeightBold
    property int maximumLineCount: 2
    readonly property bool showsLogo: logo.status === Image.Ready && source.length > 0

    implicitWidth: showsLogo ? logo.paintedWidth : Math.min(label.implicitWidth, maxWidth)
    implicitHeight: showsLogo ? logo.paintedHeight : label.implicitHeight
    Accessible.role: Accessible.Heading
    Accessible.name: text

    Image {
        id: logo
        objectName: "titleLogoImage"
        width: title.maxWidth
        height: title.maxHeight
        source: title.source
        sourceSize.width: title.maxWidth * 2
        fillMode: Image.PreserveAspectFit
        horizontalAlignment: Image.AlignLeft
        // At the top: the item is as tall as the painted logo.
        verticalAlignment: Image.AlignTop
        asynchronous: true
        visible: title.showsLogo
        opacity: visible ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }
    }

    Text {
        id: label
        objectName: "titleLogoText"
        width: title.width > 0 ? title.width : title.maxWidth
        visible: !title.showsLogo
        text: title.text
        color: Theme.colorsFor(title).text
        font.pixelSize: title.fontSize
        font.weight: title.fontWeight
        lineHeight: 1.05
        wrapMode: Text.Wrap
        maximumLineCount: title.maximumLineCount
        elide: Text.ElideRight
    }
}
