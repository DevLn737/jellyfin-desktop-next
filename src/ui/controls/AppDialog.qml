import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme

// A modal question or message. `acceptText` is the primary button;
// `rejectText` (empty for none) closes it. Esc rejects.
T.Dialog {
    id: dialog

    property string text
    property string acceptText: qsTr("OK")
    property string rejectText: qsTr("Cancel")
    property bool destructive: false

    parent: T.Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    width: Math.min(460, parent ? parent.width - 2 * Theme.spacingHuge : 460)
    padding: Theme.spacingLarge
    topPadding: Theme.spacingLarge
    closePolicy: T.Popup.CloseOnEscape

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }

    background: Rectangle {
        radius: Theme.radius
        color: Theme.surface
        border.color: Theme.border
    }

    T.Overlay.modal: Rectangle { color: Theme.scrim }

    header: Heading {
        text: dialog.title
        visible: dialog.title.length > 0
        leftPadding: Theme.spacingLarge
        rightPadding: Theme.spacingLarge
        topPadding: Theme.spacingLarge
        font.pixelSize: Theme.fontSizeLarge
    }

    contentItem: Text {
        text: dialog.text
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeBody
        wrapMode: Text.Wrap
    }

    footer: Item {
        implicitHeight: buttons.implicitHeight + Theme.spacingLarge
        Row {
            id: buttons
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingLarge
            spacing: Theme.spacingSmall

            ActionButton {
                visible: dialog.rejectText.length > 0
                text: dialog.rejectText
                onClicked: dialog.reject()
            }
            ActionButton {
                id: acceptButton
                text: dialog.acceptText
                primary: true
                focus: true
                onClicked: dialog.accept()
            }
        }
    }

    onOpened: acceptButton.forceActiveFocus(Qt.TabFocusReason)
}
