import QtQuick
import Jfd.Ui.Theme

// A small rounded label with a hairline edge: a genre on a banner or an
// item's page.
Rectangle {
    id: chip

    property string text
    property int fontSize: Theme.fontSizeSmall + 1

    implicitWidth: label.implicitWidth + 2 * (Theme.tvMode ? 16 : 11)
    implicitHeight: label.implicitHeight + (Theme.tvMode ? 12 : 8)
    radius: Theme.radius - 2
    color: Qt.alpha(Theme.text, 0.06)
    border.width: 1
    border.color: Qt.alpha(Theme.text, 0.16)

    Text {
        id: label
        anchors.centerIn: parent
        text: chip.text
        color: Theme.text
        font.pixelSize: chip.fontSize
        font.weight: Font.DemiBold
    }
}
