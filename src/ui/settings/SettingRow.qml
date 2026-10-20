import QtQuick
import Jfd.Ui.Theme
import QtQuick.Templates as T
import Jfd.Ui.Controls

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

    readonly property bool ranged: maximum > minimum

    implicitHeight: Math.max(texts.implicitHeight, editor.implicitHeight) + 2 * Theme.spacingSmall
    objectName: "setting_" + key
    Accessible.name: label

    function formatNumber(number) {
        const decimals = row.type === "int" ? 0 : (row.step < 0.1 ? 2 : 1)
        return Number(number).toLocaleString(Qt.locale(), "f", decimals) + (row.unit.length > 0 ? " " + row.unit : "")
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: row.activeFocus ? Theme.surface : "transparent"
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
            text: row.label
            color: Theme.text
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.Wrap
        }
        Text {
            width: parent.width
            visible: text.length > 0
            text: row.description
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeSmall
            wrapMode: Text.Wrap
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
            onClicked: AppSettings.trigger(row.key)
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
                const value = row.type === "string" ? text : Number(text)
                errorText = AppSettings.setValue(row.key, value) ? "" : qsTr("This value is not allowed.")
            }
            onActiveFocusChanged: if (!activeFocus) accepted()
        }
    }
}
