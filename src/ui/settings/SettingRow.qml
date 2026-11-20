import QtQuick
import Jfd.Ui.Theme
import QtQuick.Templates as T
import Jfd.Ui.Controls
import Jfd.Ui.Shell

// One setting: label and description on the left, the editor for its type
// on the right. Changes are written at once.
FocusScope {
    id: row

    required property string key
    required property string type
    required property string label
    required property string description
    required property var value
    required property var choices
    required property real minimum
    required property real maximum
    required property real step
    required property string unit
    required property bool requiresRestart
    // Actions: asked before triggering (sign out); empty runs at once.
    required property string confirmation

    readonly property bool ranged: maximum > minimum
    // The search's words, marked where they are in the label and description.
    property string highlight

    function escaped(text) {
        return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    }
    function marked(text) {
        const words = row.highlight.trim().toLowerCase().split(/\s+/).filter((w) => w.length > 0)
        if (words.length === 0)
            return escaped(text)
        const lower = text.toLowerCase()
        // Where each character is inside a match.
        const hit = new Array(text.length).fill(false)
        for (const word of words) {
            for (let at = lower.indexOf(word); at >= 0; at = lower.indexOf(word, at + word.length))
                for (let i = at; i < at + word.length; ++i)
                    hit[i] = true
        }
        let out = ""
        for (let i = 0; i < text.length; ++i) {
            if (hit[i] && (i === 0 || !hit[i - 1]))
                out += "<font color=\"" + Theme.accent + "\"><b>"
            out += escaped(text.charAt(i))
            if (hit[i] && (i === text.length - 1 || !hit[i + 1]))
                out += "</b></font>"
        }
        return out
    }

    implicitHeight: Math.max(texts.implicitHeight, editor.implicitHeight) + 2 * Theme.spacingSmall
    objectName: "setting_" + key
    Accessible.name: label

    function formatNumber(number) {
        const decimals = row.type === "int" ? 0 : (row.step < 0.1 ? 2 : 1)
        return Number(number).toLocaleString(Qt.locale(), "f", decimals) + (row.unit.length > 0 ? " " + row.unit : "")
    }

    // "1,5" in Russian, "1.5" anywhere: NaN (refused) only when neither reads.
    function parseNumber(text) {
        try {
            return Number.fromLocaleString(Qt.locale(), text.trim())
        } catch (error) {
            return Number(text.trim())
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: row.activeFocus ? Theme.surfaceHover : "transparent"
        border.width: row.activeFocus ? 1 : 0
        border.color: Qt.alpha(Theme.focusRing, 0.6)
    }

    Column {
        id: texts
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacing
        anchors.right: editor.left
        anchors.rightMargin: Theme.spacingLarge
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        Text {
            width: parent.width
            text: row.marked(row.label)
            textFormat: Text.StyledText
            color: Theme.text
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.Wrap
        }
        Text {
            width: parent.width
            visible: row.description.length > 0
            text: row.marked(row.description)
            textFormat: Text.StyledText
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeSmall
            wrapMode: Text.Wrap
        }
        // The gamepad switch says which gamepad is connected, with the
        // shell's gamepad sign.
        Row {
            objectName: "gamepadState"
            visible: row.key === "input/gamepad" && GamepadStatus.available
            spacing: Theme.spacingSmall
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                size: Math.round(Theme.iconSize * 0.8)
                source: Icons.gamepad
                color: GamepadStatus.connected ? Theme.text : Theme.textDisabled
                opacity: GamepadStatus.connected ? 1 : 0.5
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: GamepadStatus.connected ? qsTr("Connected: %1").arg(GamepadStatus.controllerNames.join(", "))
                                              : qsTr("No gamepad is connected.")
                color: GamepadStatus.connected ? Theme.text : Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
            }
        }
        Text {
            width: parent.width
            visible: row.requiresRestart
            text: qsTr("Takes effect after restarting the app.")
            color: Theme.textDisabled
            font.pixelSize: Theme.fontSizeSmall
            wrapMode: Text.Wrap
        }
    }

    Loader {
        id: editor
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacing
        anchors.verticalCenter: parent.verticalCenter
        focus: true
        sourceComponent: {
            switch (row.type) {
            case "bool": return boolEditor
            case "choice": return choiceEditor
            case "action": return actionEditor
            case "folder": return folderEditor
            case "int":
            case "double": return row.ranged ? sliderEditor : textEditor
            default: return textEditor
            }
        }
    }

    Component {
        id: boolEditor
        ToggleSwitch {
            objectName: "editor"
            focus: true
            checked: row.value === true
            Accessible.name: row.label
            onToggled: AppSettings.setValue(row.key, checked)
        }
    }

    Component {
        id: choiceEditor
        ChoiceBox {
            objectName: "editor"
            focus: true
            model: row.choices
            Accessible.name: row.label
            Component.onCompleted: currentIndex = indexOfValue(row.value)
            onActivated: AppSettings.setValue(row.key, currentValue)
            Connections {
                target: row
                function onValueChanged() { currentIndex = indexOfValue(row.value) }
            }
        }
    }

    Component {
        id: actionEditor
        ActionButton {
            objectName: "editor"
            focus: true
            text: row.label
            onClicked: {
                if (row.confirmation.length > 0) confirm.open()
                else AppSettings.trigger(row.key)
            }

            AppDialog {
                id: confirm
                objectName: "confirmAction"
                title: row.label
                text: row.confirmation
                acceptText: row.label
                destructive: true
                onAccepted: AppSettings.trigger(row.key)
            }
        }
    }

    Component {
        id: sliderEditor
        Row {
            spacing: Theme.spacing
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: 64
                horizontalAlignment: Text.AlignRight
                text: row.formatNumber(slider.value)
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
            }
            ValueSlider {
                id: slider
                objectName: "editor"
                focus: true
                from: row.minimum
                to: row.maximum
                stepSize: row.step
                snapMode: T.Slider.SnapAlways
                value: Number(row.value)
                Accessible.name: row.label
                onMoved: AppSettings.setValue(row.key, row.type === "int" ? Math.round(value) : value)
            }
        }
    }

    Component {
        id: textEditor
        InputField {
            objectName: "editor"
            focus: true
            width: 260
            text: row.value === undefined ? "" : String(row.value)
            Accessible.name: row.label
            inputMethodHints: row.type === "string" ? Qt.ImhNone : Qt.ImhFormattedNumbersOnly
            onAccepted: {
                const value = row.type === "string" ? text : row.parseNumber(text)
                errorText = AppSettings.setValue(row.key, value) ? "" : qsTr("This value is not allowed.")
            }
            onActiveFocusChanged: if (!activeFocus) accepted()
        }
    }

    // A folder: the path (typed, kept when it is absolute) and "Choose..."
    // with the system's folder picker. The picker is loaded on first use,
    // so the page works without the dialogs module.
    Component {
        id: folderEditor
        Row {
            spacing: Theme.spacingSmall
            InputField {
                id: folderField
                objectName: "editor"
                focus: true
                width: 320
                text: row.value === undefined ? "" : String(row.value)
                Accessible.name: row.label
                onAccepted: errorText = AppSettings.setValue(row.key, text) ? "" : qsTr("Enter the full path of a folder.")
                onActiveFocusChanged: if (!activeFocus && text !== String(row.value)) accepted()
                KeyNavigation.right: chooseButton
            }
            ActionButton {
                id: chooseButton
                objectName: "chooseFolder"
                anchors.verticalCenter: folderField.verticalCenter
                text: qsTr("Choose…")
                Accessible.name: qsTr("Choose a folder for %1").arg(row.label)
                onClicked: {
                    if (picker.status === Loader.Null)
                        picker.source = "FolderPicker.qml"
                    if (picker.item) {
                        picker.item.currentFolder = AppSettings.folderUrl(String(row.value))
                        picker.item.open()
                    }
                }
            }
            Loader {
                id: picker
                Connections {
                    target: picker.item
                    ignoreUnknownSignals: true
                    function onChosen(url) {
                        const path = AppSettings.localPath(url)
                        if (path.length > 0 && AppSettings.setValue(row.key, path))
                            folderField.text = path
                    }
                }
            }
        }
    }
}
