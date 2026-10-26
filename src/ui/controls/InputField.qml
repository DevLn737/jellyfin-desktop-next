import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme

// A single-line text field with a label above and an error below.
// Set `password` for hidden input. `accepted` fires on Enter.
FocusScope {
    id: root
    // Colours of the nearest colour scope (the video player has its own).
    readonly property var colors: Theme.colorsFor(root)

    property string label
    property alias text: field.text
    property alias placeholderText: field.placeholderText
    property string errorText
    property bool password: false
    property alias inputMethodHints: field.inputMethodHints
    property alias readOnly: field.readOnly
    readonly property alias field: field

    signal accepted()

    implicitWidth: 320
    implicitHeight: column.implicitHeight
    activeFocusOnTab: true
    Accessible.name: label

    Column {
        id: column
        width: parent.width
        spacing: Theme.spacingTiny

        Text {
            visible: root.label.length > 0
            text: root.label
            color: colors.textSecondary
            font.pixelSize: Theme.fontSizeSmall
        }

        T.TextField {
            id: field
            width: parent.width
            implicitHeight: Theme.controlHeight
            focus: true
            leftPadding: Theme.spacing
            rightPadding: Theme.spacing
            verticalAlignment: TextInput.AlignVCenter
            color: colors.text
            placeholderTextColor: colors.textDisabled
            selectionColor: colors.accent
            selectedTextColor: colors.accentText
            font.pixelSize: Theme.fontSizeBody
            echoMode: root.password ? TextInput.Password : TextInput.Normal
            Accessible.name: root.label.length > 0 ? root.label : placeholderText
            onAccepted: root.accepted()

            // Placeholder drawn by hand: the template does not draw one.
            Text {
                anchors.fill: parent
                leftPadding: field.leftPadding
                rightPadding: field.rightPadding
                verticalAlignment: Text.AlignVCenter
                text: field.placeholderText
                color: field.placeholderTextColor
                font: field.font
                visible: field.length === 0 && !field.preeditText
                elide: Text.ElideRight
            }

            background: Rectangle {
                radius: Theme.radius
                color: colors.surfaceRaised
                border.width: field.activeFocus || root.errorText.length > 0 ? Theme.focusRingWidth : 1
                border.color: root.errorText.length > 0 ? colors.error
                    : field.activeFocus ? colors.focusRing : colors.border
            }
        }

        Text {
            visible: root.errorText.length > 0
            width: parent.width
            text: root.errorText
            color: colors.error
            font.pixelSize: Theme.fontSizeSmall
            wrapMode: Text.Wrap
        }
    }
}
