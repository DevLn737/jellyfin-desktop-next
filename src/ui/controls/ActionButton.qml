import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme

// A text button, optionally with an icon. `primary` is the main action of a
// screen (filled with the accent colour). Enter and Space activate it.
T.Button {
    id: control
    // Colours of the nearest colour scope (the video player has its own).
    readonly property var colors: Theme.colorsFor(control)

    property bool primary: false
    // The main action's colours: the palette's own (a pale button in the
    // "Depth" look), otherwise the accent.
    readonly property color primaryFill: colors.primary !== undefined ? colors.primary : colors.accent
    readonly property color primaryHoverFill: colors.primary !== undefined ? Qt.darker(colors.primary, 1.1) : colors.accentHover
    readonly property color primaryOn: colors.primaryText !== undefined ? colors.primaryText : colors.accentText
    property url iconSource

    implicitWidth: Math.max(implicitBackgroundWidth, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Theme.controlHeight
    leftPadding: Theme.spacingLarge
    rightPadding: Theme.spacingLarge
    spacing: Theme.spacingSmall
    focusPolicy: Qt.StrongFocus
    font.pixelSize: Theme.fontSizeBody
    font.weight: Theme.fontWeightBold

    Keys.onReturnPressed: control.clicked()
    Keys.onEnterPressed: control.clicked()

    contentItem: Row {
        spacing: control.spacing
        opacity: control.enabled ? 1 : 0.45

        Icon {
            visible: control.iconSource.toString().length > 0
            source: control.iconSource
            color: control.primary ? control.primaryOn : colors.text
            size: Theme.iconSize - 4
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: control.text
            font: control.font
            color: control.primary ? control.primaryOn : colors.text
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
        }
    }

    background: Rectangle {
        implicitWidth: 96
        radius: Theme.radius
        color: control.primary
            ? (control.down || control.hovered ? control.primaryHoverFill : control.primaryFill)
            : (control.down ? colors.surfaceHover : control.hovered ? colors.surfaceHover : colors.surfaceRaised)
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        FocusFrame { shown: control.visualFocus }
    }
}
