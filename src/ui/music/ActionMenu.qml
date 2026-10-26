import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme
import Jfd.Ui.Controls

// A small menu of actions that opens at the pointer or under a button.
// `entries` are the texts; `activated(index)` says which was chosen. An
// entry may be disabled through `disabledIndices`.
T.Popup {
    id: menu

    property var entries: []
    property var disabledIndices: []

    signal activated(int index)

    parent: T.Overlay.overlay
    padding: Theme.spacingTiny
    width: 240
    modal: false
    focus: true
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

    background: Rectangle {
        radius: Theme.radius
        color: Theme.surface
        border.color: Theme.border
    }

    contentItem: Column {
        Repeater {
            model: menu.entries
            delegate: ListItem {
                required property string modelData
                required property int index
                width: menu.availableWidth
                text: modelData
                enabled: menu.disabledIndices.indexOf(index) < 0
                onClicked: {
                    menu.close()
                    menu.activated(index)
                }
            }
        }
    }
}
