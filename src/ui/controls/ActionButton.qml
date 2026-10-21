import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme

// A text button, optionally with an icon. `primary` is the main action of a
// screen (filled with the accent colour). Enter and Space activate it.
T.Button {
    id: control

    property bool primary: false
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
            size: Theme.iconSize - 4
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: control.text
            font: control.font
            color: control.primary ? Theme.accentText : Theme.text
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
        }
    }

    background: Rectangle {
        implicitWidth: 96
        radius: Theme.radius
        color: control.primary
            ? (control.down || control.hovered ? Theme.accentHover : Theme.accent)
            : (control.down ? Theme.surfaceHover : control.hovered ? Theme.surfaceHover : Theme.surfaceRaised)
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        FocusFrame { shown: control.visualFocus }
    }
}
