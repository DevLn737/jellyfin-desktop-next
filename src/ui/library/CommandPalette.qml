pragma ComponentBehavior: Bound

import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// Ctrl+K: a palette over any screen. A text field, the commands that are
// available now (CommandRegistry; each module registers its own) and, below
// them, the media found by that text. Up and Down move, Enter does it, Esc
// closes. The last command used comes first the next time.
FocusScope {
    id: palette

    property bool opened: false
    property int current: 0
    property var commandList: []
    property string lastCommand
    property var search: null
    property Item previousFocus: null

    readonly property int mediaCount: mediaRepeater.count
    readonly property int total: commandList.length + mediaCount

    function open() {
        if (!search)
            search = Library.createSearch(palette)
        previousFocus = Window.window ? Window.window.activeFocusItem : null
        opened = true
        input.text = ""
        refresh()
        current = 0
        input.field.forceActiveFocus()
    }

    function close() {
        if (!opened)
            return
        opened = false
        if (search)
            search.text = ""
        const target = previousFocus
        previousFocus = null
        if (target)
            target.forceActiveFocus()
    }

    function toggle() {
        if (opened)
            close()
        else
            open()
    }

    function refresh() {
        const filter = input.text.trim()
        let list = CommandRegistry.commands(filter)
        if (filter.length === 0 && lastCommand.length > 0) {
            const at = list.findIndex(command => command.id === lastCommand)
            if (at > 0)
                list = [list[at]].concat(list.slice(0, at), list.slice(at + 1))
        }
        commandList = list
        if (search && search.text !== filter)
            search.text = filter
    }

    function activate(index) {
        if (index < 0 || index >= total)
            return
        if (index < commandList.length) {
            const id = commandList[index].id
            lastCommand = id
            close()
            CommandRegistry.run(id)
            return
        }
        const item = mediaRepeater.itemAt(index - commandList.length)
        if (!item)
            return
        const route = Library.routeFor(item.itemId, item.itemType)
        close()
        Navigator.navigate(route)
    }

    visible: opened
    z: 100

    Connections {
        target: CommandRegistry
        function onChanged() { if (palette.opened) palette.refresh() }
    }

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Down) {
            palette.current = Math.min(palette.total - 1, palette.current + 1)
            event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            palette.current = Math.max(0, palette.current - 1)
            event.accepted = true
        } else if (event.key === Qt.Key_Escape) {
            palette.close()
            event.accepted = true
        } else if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
            // Focus stays in the field.
            event.accepted = true
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.scrim
        TapHandler { onTapped: palette.close() }
    }

    Rectangle {
        id: panel
        objectName: "commandPalette"
        width: Math.min(560, palette.width - 2 * Theme.spacingLarge)
        x: (palette.width - width) / 2
        y: Math.min(palette.height * 0.12, Math.max(0, palette.height - height - Theme.spacingLarge))
        height: Math.min(palette.height - 2 * Theme.spacingLarge,
                         input.implicitHeight + Theme.spacingSmall + content.implicitHeight + 2 * Theme.spacing)
        radius: Theme.radius
        color: Theme.surface
        border.color: Theme.border
        border.width: 1

        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Commands and search")

        Column {
            id: column
            anchors.fill: parent
            anchors.margins: Theme.spacing
            spacing: Theme.spacingSmall

            InputField {
                id: input
                objectName: "paletteInput"
                width: parent.width
                focus: true
                placeholderText: qsTr("Type a command or search your media")
                inputMethodHints: Qt.ImhNoPredictiveText
                Accessible.name: qsTr("Commands and search")
                onTextChanged: { palette.refresh(); palette.current = 0 }
                onAccepted: palette.activate(palette.current)
            }

            Flickable {
                id: list
                width: parent.width
                height: Math.max(0, panel.height - input.implicitHeight - 2 * Theme.spacing - Theme.spacingSmall)
                contentHeight: content.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                interactive: contentHeight > height

                // Keeps the chosen row in view.
                function reveal(item) {
                    if (!item)
                        return
                    const top = item.y
                    const bottom = item.y + item.height
                    if (top < contentY)
                        contentY = top
                    else if (bottom > contentY + height)
                        contentY = bottom - height
                }

                Column {
                    id: content
                    width: list.width

                    Text {
                        visible: palette.commandList.length > 0
                        text: qsTr("Commands")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeSmall
                        font.weight: Theme.fontWeightBold
                        topPadding: Theme.spacingTiny
                        bottomPadding: Theme.spacingTiny
                    }
                    Repeater {
                        id: commandRepeater
                        model: palette.commandList
                        ListItem {
                            required property var modelData
                            required property int index
                            width: content.width
                            text: modelData.title
                            highlighted: palette.current === index
                            onHighlightedChanged: if (highlighted) list.reveal(this)
                            onClicked: palette.activate(index)
                        }
                    }

                    Text {
                        visible: palette.mediaCount > 0
                        text: qsTr("Media")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeSmall
                        font.weight: Theme.fontWeightBold
                        topPadding: Theme.spacingSmall
                        bottomPadding: Theme.spacingTiny
                    }
                    Repeater {
                        id: mediaRepeater
                        model: palette.search ? palette.search.results : null
                        ListItem {
                            id: media
                            required property var model
                            required property int index
                            readonly property string itemId: model.itemId || ""
                            readonly property string itemType: model.type || ""
                            readonly property int position: palette.commandList.length + index
                            width: content.width
                            text: model.name || ""
                            subtitle: model.subtitle || ""
                            highlighted: palette.current === position
                            onHighlightedChanged: if (highlighted) list.reveal(this)
                            onClicked: palette.activate(position)
                        }
                    }

                    Text {
                        width: content.width
                        visible: palette.total === 0 && input.text.trim().length > 0
                                 && !(palette.search && palette.search.loading)
                        text: palette.search && palette.search.errorText.length > 0
                              ? palette.search.errorText : qsTr("Nothing found.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeBody
                        padding: Theme.spacing
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }
}
