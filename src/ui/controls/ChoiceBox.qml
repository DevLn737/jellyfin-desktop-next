import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme

// A drop-down list. The model is a list of { value, label } objects;
// `currentValue` is the chosen value. Up/Down change it when focused;
// Enter opens the list, where Up/Down and Enter choose. In TV mode Up and
// Down move the focus on to the next control instead (a column of settings
// would otherwise be a trap), so the list is opened with Enter.
T.ComboBox {
    id: control
    // Colours of the nearest colour scope (the video player has its own).
    readonly property var colors: Theme.colorsFor(control)
    // The arrows the shell's SpatialNavigation takes before this control.
    readonly property var navigationKeys: Theme.tvMode && !popup.visible ? [Qt.Key_Up, Qt.Key_Down] : []

    // Enter opened the list: its release must not choose at once.
    property bool openedByEnter: false

    function openOnEnter(event) {
        // Open: the list's own Enter chooses the highlighted entry.
        if (popup.visible) {
            event.accepted = false
            return
        }
        openedByEnter = true
        popup.open()
        event.accepted = true
    }
    Keys.onReturnPressed: (event) => openOnEnter(event)
    Keys.onEnterPressed: (event) => openOnEnter(event)
    Keys.onReleased: (event) => {
        const enter = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
        event.accepted = enter && openedByEnter
        if (enter)
            openedByEnter = false
    }

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
            color: colors.text
            font: control.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: option.highlighted ? colors.surfaceHover : "transparent"
        }
    }

    indicator: Text {
        x: control.width - width - Theme.spacing
        anchors.verticalCenter: parent.verticalCenter
        text: "▾"
        color: colors.textSecondary
        font.pixelSize: Theme.fontSizeBody
    }

    contentItem: Text {
        text: control.displayText
        color: control.enabled ? colors.text : colors.textDisabled
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: Theme.radius
        color: control.hovered ? colors.surfaceHover : colors.surfaceRaised
        border.color: colors.border
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
            color: colors.surfaceRaised
            border.color: colors.border
        }
    }
}
