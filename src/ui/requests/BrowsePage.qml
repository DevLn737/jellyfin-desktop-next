pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell

// Seerr: what is trending and popular, search, and the way to the user's
// own requests. Not signed in, the page offers the sign-in instead.
RoutePage {
    id: page

    readonly property bool searching: field.text.trim().length > 0
    readonly property var trending: Requests.createTitles("trending", page)
    readonly property var movies: Requests.createTitles("popularMovies", page)
    readonly property var series: Requests.createTitles("popularTv", page)
    readonly property var found: Requests.createTitles("search", page)

    title: qsTr("Requests")

    function load() {
        if (!Requests.signedIn)
            return
        trending.reload()
        movies.reload()
        series.reload()
    }

    Component.onCompleted: load()
    // Signing in (or out) from the sign-in page or Settings.
    property bool wasSignedIn: Requests.signedIn
    Connections {
        target: Requests
        function onStateChanged() {
            if (Requests.signedIn !== page.wasSignedIn) {
                page.wasSignedIn = Requests.signedIn
                page.load()
            }
        }
    }

    Timer {
        id: typing
        interval: 350
        onTriggered: page.found.search(field.text)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.pageMargin
        anchors.rightMargin: Theme.pageMargin
        anchors.topMargin: Theme.spacingLarge
        spacing: Theme.spacingLarge

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            InputField {
                id: field
                objectName: "requestsSearch"
                Layout.fillWidth: true
                Layout.maximumWidth: 640
                enabled: Requests.signedIn
                focus: Requests.signedIn
                placeholderText: qsTr("Search films and series to request")
                Accessible.name: qsTr("Search films and series to request")
                inputMethodHints: Qt.ImhNoPredictiveText
                onTextChanged: typing.restart()
                onAccepted: {
                    typing.stop()
                    page.found.search(text)
                }
                KeyNavigation.down: page.searching ? results : content
            }
            ActionButton {
                objectName: "myRequestsButton"
                visible: Requests.signedIn
                text: qsTr("My requests")
                onClicked: Navigator.navigate("requests-mine")
            }
        }

        // Not signed in.
        ColumnLayout {
            objectName: "requestsSignedOut"
            visible: !Requests.signedIn
            Layout.fillWidth: true
            Layout.maximumWidth: 560
            spacing: Theme.spacing

            Heading { text: qsTr("Sign in to Seerr") }
            Text {
                Layout.fillWidth: true
                text: qsTr("Sign in with your Jellyfin account to browse and request films and series on %1.").arg(Requests.serverHost)
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
                wrapMode: Text.Wrap
            }
            ActionButton {
                objectName: "requestsSignInButton"
                text: qsTr("Sign in")
                primary: true
                focus: !Requests.signedIn
                onClicked: Navigator.navigate("seerr-signin")
            }
        }

        Text {
            objectName: "requestsError"
            Layout.fillWidth: true
            readonly property int code: page.searching ? page.found.error : page.trending.error
            visible: Requests.signedIn && code !== 0
            text: Requests.outcomeText(code)
            color: Theme.error
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.Wrap
            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
        }

        // Search results.
        CardGrid {
            id: results
            objectName: "requestsResults"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: Requests.signedIn && page.searching
            model: page.found
            delegate: TitleCard {}
            onCurrentIndexChanged: if (currentIndex >= count - 4 && page.found.hasMore) page.found.loadMore()

            LoadingIndicator {
                anchors.centerIn: parent
                visible: page.found.loading && results.count === 0
            }
            Text {
                anchors.centerIn: parent
                visible: !page.found.loading && results.count === 0 && page.found.error === 0
                text: qsTr("Nothing found.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
            }
        }

        // The rows.
        Flickable {
            id: content
            objectName: "requestsRows"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: Requests.signedIn && !page.searching
            clip: true
            contentWidth: width
            contentHeight: rows.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            activeFocusOnTab: true

            Column {
                id: rows
                width: content.width
                spacing: Theme.spacingLarge

                CardRow {
                    width: parent.width
                    title: qsTr("Trending")
                    visible: count > 0
                    model: page.trending
                    delegate: TitleCard {}
                }
                CardRow {
                    width: parent.width
                    title: qsTr("Popular films")
                    visible: count > 0
                    model: page.movies
                    delegate: TitleCard {}
                }
                CardRow {
                    width: parent.width
                    title: qsTr("Popular series")
                    visible: count > 0
                    model: page.series
                    delegate: TitleCard {}
                }
            }
        }
    }
}
