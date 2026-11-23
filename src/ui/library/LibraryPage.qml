pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// One library as a grid: every card has its place from the start, so the
// scroll bar and End work at once; pages of cards load as they come into
// view. Sort, order, watched state, favorites, genres and year filter the
// grid; a movie library can list its collections instead. The cards come
// in three sizes. Listed by name from A to Z, a bar of letters on the right
// (and typing a letter on the grid) scrolls to where that letter starts.
// The library remembers its choices (Library.createLibrary).
RoutePage {
    id: page

    // Created once the route is known; the page owns it.
    property LibraryModel library: null

    // "library" without an id opens the library used last (or the first).
    property string libraryId
    Component.onCompleted: {
        libraryId = routeArgument.length > 0 ? routeArgument : Library.defaultLibraryId()
        library = Library.createLibrary(libraryId, page)
    }

    // Back from a lost connection: what failed loads again.
    Connections {
        target: Library
        function onReconnected() { if (page.library && page.library.errorText.length > 0) page.library.reload() }
    }

    // The Filters button shows the filter controls.
    property bool filtersOpen: false

    // The filters in use, as chips under the toolbar: {key, label}.
    readonly property var activeFilters: {
        const out = []
        const l = library
        if (!l)
            return out
        if (l.playedFilter === LibraryModel.Unplayed) out.push({ key: "played", label: qsTr("Unwatched") })
        else if (l.playedFilter === LibraryModel.Played) out.push({ key: "played", label: qsTr("Watched") })
        if (l.favoritesOnly) out.push({ key: "favorites", label: qsTr("Favorites") })
        for (const genre of l.genres) out.push({ key: "genre:" + genre, label: genre })
        for (const year of l.years) out.push({ key: "year", label: String(year) })
        return out
    }
    function dropFilter(key) {
        if (!library)
            return
        if (key === "played") library.playedFilter = LibraryModel.All
        else if (key === "favorites") library.favoritesOnly = false
        else if (key === "year") library.years = []
        else if (key.startsWith("genre:")) library.genres = library.genres.filter(g => g !== key.slice(6))
    }

    title: Library.libraryName(libraryId) || (library ? library.name : "") || qsTr("Library")
    hasTopBar: true

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.pageMargin
        anchors.rightMargin: Theme.pageMargin
        anchors.topMargin: Theme.spacingLarge
        spacing: Theme.spacingLarge

        // The libraries as tabs, then Filters, sort and order.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            Rectangle {
                id: tabsFrame
                objectName: "libraryTabsFrame"
                readonly property int padding: 5
                // As wide as the tabs, narrower when the toolbar needs the
                // room; the list takes its width from the frame (sizing the
                // frame from the list made the row lay itself out again).
                Layout.fillWidth: true
                Layout.minimumWidth: 2 * padding
                Layout.preferredWidth: tabs.contentWidth + 2 * padding
                Layout.maximumWidth: tabs.contentWidth + 2 * padding
                Layout.preferredHeight: tabs.height + 2 * padding
                radius: Theme.radiusLarge + 2
                color: Qt.alpha(Theme.text, 0.04)
                border.width: 1
                border.color: Theme.glassBorder

                ListView {
                    id: tabs
                    objectName: "libraryTabs"
                    readonly property bool enterAtCurrent: true
                    anchors.verticalCenter: parent.verticalCenter
                    x: tabsFrame.padding
                    width: Math.max(0, tabsFrame.width - 2 * tabsFrame.padding)
                    height: Theme.controlHeight + 4
                    orientation: ListView.Horizontal
                    spacing: 2
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: Library.home ? Library.home.libraries : null
                    keyNavigationEnabled: true
                    activeFocusOnTab: true
                    highlightFollowsCurrentItem: false
                    currentIndex: Library.home ? Math.max(0, Library.home.libraries.rowOf(page.libraryId)) : 0
                    delegate: T.ItemDelegate {
                        id: tab
                        required property var model
                        required property int index
                        readonly property string libraryId: model.itemId || ""
                        readonly property bool current: libraryId === page.libraryId
                        objectName: "library_" + libraryId
                        implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset, implicitContentWidth + leftPadding + rightPadding)
                        height: tabs.height
                        leftPadding: 14
                        rightPadding: 14
                        focusPolicy: Qt.StrongFocus
                        hoverEnabled: true
                        Accessible.name: model.name || ""
                        Keys.onReturnPressed: tab.clicked()
                        Keys.onEnterPressed: tab.clicked()
                        onClicked: if (!current) Navigator.navigate(Library.routeFor(libraryId, model.type || ""))
                        contentItem: Row {
                            spacing: Theme.spacingSmall
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: tab.model.name || ""
                                color: tab.current || tab.hovered ? Theme.text : Theme.textSecondary
                                font.pixelSize: Theme.fontSizeBody
                                font.weight: Font.Bold
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: tab.model.childCount > 0
                                text: String(tab.model.childCount)
                                color: Theme.textDisabled
                                font.pixelSize: Theme.fontSizeSmall
                                font.weight: Font.Bold
                            }
                        }
                        background: Rectangle {
                            radius: Theme.radius + 3
                            color: tab.current ? Qt.alpha(Theme.text, 0.12)
                                 : tab.hovered ? Qt.alpha(Theme.text, 0.05) : "transparent"
                            FocusFrame { shown: tab.visualFocus; inset: 0 }
                        }
                    }
                }
            }

            Item { Layout.fillWidth: true }

            Row {
                id: toolbar
                spacing: Theme.spacingSmall

                // Filters: an icon with the number in use; the filters drop
                // down under it.
                IconButton {
                    id: filtersButton
                    objectName: "libraryFiltersButton"
                    square: true
                    readonly property int active: page.activeFilters.length
                    iconSource: Icons.filter
                    toolTipText: qsTr("Filters")
                    Accessible.name: active > 0 ? qsTr("Filters · %1").arg(active) : qsTr("Filters")
                    highlighted: page.filtersOpen || active > 0
                    onClicked: page.filtersOpen = !page.filtersOpen
                    Rectangle {
                        visible: filtersButton.active > 0
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: -2
                        width: Math.max(18, badgeText.implicitWidth + 8)
                        height: 18
                        radius: 9
                        color: Theme.accent
                        Text {
                            id: badgeText
                            anchors.centerIn: parent
                            text: String(filtersButton.active)
                            color: Theme.accentText
                            font.pixelSize: 11
                            font.weight: Font.ExtraBold
                        }
                    }
                }
                // Sort: an icon; the fields drop down, the current one marked.
                IconButton {
                    id: sortButton
                    objectName: "librarySortButton"
                    square: true
                    readonly property var fields: [
                        { value: "SortName", label: qsTr("Name") },
                        { value: "DateCreated", label: qsTr("Date added") },
                        { value: "PremiereDate", label: qsTr("Release date") },
                        { value: "CommunityRating", label: qsTr("Rating") },
                        { value: "Runtime", label: qsTr("Runtime") },
                        { value: "DatePlayed", label: qsTr("Date played") },
                    ]
                    function indexOfValue(value) {
                        for (let i = 0; i < fields.length; ++i)
                            if (fields[i].value === value) return i
                        return 0
                    }
                    iconSource: Icons.sort
                    toolTipText: qsTr("Sort: %1").arg(fields[page.library ? indexOfValue(page.library.sortBy) : 0].label)
                    Accessible.name: toolTipText
                    highlighted: sortMenu.visible
                    onClicked: sortMenu.visible ? sortMenu.close() : sortMenu.open()
                    PopupMenu {
                        id: sortMenu
                        objectName: "librarySort"
                        anchorItem: sortButton
                        alignRight: true
                        entries: sortButton.fields.map((f) => f.label)
                        currentIndex: page.library ? sortButton.indexOfValue(page.library.sortBy) : 0
                        onActivated: (index) => {
                            if (!page.library)
                                return
                            const value = sortButton.fields[index].value
                            page.library.sortBy = value
                            // Newest and best first, as Jellyfin Web does.
                            page.library.descending = value !== "SortName"
                        }
                    }
                }
                // The order: one press turns it round.
                IconButton {
                    objectName: "libraryOrder"
                    square: true
                    readonly property bool descending: page.library !== null && page.library.descending
                    iconSource: descending ? Icons.down : Icons.up
                    toolTipText: descending ? qsTr("Descending") : qsTr("Ascending")
                    Accessible.name: toolTipText
                    onClicked: if (page.library) page.library.descending = !page.library.descending
                }
            }
        }

        // The filters, dropped down under the Filters button.
        T.Popup {
            id: filtersPopup
            objectName: "libraryFiltersPopup"
            parent: T.Overlay.overlay
            visible: page.filtersOpen
            onClosed: page.filtersOpen = false
            onAboutToShow: {
                const corner = filtersButton.mapToItem(parent, 0, 0)
                x = Math.max(Theme.spacingSmall, Math.min(corner.x + filtersButton.width - width, parent.width - width - Theme.spacingSmall))
                y = corner.y + filtersButton.height + 8
            }
            width: 300
            // A templated Popup has no height of its own.
            implicitHeight: contentHeight + topPadding + bottomPadding
            padding: Theme.spacing
            focus: true
            closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent
            background: Rectangle {
                radius: Theme.radiusLarge
                color: Theme.popupFill
                border.width: 1
                border.color: Theme.glassBorder
            }

            contentItem: Column {
            objectName: "libraryFilters"
            spacing: Theme.spacingSmall

            ChoiceBox {
                objectName: "libraryPlayed"
                width: parent.width
                model: [
                    { value: LibraryModel.All, label: qsTr("All") },
                    { value: LibraryModel.Unplayed, label: qsTr("Unwatched") },
                    { value: LibraryModel.Played, label: qsTr("Watched") },
                ]
                currentIndex: page.library ? indexOfValue(page.library.playedFilter) : 0
                onActivated: if (page.library) page.library.playedFilter = currentValue
            }
            ChoiceBox {
                objectName: "libraryShow"
                width: parent.width
                visible: page.library && page.library.canShowCollections
                model: [
                    { value: false, label: qsTr("Movies") },
                    { value: true, label: qsTr("Collections") },
                ]
                currentIndex: page.library && page.library.showCollections ? 1 : 0
                onActivated: if (page.library) page.library.showCollections = currentValue
            }
            GenrePicker {
                objectName: "libraryGenre"
                visible: page.library && (page.library.availableGenres.length > 0 || page.library.genres.length > 0)
                genres: page.library ? page.library.availableGenres : []
                selected: page.library ? page.library.genres : []
                onGenreToggled: (genre) => {
                    const chosen = page.library.genres.slice()
                    const at = chosen.indexOf(genre)
                    if (at >= 0)
                        chosen.splice(at, 1)
                    else
                        chosen.push(genre)
                    page.library.genres = chosen
                }
                onGenresCleared: page.library.genres = []
            }
            ChoiceBox {
                objectName: "libraryYear"
                width: parent.width
                visible: page.library && (page.library.availableYears.length > 0 || page.library.years.length > 0)
                model: {
                    const years = [{ value: 0, label: qsTr("All years") }]
                    if (page.library) {
                        const offered = page.library.availableYears.slice().sort((a, b) => b - a)
                        // A remembered year the library no longer has stays choosable.
                        for (const year of page.library.years) {
                            if (offered.indexOf(year) < 0)
                                offered.unshift(year)
                        }
                        for (const year of offered)
                            years.push({ value: year, label: String(year) })
                    }
                    return years
                }
                currentIndex: page.library && page.library.years.length > 0
                    ? Math.max(0, indexOfValue(page.library.years[0])) : 0
                onActivated: if (page.library) page.library.years = currentValue > 0 ? [currentValue] : []
            }
            ChoiceBox {
                objectName: "libraryDensity"
                width: parent.width
                model: [
                    { value: "small", label: qsTr("Small cards") },
                    { value: "medium", label: qsTr("Medium cards") },
                    { value: "large", label: qsTr("Large cards") },
                ]
                currentIndex: page.library ? Math.max(0, indexOfValue(page.library.density)) : 1
                onActivated: if (page.library) page.library.density = currentValue
            }
            Row {
                height: Theme.controlHeight
                spacing: Theme.spacingSmall
                ToggleSwitch {
                    id: favorites
                    objectName: "libraryFavorites"
                    anchors.verticalCenter: parent.verticalCenter
                    checked: page.library ? page.library.favoritesOnly : false
                    onToggled: if (page.library) page.library.favoritesOnly = checked
                    Accessible.name: favoritesLabel.text
                }
                Text {
                    id: favoritesLabel
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Favorites")
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeBody
                }
            }
        }
        }

        // How many, then the filters in use, each with a way to drop it.
        Flow {
            objectName: "libraryActiveFilters"
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            Text {
                objectName: "libraryCount"
                height: Theme.tvMode ? 40 : 28
                verticalAlignment: Text.AlignVCenter
                // Not "0 items" beside an error, nor while the first page loads.
                visible: page.library !== null && page.library.errorText.length === 0
                         && !(page.library.loading && page.library.count === 0)
                text: page.library ? qsTr("%n item(s)", "", page.library.count) : ""
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
                font.weight: Font.DemiBold
                rightPadding: Theme.spacingSmall
            }
            Repeater {
                model: page.activeFilters
                T.AbstractButton {
                    id: chip
                    required property var modelData
                    objectName: "filterChip_" + modelData.key
                    implicitWidth: chipRow.implicitWidth + 24
                    implicitHeight: Theme.tvMode ? 40 : 28
                    focusPolicy: Qt.StrongFocus
                    hoverEnabled: true
                    Accessible.name: qsTr("Remove filter %1").arg(modelData.label)
                    Keys.onReturnPressed: chip.clicked()
                    Keys.onEnterPressed: chip.clicked()
                    onClicked: page.dropFilter(modelData.key)
                    contentItem: Row {
                        id: chipRow
                        spacing: 6
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: chip.modelData.label
                            color: Theme.accent
                            font.pixelSize: Theme.fontSizeSmall + 1
                            font.weight: Font.Bold
                        }
                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            source: Icons.close
                            size: Theme.tvMode ? 18 : 13
                            color: Theme.accent
                        }
                    }
                    leftPadding: 12
                    rightPadding: 12
                    background: Rectangle {
                        radius: Theme.radius - 2
                        color: Qt.alpha(Theme.accent, chip.hovered ? 0.24 : 0.14)
                        border.width: 1
                        border.color: Qt.alpha(Theme.accent, 0.3)
                        FocusFrame { shown: chip.visualFocus; inset: 0 }
                    }
                }
            }
        }

        StateMessage {
            objectName: "libraryError"
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 200 : 0
            visible: page.library && page.library.errorText.length > 0
            kind: Library.online ? "error" : "offline"
            description: page.library ? page.library.errorText : ""
            onRetry: if (page.library) page.library.reload()
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingSmall

            CardGrid {
                id: grid
                objectName: "libraryGrid"
                Layout.fillWidth: true
                Layout.fillHeight: true
                focus: true
                model: page.library
                readonly property real sizeFactor: !page.library ? 1
                    : page.library.density === "small" ? 0.78 : page.library.density === "large" ? 1.3 : 1
                cardWidth: Math.round(Theme.posterWidth * sizeFactor)
                cardHeight: Math.round(cardWidth / Theme.posterAspect) + Theme.cardCaptionHeight
                delegate: ItemCard { cardWidth: grid.cardWidth }

                function showRow(row) {
                    if (row < 0 || row >= count)
                        return
                    view.positionViewAtIndex(row, GridView.Beginning)
                    currentIndex = row
                }

                // A letter typed on the grid goes to it, as the bar does.
                Keys.onPressed: (event) => {
                    const letter = event.text.length === 1 ? event.text.toUpperCase() : ""
                    if (page.library && page.library.alphabetical && letter.length === 1
                            && letter.toLowerCase() !== letter
                            && (event.modifiers & ~(Qt.ShiftModifier | Qt.KeypadModifier)) === 0) {
                        page.library.locateLetter(letter)
                        event.accepted = true
                    }
                }
                Connections {
                    target: page.library
                    function onLetterLocated(letter, row) { grid.showRow(row) }
                }

                // While the first page loads: grey cards where the grid will be.
                Flow {
                    objectName: "librarySkeleton"
                    width: parent.width
                    spacing: Theme.spacing
                    visible: page.library && page.library.loading && grid.count === 0
                    Repeater {
                        model: visible ? 12 : 0
                        SkeletonBox {
                            width: grid.cardWidth
                            height: Math.round(grid.cardWidth / Theme.posterAspect)
                            radius: Theme.radiusLarge
                        }
                    }
                }
                StateMessage {
                    objectName: "libraryEmpty"
                    anchors.centerIn: parent
                    visible: page.library && !page.library.loading && page.library.count === 0
                             && page.library.errorText.length === 0
                    kind: "empty"
                    title: qsTr("Nothing matches these filters.")
                }
            }

            AlphabetBar {
                objectName: "alphabetBar"
                Layout.fillHeight: true
                visible: page.library !== null && page.library.alphabetical && grid.view.contentHeight > grid.height
                onLetterChosen: (letter) => page.library.locateLetter(letter)
            }
        }
    }
}
