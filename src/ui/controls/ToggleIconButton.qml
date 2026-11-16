import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme

// A square button with only an icon that shows an on/off state (favorite,
// watched): a hairline edge when off, an accent tint and edge when on.
// `toolTipText` is also the accessible name; `on` is the state to show (the
// owner flips it when the action has been done).
T.Button {
    id: control
    readonly property var colors: Theme.colorsFor(control)

    property url iconSource
    property string toolTipText
    property bool on: false

    implicitWidth: Theme.controlHeight
    implicitHeight: Theme.controlHeight
    focusPolicy: Qt.StrongFocus
    Accessible.name: toolTipText
    Accessible.role: Accessible.CheckBox
    Accessible.checkable: true
    Accessible.checked: on

    Keys.onReturnPressed: control.clicked()
    Keys.onEnterPressed: control.clicked()

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            source: control.iconSource
            size: Theme.iconSize - 2
            color: colors.text
            opacity: control.enabled ? 1 : 0.45
        }
    }

    background: Rectangle {
        radius: Theme.radius
        color: control.on ? Qt.alpha(colors.accent, control.hovered ? 0.28 : 0.18)
             : control.down || control.hovered ? colors.surfaceHover : colors.surfaceRaised
        border.width: 1
        border.color: control.on ? Qt.alpha(colors.accent, 0.6) : (colors.glassBorder !== undefined ? colors.glassBorder : colors.border)
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        FocusFrame { shown: control.visualFocus }
    }

    ToolTipLabel {
        text: control.toolTipText
        shown: control.hovered && !control.down && control.toolTipText.length > 0
    }
}
