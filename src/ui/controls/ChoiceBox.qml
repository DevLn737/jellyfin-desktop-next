import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme

// A drop-down list. The model is a list of { value, label } objects;
// `currentValue` is the chosen value. Up and Down move the focus on to the
// control above or below (a column of settings would otherwise be a trap
// for arrows and a gamepad, and passing over a setting would change it);
// Enter (the gamepad's A) opens the list, where Up/Down and Enter choose
// and Escape (B) closes it without a change.
T.ComboBox {
    id: control
    // Colours of the nearest colour scope (the video player has its own).
    readonly property var colors: Theme.colorsFor(control)
    // The arrows the shell's SpatialNavigation takes before this control.
    readonly property var navigationKeys: !popup.visible ? [Qt.Key_Up, Qt.Key_Down] : []

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

    // Wide enough for the chosen entry's text (within reason), so the box
    // does not cut it.
    implicitWidth: Math.max(220, Math.min(360, labelMetrics.advanceWidth(control.displayText) + leftPadding + rightPadding + 2))
    implicitHeight: Theme.controlHeight
    leftPadding: Theme.spacing
    rightPadding: Theme.spacing + Theme.iconSize
    focusPolicy: Qt.StrongFocus
    textRole: "label"
    valueRole: "value"
    font.pixelSize: Theme.fontSizeBody

    // The widest label, so the open list shows every one in full.
    FontMetrics { id: labelMetrics; font: control.font }
    readonly property real widestLabel: {
        let widest = 0
        const items = control.model
        const count = items ? (items.length !== undefined ? items.length : 0) : 0
        for (let i = 0; i < count; ++i) {
            const label = items[i] && items[i].label !== undefined ? items[i].label : items[i]
            widest = Math.max(widest, labelMetrics.advanceWidth(String(label)))
        }
        return Math.ceil(widest)
    }

    delegate: T.ItemDelegate {
        id: option
        objectName: "choiceOption"
        required property var modelData
        required property int index
        width: ListView.view ? ListView.view.width : implicitWidth
        implicitHeight: Math.max(Theme.controlHeight, optionText.implicitHeight + 2 * Theme.spacingSmall)
        leftPadding: Theme.spacing
        rightPadding: Theme.spacing
        highlighted: control.highlightedIndex === index
        hoverEnabled: true
        contentItem: Text {
            id: optionText
            text: option.modelData.label
            color: colors.text
            font: control.font
            verticalAlignment: Text.AlignVCenter
            // Wider than the window allows: on more lines, never cut.
            wrapMode: Text.Wrap
        }
        background: Item {
            Rectangle {
                visible: option.index > 0
                x: Theme.spacingSmall
                width: parent.width - 2 * Theme.spacingSmall
                height: 1
                color: Qt.alpha(colors.text, 0.14)
            }
            Rectangle {
                objectName: "choiceHighlight"
                anchors.fill: parent
                radius: Theme.radius
                color: option.highlighted ? Qt.alpha(colors.accent, 0.28) : "transparent"
                border.width: option.highlighted ? 1 : 0
                border.color: Qt.alpha(colors.accent, 0.7)
                Rectangle {
                    visible: option.highlighted
                    anchors.verticalCenter: parent.verticalCenter
                    width: 3
                    height: parent.height - 2 * Theme.spacingSmall
                    radius: 1.5
                    color: colors.accent
                }
            }
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
        objectName: "choicePopup"
        y: control.height + Theme.spacingTiny
        // Wider than the box: right edges lined up (the boxes sit at the
        // right of their rows).
        x: width > control.width ? control.width - width : 0
        // As wide as the longest entry (at least the box), inside the window;
        // `margins` keeps it there, sliding it left if need be.
        width: {
            const window = control.Window.window
            const room = window ? window.width - 2 * Theme.spacingSmall : 100000
            const wanted = control.widestLabel + 2 * Theme.spacing + 2 * padding + 2
            return Math.min(room, Math.max(control.width, wanted))
        }
        // A templated Popup has no height of its own: the list's plus the
        // padding (without it the list is a few pixels short and scrolls,
        // and moving the pointer over the entries jerks it up and down).
        implicitHeight: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, 360)
        padding: Theme.spacingTiny
        margins: Theme.spacingSmall

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            // Only a list longer than the popup scrolls to the entry.
            highlightFollowsCurrentItem: false
            highlightRangeMode: ListView.NoHighlightRange
            boundsBehavior: Flickable.StopAtBounds
            onCurrentIndexChanged: if (contentHeight > height && currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
        }

        background: Rectangle {
            radius: Theme.radiusLarge
            color: colors.surfaceRaised
            border.color: colors.border
        }
    }
}
