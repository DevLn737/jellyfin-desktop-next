import QtQuick
import QtQuick.Controls.Basic as Basic
import Jfd.Ui.Theme
import Jfd.Ui.Controls

// The window's content: sidebar, the page stack driven by Navigator, and
// the toast. Back: Esc, Backspace outside text fields, Alt+Left and the
// mouse back button. F11 toggles full screen; Esc leaves it first.
FocusScope {
    id: shell

    readonly property alias stack: stack
    readonly property alias toast: toast
    // The strip under the pages for the mini player. A module puts its item
    // in it (`parent: shell.miniPlayerSlot`); the strip is as tall as its
    // content and shows wherever the shell shows its sidebar, so not on the
    // sign-in page and not in the video player.
    readonly property alias miniPlayerSlot: miniPlayerSlot
    readonly property Item currentPage: stack.currentItem
    readonly property string title: currentPage && currentPage.title ? currentPage.title : ""
    // In a compact window the sidebar is an icon rail; the menu button opens
    // it over the page with the names (closed again by choosing, Esc or a
    // click outside).
    property bool menuOpen: false
    // Off while something under the shell must show through (the video with
    // HDR10 output).
    property bool backgroundVisible: true

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

    function goBack() {
        const window = shell.Window.window
        if (window && window.visibility === Window.FullScreen) {
            window.visibility = Window.Windowed
            return true
        }
        return Navigator.back()
    }

    focus: true

    // The size classes follow the window's width (logical pixels).
    Binding { target: Theme; property: "windowWidth"; value: shell.width }
    Connections {
        target: Theme
        function onCompactChanged() { shell.menuOpen = false }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.background
        visible: shell.backgroundVisible
    }

    Sidebar {
        id: sidebar
        objectName: "sidebar"
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        x: Navigator.chromeVisible ? 0 : -width
        visible: x > -width
        z: 2
        collapsed: Theme.compact && !shell.menuOpen
        menuButton: Theme.compact
        onMenuToggled: shell.menuOpen = !shell.menuOpen
        onChosen: shell.menuOpen = false
        Behavior on x { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
        KeyNavigation.right: stack
    }

    // The room the sidebar takes from the pages: the full sidebar, or only
    // the rail while the menu is open over them.
    Item {
        id: rail
        x: sidebar.x
        width: Theme.compact ? Theme.sidebarCollapsedWidth : Theme.sidebarWidth
        height: parent.height
    }

    Item {
        id: miniPlayerSlot
        objectName: "miniPlayerSlot"
        anchors.left: stack.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Navigator.chromeVisible ? childrenRect.height : 0
        visible: height > 0
    }

    Basic.StackView {
        id: stack
        objectName: "pageStack"
        anchors.top: parent.top
        anchors.bottom: miniPlayerSlot.top
        anchors.right: parent.right
        anchors.left: sidebar.visible ? rail.right : parent.left
        focus: true
        clip: true

        pushEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal } }
        pushExit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }
        popEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal } }
        popExit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }
        replaceEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal } }
        replaceExit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }
    }

    // Dims the page while the menu is open over it.
    Rectangle {
        anchors.fill: stack
        z: 1
        color: Theme.scrim
        visible: shell.menuOpen && sidebar.visible
        TapHandler { onTapped: shell.menuOpen = false }
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
        if (event.key === Qt.Key_Escape && shell.menuOpen) {
            shell.menuOpen = false
            event.accepted = true
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
    }
}
