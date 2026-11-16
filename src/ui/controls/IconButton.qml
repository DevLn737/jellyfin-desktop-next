import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme

// A round button with only an icon. `toolTipText` is also the accessible name.
// `square` draws it as a square with a hairline edge instead (a page's top
// bar).
T.Button {
    id: control
    // Colours of the nearest colour scope (the video player has its own).
    readonly property var colors: Theme.colorsFor(control)

    property url iconSource
    property string toolTipText
    // Hides the tooltip, for example while the menu the button opened shows.
    property bool toolTipSuppressed: false
    property bool square: false

    implicitWidth: Theme.controlHeight
    implicitHeight: Theme.controlHeight
    focusPolicy: Qt.StrongFocus
    Accessible.name: toolTipText

    Keys.onReturnPressed: control.clicked()
    Keys.onEnterPressed: control.clicked()

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            source: control.iconSource
            opacity: control.enabled ? 1 : 0.45
        }
    }

    background: Rectangle {
        radius: control.square ? Theme.radius : width / 2
        color: control.down || control.hovered ? colors.surfaceHover
             : control.square ? colors.surfaceRaised : "transparent"
        border.width: control.square ? 1 : 0
        border.color: colors.glassBorder !== undefined ? colors.glassBorder : colors.border
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        FocusFrame { shown: control.visualFocus; radius: control.square ? Theme.radius + Theme.focusRingWidth : width / 2 }
    }

    ToolTipLabel {
        text: control.toolTipText
        shown: control.hovered && !control.down && !control.toolTipSuppressed && control.toolTipText.length > 0
    }
}
