pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell

// The navigation bar at the top of every page with chrome: Back (when there
// is somewhere to go back to) and the app's sign and name on the left; in
// the middle a floating pill with the main destinations (Navigator
// navEntries placed "primary": Home, Library, Music, Watch together,
// Search) and the account, whose menu holds Settings, the other "account"
// destinations (Downloads, Requests), TV mode and Sign out.
//
// Keys: the pill is a horizontal list that is entered at the current page's
// entry (enterAtCurrent), whichever entry is nearest; Down leaves the bar
// for the page, where the focus was last (`leaveDown`). In a narrow window
// the pill shows icons only, and below `menuBelow` one menu button opens
// every destination in a list.
FocusScope {
    id: bar

    property string accountName
    // Under the name in the menu: the server and its address.
    property string accountDetail
    // Without the server, downloads are what there is to watch: their entry
    // comes into the pill.
    property bool offline: false
    // Pages draw under a transparent bar when they scroll to their top
    // (a hero); otherwise it has its fill.
    property bool transparent: false
    signal signOutRequested()
    // Down from the bar: the page takes the focus back.
    signal leaveDown()

    readonly property int iconsOnlyBelow: 860
    readonly property int menuBelow: 560
    // Names go when the bar with them would not fit beside the brand (the
    // width depends on the font and the language). The width with names is
    // remembered from when they last showed.
    property real fullPillWidth: 0
    property real fullLeftWidth: 0
    // The same for icons only: below that, one menu.
    property real iconPillWidth: 0
    readonly property bool iconsOnly: width < iconsOnlyBelow
        || left.x + fullLeftWidth + Theme.spacing + fullPillWidth + Theme.spacingSmall > width
    readonly property bool menuOnly: width < menuBelow
        || (iconsOnly && left.x + left.width + Theme.spacing + iconPillWidth + Theme.spacingSmall > width)

    readonly property var primaryEntries: Navigator.navEntries.filter(
        (entry) => entry.placement === "primary" || (bar.offline && entry.route === "downloads"))
    readonly property var accountEntries: Navigator.navEntries.filter(
        (entry) => entry.placement === "account" && !(bar.offline && entry.route === "downloads"))

    function iconFor(name) { return Icons[name] !== undefined ? Icons[name] : name }
    function currentIndexIn(entries) {
        for (let i = 0; i < entries.length; ++i) {
            if (entries[i].route === Navigator.currentName)
                return i
        }
        return -1
    }
    // The Menu key and Back on a root page: the current entry (or the first).
    function focusCurrentEntry() {
        if (bar.menuOnly) {
            menuButton.forceActiveFocus(Qt.TabFocusReason)
            return
        }
        pill.currentIndex = Math.max(0, bar.currentIndexIn(bar.primaryEntries))
        pill.forceActiveFocus(Qt.TabFocusReason)
    }
    function openAccountMenu() { accountMenu.open() }
    function remember() {
        if (bar.menuOnly)
            return
        if (bar.iconsOnly) {
            bar.iconPillWidth = pillFrame.width
        } else {
            bar.fullPillWidth = pillFrame.width
            bar.fullLeftWidth = left.width
        }
    }
    onIconsOnlyChanged: Qt.callLater(remember)

    implicitHeight: (Theme.tvMode ? 112 : 88)
    activeFocusOnTab: true

    Keys.onDownPressed: (event) => {
        bar.leaveDown()
        event.accepted = true
    }

    Rectangle {
        objectName: "topNavFill"
        anchors.fill: parent
        color: Theme.background
        opacity: bar.transparent ? 0 : 0.96
        Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.glassBorder
            opacity: 0.6
        }
    }

    Row {
        id: left
        anchors.left: parent.left
        anchors.leftMargin: Theme.pageMargin - Theme.spacingSmall
        anchors.verticalCenter: parent.verticalCenter
        onWidthChanged: bar.remember()
        spacing: Theme.spacing

        IconButton {
            objectName: "backButton"
            anchors.verticalCenter: parent.verticalCenter
            visible: Navigator.canGoBack
            iconSource: Icons.back
            toolTipText: qsTr("Back")
            onClicked: Navigator.back()
        }

        Row {
            objectName: "topNavBrand"
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingSmall + 2
            visible: !bar.iconsOnly || !Navigator.canGoBack
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.tvMode ? 40 : 30
                height: width
                radius: Math.round(width * 0.3)
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: Qt.lighter(Theme.accent, 1.15) }
                    GradientStop { position: 1; color: Qt.darker(Theme.accent, 1.25) }
                }
                Icon {
                    anchors.centerIn: parent
                    source: Icons.play
                    size: Math.round(parent.width * 0.42)
                    color: Theme.background
                }
            }
            Text {
                visible: !bar.iconsOnly
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Media library")
                color: Theme.text
                font.pixelSize: Math.round(Theme.fontSizeBody * 1.15)
                font.weight: Font.Bold
            }
        }
    }

    // The pill.
    Rectangle {
        id: pillFrame
        objectName: "topNavPill"
        visible: !bar.menuOnly
        anchors.centerIn: parent
        // Between the brand and the right edge when the window is narrow.
        anchors.horizontalCenterOffset: {
            const free = (bar.width - width) / 2
            const need = left.x + left.width + Theme.spacing
            return free < need ? need - free : 0
        }
        width: content.implicitWidth + 2 * padding
        onWidthChanged: bar.remember()
        height: (Theme.tvMode ? 64 : 50)
        readonly property int padding: 5
        radius: Theme.tvMode ? 20 : 16
        color: Qt.alpha(Theme.surfaceRaised, Theme.blurEnabled ? 0.72 : 0.9)
        border.width: 1
        border.color: Theme.glassBorder

        Row {
            id: content
            anchors.centerIn: parent
            spacing: 2

            ListView {
                id: pill
                objectName: "topNavEntries"
                // Spatial navigation enters this list at its current entry.
                readonly property bool enterAtCurrent: true
                anchors.verticalCenter: parent.verticalCenter
                orientation: ListView.Horizontal
                interactive: false
                spacing: 2
                width: contentWidth
                height: pillFrame.height - 2 * pillFrame.padding
                model: bar.primaryEntries
                currentIndex: bar.currentIndexIn(bar.primaryEntries)
                keyNavigationEnabled: true
                activeFocusOnTab: true
                focus: true
                highlightFollowsCurrentItem: false

                delegate: T.ItemDelegate {
                    id: entry
                    required property var modelData
                    required property int index
                    objectName: "nav_" + modelData.route
                    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset, implicitContentWidth + leftPadding + rightPadding)
                    readonly property bool current: Navigator.currentName === modelData.route
                    height: pill.height
                    leftPadding: bar.iconsOnly ? 11 : 15
                    rightPadding: leftPadding
                    focusPolicy: Qt.StrongFocus
                    hoverEnabled: true
                    Accessible.name: modelData.text
                    Keys.onReturnPressed: entry.clicked()
                    Keys.onEnterPressed: entry.clicked()
                    onClicked: Navigator.navigate(modelData.route)

                    contentItem: Row {
                        spacing: Theme.spacingSmall
                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            source: bar.iconFor(entry.modelData.icon)
                            size: Theme.tvMode ? 26 : 18
                            color: entry.current ? Theme.text : Theme.textSecondary
                        }
                        Text {
                            visible: !bar.iconsOnly
                            anchors.verticalCenter: parent.verticalCenter
                            text: entry.modelData.text
                            color: entry.current || entry.hovered ? Theme.text : Theme.textSecondary
                            font.pixelSize: Theme.fontSizeBody
                            font.weight: entry.current ? Font.Bold : Font.DemiBold
                        }
                        // Ctrl+K opens the command palette from anywhere.
                        Rectangle {
                            visible: entry.modelData.route === "search" && !bar.iconsOnly && !Theme.tvMode
                            anchors.verticalCenter: parent.verticalCenter
                            width: shortcut.implicitWidth + 10
                            height: shortcut.implicitHeight + 2
                            radius: 5
                            color: "transparent"
                            border.width: 1
                            border.color: Theme.glassBorder
                            Text {
                                id: shortcut
                                anchors.centerIn: parent
                                text: qsTr("Ctrl K")
                                color: Theme.textDisabled
                                font.pixelSize: Theme.fontSizeSmall - 1
                            }
                        }
                    }
                    background: Rectangle {
                        radius: pillFrame.radius - pillFrame.padding
                        color: entry.current ? Qt.alpha(Theme.text, 0.12)
                             : entry.hovered || entry.down ? Qt.alpha(Theme.text, 0.06) : "transparent"
                        FocusFrame { shown: entry.visualFocus; inset: 0 }
                    }
                }
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 1
                height: Math.round(pill.height * 0.55)
                color: Theme.glassBorder
                visible: account.visible
            }

            T.ItemDelegate {
                id: account
                objectName: "topNavAccount"
                implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset, implicitContentWidth + leftPadding + rightPadding)
                anchors.verticalCenter: parent.verticalCenter
                visible: bar.accountName.length > 0
                height: pill.height
                leftPadding: 6
                rightPadding: 10
                focusPolicy: Qt.StrongFocus
                hoverEnabled: true
                Accessible.name: bar.accountName
                Keys.onReturnPressed: account.clicked()
                Keys.onEnterPressed: account.clicked()
                Keys.onLeftPressed: (event) => {
                    pill.currentIndex = pill.count - 1
                    pill.forceActiveFocus(Qt.TabFocusReason)
                    event.accepted = true
                }
                onClicked: accountMenu.open()
                contentItem: Row {
                    spacing: Theme.spacingSmall
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.tvMode ? 38 : 28
                        height: width
                        radius: width / 2
                        gradient: Gradient {
                            GradientStop { position: 0; color: Qt.lighter(Theme.accent, 1.1) }
                            GradientStop { position: 1; color: Qt.darker(Theme.accent, 1.4) }
                        }
                        Text {
                            anchors.centerIn: parent
                            text: bar.accountName.charAt(0).toUpperCase()
                            color: Theme.text
                            font.pixelSize: Theme.fontSizeBody - 1
                            font.weight: Font.Bold
                        }
                    }
                    Text {
                        objectName: "accountName"
                        visible: !bar.iconsOnly
                        anchors.verticalCenter: parent.verticalCenter
                        text: bar.accountName
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeBody
                        font.weight: Font.DemiBold
                    }
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        source: Icons.down
                        size: 16
                        color: Theme.textSecondary
                    }
                }
                background: Rectangle {
                    radius: pillFrame.radius - pillFrame.padding
                    color: account.hovered || account.down || accountMenu.visible ? Qt.alpha(Theme.text, 0.06) : "transparent"
                    FocusFrame { shown: account.visualFocus; inset: 0 }
                }
            }
        }
    }

    // A very narrow window: every destination in one menu.
    IconButton {
        id: menuButton
        objectName: "menuButton"
        visible: bar.menuOnly
        anchors.right: parent.right
        anchors.rightMargin: Theme.pageMargin - Theme.spacingSmall
        anchors.verticalCenter: parent.verticalCenter
        iconSource: Icons.menu
        toolTipText: qsTr("Menu")
        onClicked: allMenu.open()
    }

    PopupMenu {
        id: allMenu
        objectName: "allDestinationsMenu"
        anchorItem: menuButton
        alignRight: true
        readonly property var routes: bar.primaryEntries.concat(bar.accountEntries)
        entries: routes.map((entry) => entry.text).concat([qsTr("Sign out")])
        currentIndex: bar.currentIndexIn(routes)
        onActivated: (index) => {
            if (index === routes.length)
                signOutConfirm.open()
            else
                Navigator.navigate(routes[index].route)
        }
    }

    // The account's menu: Settings and the other "account" destinations,
    // TV mode (on or off) and Sign out.
    PopupMenu {
        id: accountMenu
        objectName: "accountMenu"
        anchorItem: account
        alignRight: true
        width: 260
        readonly property int tvIndex: bar.accountEntries.length
        entries: bar.accountEntries.map((entry) => entry.text)
                 .concat([Theme.tvMode ? qsTr("Leave TV mode") : qsTr("TV mode"), qsTr("Sign out")])
        currentIndex: bar.currentIndexIn(bar.accountEntries)
        onActivated: (index) => {
            if (index < tvIndex)
                Navigator.navigate(bar.accountEntries[index].route)
            else if (index === tvIndex)
                CommandRegistry.run("interface.tvMode")
            else
                signOutConfirm.open()
        }
    }

    AppDialog {
        id: signOutConfirm
        objectName: "signOutConfirm"
        title: qsTr("Sign out")
        text: qsTr("Sign out of this server? Playback stops and the password is asked for again.")
        acceptText: qsTr("Sign out")
        destructive: true
        onAccepted: bar.signOutRequested()
    }
}
