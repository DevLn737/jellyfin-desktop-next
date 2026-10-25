pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// One library as a grid: every card has its place from the start, so the
// scroll bar and End work at once; pages of cards load as they come into
// view. Sort, order, watched state, favorites, genres and year filter the
// grid; a movie library can list its collections instead. The library
// remembers its choices (Library.createLibrary).
RoutePage {
    id: page

    // Created once the route is known; the page owns it.
    property LibraryModel library: null

    Component.onCompleted: library = Library.createLibrary(routeArgument, page)

    title: Library.libraryName(routeArgument) || (library ? library.name : "") || qsTr("Library")

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingHuge
        anchors.rightMargin: Theme.spacingHuge
        anchors.topMargin: Theme.spacingLarge
        spacing: Theme.spacingLarge

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            Heading {
                Layout.fillWidth: true
                text: page.title
            }
            Text {
                objectName: "libraryCount"
                text: page.library ? qsTr("%n item(s)", "", page.library.count) : ""
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: Theme.spacing

            ChoiceBox {
                id: sort
                objectName: "librarySort"
                model: [
                    { value: "SortName", label: qsTr("Name") },
                    { value: "DateCreated", label: qsTr("Date added") },
                    { value: "PremiereDate", label: qsTr("Release date") },
                    { value: "CommunityRating", label: qsTr("Rating") },
                    { value: "Runtime", label: qsTr("Runtime") },
                    { value: "DatePlayed", label: qsTr("Date played") },
                ]
                currentIndex: page.library ? indexOfValue(page.library.sortBy) : 0
                onActivated: {
                    if (!page.library)
                        return
                    page.library.sortBy = currentValue
                    // Newest and best first, as Jellyfin Web does.
                    page.library.descending = currentValue !== "SortName"
                }
            }
            ChoiceBox {
                objectName: "libraryOrder"
                implicitWidth: 180
                model: [
                    { value: false, label: qsTr("Ascending") },
                    { value: true, label: qsTr("Descending") },
                ]
                currentIndex: page.library && page.library.descending ? 1 : 0
                onActivated: if (page.library) page.library.descending = currentValue
            }
            ChoiceBox {
                objectName: "libraryPlayed"
                implicitWidth: 180
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
                implicitWidth: 180
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
                implicitWidth: 160
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

        Text {
            objectName: "libraryError"
            Layout.fillWidth: true
            visible: text.length > 0
            text: page.library ? page.library.errorText : ""
            color: Theme.error
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.Wrap
        }

        CardGrid {
            id: grid
            objectName: "libraryGrid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            focus: true
            model: page.library
            delegate: ItemCard {}

            LoadingIndicator {
                anchors.centerIn: parent
                visible: page.library && page.library.loading && grid.count === 0
            }
            Text {
                anchors.centerIn: parent
                visible: page.library && !page.library.loading && page.library.count === 0
                         && page.library.errorText.length === 0
                text: qsTr("Nothing matches these filters.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
            }
        }
    }
}
