import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Templates as T
import Jfd.Ui.Theme
import Jfd.Ui.Controls
// Its own module, for the attached SpatialNavigation.
import Jfd.Ui.Shell

// The window's content: the navigation bar at the top (TopNav), the page
// stack driven by Navigator under it, and the toast. Back: Esc, Backspace
// outside text fields, Alt+Left and the mouse back button; on a page with
// nowhere to go back to, Back moves the focus to the bar. F11 toggles full
// screen; Esc leaves it first. Arrows nobody else takes move the focus to
// the nearest item in that direction, PageUp and PageDown by a page
// (SpatialNavigation); the Menu key, when the focused item has no menu of
// its own, goes to the bar and back. In TV mode the pages keep 5 % away from the screen's edges
// (Theme.safeMargin). In TV mode a text field reached by keys, or Select on
// it, opens the on-screen keyboard over everything (TextEntry).
FocusScope {
    id: shell

    readonly property alias stack: stack
    readonly property alias toast: toast
    // The strip under the pages for the mini player. A module puts its item
    // in it (`parent: shell.miniPlayerSlot`); the strip is as tall as its
    // content and shows wherever the shell shows its bar, so not on the
    // sign-in page and not in the video player.
    readonly property alias miniPlayerSlot: miniPlayerSlot
    readonly property Item currentPage: stack.currentItem
    readonly property string title: currentPage && currentPage.title ? currentPage.title : ""
    // The account in the bar (optional), and whether the server is out of
    // reach (Downloads then come into the bar).
    property alias accountName: topNav.accountName
    property alias accountDetail: topNav.accountDetail
    property alias offline: topNav.offline
    // The account menu's Sign out, confirmed.
    signal signOutRequested()
    // Off while something under the shell must show through (the video with
    // HDR10 output).
    property bool backgroundVisible: true

    // Ctrl+K (or Ctrl+/): the application shows the command palette.
    signal paletteRequested()

    // The K key whatever the keyboard layout: its native codes, as well as
    // the letter (a Russian layout gives another letter on that key).
    function isPaletteKey(event) {
        if (!(event.modifiers & Qt.ControlModifier) || (event.modifiers & (Qt.AltModifier | Qt.ShiftModifier)))
            return false
        if (event.key === Qt.Key_K || event.key === Qt.Key_Slash)
            return true
        switch (Qt.platform.os) {
        case "windows": return event.nativeVirtualKey === 0x4B
        // X11 and Wayland: the evdev code of K (37) plus 8.
        case "linux": return event.nativeScanCode === 45
        default: return false
        }
    }

    function showMessage(text) { toast.show(text, "info") }
    function showError(text) { toast.show(text, "error") }
    // A message with a button that opens url in the browser.
    function showLink(text, linkText, url) { toast.show(text, "info", linkText, url) }

    function toggleFullScreen() {
        const window = shell.Window.window
        if (!window)
            return
        window.visibility = window.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
    }

    // The Menu key: the bar's current entry, or back to the page.
    function toggleNavFocus() {
        if (!topNav.visible)
            return false
        if (topNav.activeFocus) {
            stack.forceActiveFocus(Qt.TabFocusReason)
            return true
        }
        topNav.focusCurrentEntry()
        return true
    }

    function goBack() {
        const window = shell.Window.window
        if (window && window.visibility === Window.FullScreen) {
            window.visibility = Window.Windowed
            return true
        }
        if (Navigator.back())
            return true
        // A root page: Back goes up to the bar (and from there nowhere).
        if (topNav.visible && !topNav.activeFocus) {
            topNav.focusCurrentEntry()
            return true
        }
        return false
    }

    focus: true
    SpatialNavigation.enabled: true

    // The size classes follow the window's width (logical pixels).
    Binding { target: Theme; property: "windowWidth"; value: shell.width }

    Rectangle {
        anchors.fill: parent
        color: Theme.background
        visible: shell.backgroundVisible
    }

    // Everything that can take the focus, inside the TV's safe area (the
    // video player and the sign-in page have no bar and place their
    // controls themselves).
    Item {
        id: frame
        objectName: "safeArea"
        readonly property real share: Navigator.chromeVisible ? Theme.safeMargin : 0
        anchors.fill: parent
        anchors.leftMargin: Math.round(shell.width * share)
        anchors.rightMargin: Math.round(shell.width * share)
        anchors.topMargin: Math.round(shell.height * share)
        anchors.bottomMargin: Math.round(shell.height * share)

        TopNav {
            id: topNav
            objectName: "topNav"
            anchors.left: parent.left
            anchors.right: parent.right
            y: Navigator.chromeVisible ? 0 : -height
            visible: y > -height
            z: 2
            onSignOutRequested: shell.signOutRequested()
            onLeaveDown: stack.forceActiveFocus(Qt.TabFocusReason)
            Behavior on y { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
        }

        Item {
            id: miniPlayerSlot
            objectName: "miniPlayerSlot"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            // The first child's height: childrenRect would loop, since it counts
            // visible children only and the strip is visible by its height.
            height: Navigator.chromeVisible && children.length > 0 ? children[0].height : 0
            visible: height > 0
        }

        Basic.StackView {
            id: stack
            objectName: "pageStack"
            anchors.top: parent.top
            // At once, not with the bar's slide: the player and sign-in
            // page take the whole window from their first frame.
            anchors.topMargin: Navigator.chromeVisible ? topNav.height : 0
            anchors.bottom: miniPlayerSlot.top
            anchors.right: parent.right
            anchors.left: parent.left
            focus: true
            clip: true

            pushEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal } }
            pushExit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }
            popEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal } }
            popExit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }
            replaceEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal } }
            replaceExit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }
        }
    }

    Connections {
        target: Navigator
        function onStackChanged(operation, page, properties) {
            switch (operation) {
            case Navigator.Push:
                stack.push(page, properties)
                break
            case Navigator.Pop:
                stack.pop()
                break
            case Navigator.Replace:
                stack.replace(page, properties)
                break
            case Navigator.Reset:
                stack.clear(Basic.StackView.Immediate)
                stack.push(page, properties, Basic.StackView.Immediate)
                break
            }
            if (stack.currentItem)
                stack.currentItem.forceActiveFocus()
        }
    }

    // Pages opened before the shell existed (the start page).
    Component.onCompleted: Navigator.replay()

    Keys.onPressed: (event) => {
        if (shell.isPaletteKey(event)) {
            shell.paletteRequested()
            event.accepted = true
        } else if (event.key === Qt.Key_Menu && event.modifiers === Qt.NoModifier) {
            event.accepted = shell.toggleNavFocus()
        } else if (event.key === Qt.Key_F11) {
            shell.toggleFullScreen()
            event.accepted = true
        } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace
                   || (event.key === Qt.Key_Left && (event.modifiers & Qt.AltModifier))) {
            event.accepted = shell.goBack()
        }
    }

    TapHandler {
        acceptedButtons: Qt.BackButton
        onTapped: shell.goBack()
    }

    Toast {
        id: toast
        objectName: "toast"
        parent: frame
    }

    // Settings > Gamepad > Check the buttons.
    GamepadCheck {}

    // TV mode: the on-screen keyboard, above the pages, dialogs and the
    // command palette while it shows.
    TextEntry {
        id: textEntry
        objectName: "textEntry"
        active: Theme.tvMode
    }
    OnScreenKeyboard {
        entry: textEntry
        parent: textEntry.open ? T.Overlay.overlay : shell
    }
}
