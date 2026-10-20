import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme

// A row in a vertical list: optional icon, title, subtitle and text on the
// right (an episode, a group member, a server).
T.ItemDelegate {
    id: item

    property string subtitle
    property string trailingText
    property url iconSource

    implicitWidth: 400
    implicitHeight: Math.max(Theme.controlHeight + Theme.spacingSmall, content.implicitHeight + 2 * Theme.spacingSmall)
    leftPadding: Theme.spacing
    rightPadding: Theme.spacing
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    Accessible.name: text

    Keys.onReturnPressed: item.clicked()
    Keys.onEnterPressed: item.clicked()

    contentItem: Item {
        implicitHeight: content.implicitHeight

        Row {
            id: content
            anchors.left: parent.left
            anchors.right: trailing.left
            anchors.rightMargin: Theme.spacing
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacing

            Icon {
                visible: item.iconSource.toString().length > 0
                source: item.iconSource
                anchors.verticalCenter: parent.verticalCenter
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: content.width - (item.iconSource.toString().length > 0 ? Theme.iconSize + content.spacing : 0)
                Text {
                    width: parent.width
                    text: item.text
                    color: item.enabled ? Theme.text : Theme.textDisabled
                    font.pixelSize: Theme.fontSizeBody
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    visible: item.subtitle.length > 0
                    text: item.subtitle
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                    elide: Text.ElideRight
                }
            }
        }
        Text {
            id: trailing
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: item.trailingText
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeSmall
        }
    }

    background: Rectangle {
        radius: Theme.radius
        color: item.down ? Theme.surfaceHover : item.hovered || item.highlighted ? Theme.surfaceRaised : "transparent"
        FocusFrame { shown: item.visualFocus; inset: 0 }
    }
}
