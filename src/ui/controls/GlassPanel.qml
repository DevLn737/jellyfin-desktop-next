import QtQuick
import Jfd.Ui.Theme

// A panel of the "Depth" look: a translucent fill and a hairline edge. With
// simplified effects (Theme.blurEnabled false) the fill is nearly solid.
// No blur is drawn by the panel itself, so it is safe next to live video;
// it is not put over the video either (the player keeps Overlay colours).
Rectangle {
    id: panel
    property bool popup: false
    radius: Theme.radiusLarge
    color: popup ? Theme.popupFill : Theme.panelFill
    border.width: 1
    border.color: Theme.glassBorder
}
