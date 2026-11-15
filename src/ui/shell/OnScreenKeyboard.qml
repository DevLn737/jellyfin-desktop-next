pragma ComponentBehavior: Bound

import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
// Its own module, for TextEntry.
import Jfd.Ui.Shell

// The on-screen keyboard of TV mode, along the bottom of the window. Opens
// and closes with its TextEntry (focus reaching a field by keys, Select on
// a field, Back). The field keeps the focus; the keyboard shows the text
// above its keys and moves its own highlight with the arrows. Select types
// the highlighted key. Up from the top row closes it, back to the field.
//
// Layouts: English and Russian letters, symbols; digits on top of each.
// The bottom row: Shift (one letter in capitals), symbols or letters, the
// other language, the cursor left and right, space, erase and Done (Enter
// in the field). A gamepad's shoulder buttons (PageUp, PageDown) erase and
// type a space from anywhere on the keyboard.
Rectangle {
    id: keyboard

    required property TextEntry entry
    // "en", "ru" or "symbols"; the letters last used come back after the
    // symbols.
    property string layout: "en"
    property string letters: "en"
    property bool shift: false
    // The highlighted key.
    property int row: 1
    property int column: 0

    readonly property Item field: entry.field
    readonly property int keyHeight: Math.max(Theme.minHitSize, Theme.controlHeight)
    readonly property int gap: Theme.spacingSmall
    // One key's width: twelve and a half of them across.
    readonly property real unit: Math.min(keyHeight * 1.25, (width - 2 * Theme.spacingLarge - 11 * gap) / 12.5)

    readonly property var letterRows: ({
        en: ["qwertyuiop", "asdfghjkl", "zxcvbnm.-@"],
        ru: ["йцукенгшщзхъ", "фывапролджэ", "ячсмитьбюё."],
        symbols: ["!@#$%^&*()", "-_=+[]{};:", "'\",./?\\|~<>"]
    })
    readonly property var rows: {
        const keysOf = (chars) => Array.from(chars).map((c) => ({ text: c }))
        const list = [keysOf("1234567890")]
        for (const chars of letterRows[layout])
            list.push(keysOf(chars))
        list.push([
            { action: "shift", label: "⇧", name: qsTr("Shift"), span: 1.5 },
            { action: "mode", label: layout === "symbols" ? "ABC" : "?123",
              name: layout === "symbols" ? qsTr("Letters") : qsTr("Symbols"), span: 1.5 },
            { action: "language", label: letters === "en" ? "RU" : "EN",
              name: letters === "en" ? qsTr("Russian") : qsTr("English") },
            { action: "left", label: "←", name: qsTr("Cursor left") },
            { action: "space", label: qsTr("Space"), name: qsTr("Space"), span: 3 },
            { action: "right", label: "→", name: qsTr("Cursor right") },
            { action: "erase", label: "⌫", name: qsTr("Erase"), span: 1.5 },
            { action: "done", label: qsTr("Done"), name: qsTr("Done"), span: 2 }
        ])
        return list
    }

    function keyAt(r, c) { return rows[r][c] }

    // The key's centre across the keyboard (rows are centred).
    function centreOf(r, c) {
        const keys = rows[r]
        let total = (keys.length - 1) * gap
        for (const k of keys) total += (k.span || 1) * unit
        let x = (width - total) / 2
        for (let i = 0; i < c; ++i) x += (keys[i].span || 1) * unit + gap
        return x + (keys[c].span || 1) * unit / 2
    }

    // The key of row `r` nearest under the highlighted one.
    function nearestIn(r) {
        const x = centreOf(row, Math.min(column, rows[row].length - 1))
        let best = 0
        for (let c = 1; c < rows[r].length; ++c)
            if (Math.abs(centreOf(r, c) - x) < Math.abs(centreOf(r, best) - x)) best = c
        return best
    }

    function setLayout(name) {
        layout = name
        if (name !== "symbols") letters = name
        column = Math.min(column, rows[row].length - 1)
    }

    function press(key) {
        if (key.text !== undefined) {
            entry.type(shift ? key.text.toUpperCase() : key.text)
            shift = false
            return
        }
        switch (key.action) {
        case "shift": shift = !shift; break
        case "mode": setLayout(layout === "symbols" ? letters : "symbols"); break
        case "language": setLayout(letters === "en" ? "ru" : "en"); break
        case "left": entry.moveCursor(-1); break
        case "right": entry.moveCursor(1); break
        case "space": entry.type(" "); break
        case "erase": entry.erase(); break
        case "done": entry.done(); break
        }
    }

    function handleKey(key) {
        switch (key) {
        case Qt.Key_Left:
            column = Math.max(0, column - 1)
            break
        case Qt.Key_Right:
            column = Math.min(rows[row].length - 1, column + 1)
            break
        case Qt.Key_Up:
            // Above the keys is the field: back to it.
            if (row === 0) {
                entry.hide()
                return
            }
            column = nearestIn(row - 1)
            row = row - 1
            break
        case Qt.Key_Down:
            if (row === rows.length - 1) return
            column = nearestIn(row + 1)
            row = row + 1
            break
        default:
            press(keyAt(row, column))
        }
    }

    // A fresh start on each opening: the first letter, lower case; Latin
    // letters for an address or an e-mail.
    function reset() {
        row = 1
        column = 0
        shift = false
        const latin = Qt.ImhUrlCharactersOnly | Qt.ImhEmailCharactersOnly | Qt.ImhLatinOnly | Qt.ImhDigitsOnly
        const hints = field && field.inputMethodHints !== undefined ? field.inputMethodHints : 0
        layout = (hints & latin) ? "en" : letters
    }

    Connections {
        target: keyboard.entry
        function onOpenChanged() { if (keyboard.entry.open) keyboard.reset() }
        // Done moved on to the next field.
        function onFieldChanged() { if (keyboard.entry.open) keyboard.reset() }
        function onKeyPressed(key, autoRepeat) { keyboard.handleKey(key) }
    }

    objectName: "onScreenKeyboard"
    visible: entry.open
    anchors.left: parent ? parent.left : undefined
    anchors.right: parent ? parent.right : undefined
    anchors.bottom: parent ? parent.bottom : undefined
    // TV mode's safe area under the keys.
    readonly property int safeBottom: parent ? Math.round(parent.height * Theme.safeMargin) : 0
    height: content.implicitHeight + 2 * Theme.spacingLarge + safeBottom
    z: 1000
    color: Theme.popupFill
    border.color: Theme.border
    border.width: 1

    // Clicks on the keyboard stay on it: the field keeps the focus.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
    }

    Column {
        id: content
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Theme.spacingLarge
        width: parent.width - 2 * Theme.spacingLarge
        spacing: keyboard.gap

        // The field's name and its text with the cursor, wherever the field
        // is on the page (the keyboard may cover it).
        Text {
            width: parent.width
            text: keyboard.field && keyboard.field.Accessible.name ? keyboard.field.Accessible.name : ""
            visible: text.length > 0
            horizontalAlignment: Text.AlignHCenter
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeSmall
            elide: Text.ElideRight
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(parent.width, 12 * keyboard.unit + 11 * keyboard.gap)
            height: keyboard.keyHeight
            radius: Theme.radius
            color: Theme.surfaceRaised
            border.color: Theme.focusRing
            border.width: Theme.focusRingWidth

            Text {
                objectName: "keyboardPreview"
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing
                anchors.rightMargin: Theme.spacing
                verticalAlignment: Text.AlignVCenter
                readonly property string shown: keyboard.field ? keyboard.field.displayText : ""
                readonly property int cursor: keyboard.field ? keyboard.field.cursorPosition : 0
                text: shown.slice(0, cursor) + "|" + shown.slice(cursor)
                color: Theme.text
                font.pixelSize: Theme.fontSizeLarge
                elide: Text.ElideLeft
            }
        }

        Repeater {
            model: keyboard.rows
            delegate: Row {
                id: keyRow
                required property var modelData
                required property int index
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: keyboard.gap

                Repeater {
                    model: keyRow.modelData
                    delegate: Rectangle {
                        id: keyCap
                        required property var modelData
                        required property int index
                        readonly property bool current: keyboard.row === keyRow.index && keyboard.column === index
                        readonly property bool special: modelData.action !== undefined
                        objectName: "key_" + (special ? modelData.action : modelData.text)
                        width: (modelData.span || 1) * keyboard.unit + ((modelData.span || 1) - 1) * keyboard.gap
                        height: keyboard.keyHeight
                        radius: Theme.radius
                        color: current ? Theme.accent
                             : modelData.action === "shift" && keyboard.shift ? Theme.surfaceHover
                             : special ? Theme.surface : Theme.surfaceRaised
                        Accessible.role: Accessible.Button
                        Accessible.name: special ? modelData.name : modelData.text

                        Text {
                            anchors.centerIn: parent
                            text: keyCap.special ? keyCap.modelData.label
                                : keyboard.shift ? keyCap.modelData.text.toUpperCase() : keyCap.modelData.text
                            color: keyCap.current ? Theme.accentText : Theme.text
                            font.pixelSize: Theme.fontSizeLarge
                            font.weight: keyCap.special ? Theme.fontWeightBold : Theme.fontWeightNormal
                        }
                        FocusFrame { shown: keyCap.current }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                keyboard.row = keyRow.index
                                keyboard.column = keyCap.index
                                keyboard.press(keyCap.modelData)
                            }
                        }
                    }
                }
            }
        }
    }
}
