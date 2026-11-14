import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell

// Settings > Gamepad > Check the buttons: which gamepad is connected, and
// the action of each press ("press any button"). The presses only show
// here; Back twice in a row, Done or Esc end the check (GamepadStatus).
AppDialog {
    id: check
    objectName: "gamepadCheck"

    title: qsTr("Check the gamepad")
    rejectText: ""
    acceptText: qsTr("Done")

    contentItem: Column {
        spacing: Theme.spacing

        Row {
            spacing: Theme.spacingSmall
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                source: Icons.gamepad
                opacity: GamepadStatus.connected ? 1 : 0.4
            }
            Text {
                objectName: "gamepadName"
                anchors.verticalCenter: parent.verticalCenter
                text: GamepadStatus.connected ? GamepadStatus.controllerNames.join(", ")
                                              : qsTr("No gamepad is connected.")
                color: Theme.text
                font.pixelSize: Theme.fontSizeBody
            }
        }

        Text {
            width: parent.width
            text: qsTr("Press any button on the gamepad: its action shows below. Press Back twice to finish.")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.Wrap
        }

        Text {
            objectName: "gamepadPressed"
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: GamepadStatus.pressCount > 0 ? GamepadStatus.lastAction : "…"
            color: GamepadStatus.pressCount > 0 ? Theme.text : Theme.textDisabled
            font.pixelSize: Theme.fontSizeHeadline
            font.weight: Theme.fontWeightBold
        }
    }

    Connections {
        target: GamepadStatus
        function onCheckingChanged() {
            if (GamepadStatus.checking)
                check.open()
            else
                check.close()
        }
    }
    // The focus goes back where it was (the settings row), so the gamepad
    // goes on from there.
    property Item returnFocus: null
    onAboutToShow: returnFocus = parent ? parent.Window.activeFocusItem : null
    onClosed: {
        GamepadStatus.stopCheck()
        if (returnFocus)
            returnFocus.forceActiveFocus(Qt.TabFocusReason)
        returnFocus = null
    }
}
