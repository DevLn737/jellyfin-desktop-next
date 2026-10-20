import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme

// A drop-down list. The model is a list of { value, label } objects;
// `currentValue` is the chosen value. Up/Down change it when focused.
T.ComboBox {
    id: control

    implicitWidth: 220
    implicitHeight: Theme.controlHeight
    leftPadding: Theme.spacing
    rightPadding: Theme.spacing + Theme.iconSize
    focusPolicy: Qt.StrongFocus
    textRole: "label"
    valueRole: "value"
    font.pixelSize: Theme.fontSizeBody

    delegate: T.ItemDelegate {
        id: option
        required property var modelData
        required property int index
        width: ListView.view ? ListView.view.width : implicitWidth
        implicitHeight: Theme.controlHeight
        leftPadding: Theme.spacing
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: option.modelData.label
            color: Theme.text
            font: control.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: option.highlighted ? Theme.surfaceHover : "transparent"
        }
    }

    indicator: Text {
        x: control.width - width - Theme.spacing
        anchors.verticalCenter: parent.verticalCenter
        text: "▾"
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeBody
    }

    contentItem: Text {
        text: control.displayText
        color: control.enabled ? Theme.text : Theme.textDisabled
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: Theme.radius
        color: control.hovered ? Theme.surfaceHover : Theme.surfaceRaised
        border.color: Theme.border
        FocusFrame { shown: control.visualFocus }
    }

    popup: T.Popup {
        y: control.height + Theme.spacingTiny
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight, 320)
        padding: Theme.spacingTiny

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
        }

        background: Rectangle {
            radius: Theme.radius
            color: Theme.surfaceRaised
            border.color: Theme.border
        }
    }
}
