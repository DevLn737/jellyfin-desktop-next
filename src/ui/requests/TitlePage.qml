pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell

// One title in Seerr: poster and facts, where it stands (available, being
// added, requested), and the Request button. A series offers the seasons
// to pick, or all of them after a separate confirmation. Nothing is
// requested without a question first. routeArgument is "movie:603" or
// "tv:1399".
RoutePage {
    id: page

    property var details: null
    // Season number -> chosen.
    property var chosen: ({})
    readonly property var chosenNumbers: {
        var numbers = []
        for (var key in chosen) {
            if (chosen[key])
                numbers.push(Number(key))
        }
        numbers.sort(function (a, b) { return a - b })
        return numbers
    }
    property string resultText
    property bool resultIsError: false
    readonly property bool isSeries: details && details.mediaType === "tv"

    title: details && details.loaded ? details.name : qsTr("Request")

    Component.onCompleted: {
        details = Requests.createDetails(page)
        details.load(routeArgument)
    }

    Connections {
        target: page.details
        function onActionFinished(outcome) {
            page.resultIsError = outcome !== 0
            page.resultText = outcome === 0 ? qsTr("Done. The status is updated.") : Requests.outcomeText(outcome)
            page.chosen = ({})
        }
    }

    Flickable {
        id: scroller
        anchors.fill: parent
        anchors.leftMargin: Theme.pageMargin
        anchors.rightMargin: Theme.pageMargin
        anchors.topMargin: Theme.spacingLarge
        contentWidth: width
        contentHeight: content.implicitHeight + Theme.spacingHuge
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        LoadingIndicator {
            anchors.centerIn: parent
            visible: page.details.loading && !page.details.loaded
        }

        Text {
            objectName: "titleError"
            width: parent.width
            visible: page.details.error !== 0 && !page.details.loaded
            text: Requests.outcomeText(page.details.error)
            color: Theme.error
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.Wrap
            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
        }

        RowLayout {
            id: content
            width: scroller.width
            visible: page.details.loaded
            spacing: Theme.spacingLarge

            Rectangle {
                Layout.alignment: Qt.AlignTop
                Layout.preferredWidth: Theme.compact ? 120 : 240
                Layout.preferredHeight: width * 1.5
                radius: Theme.radius
                color: Theme.surfaceRaised
                clip: true
                Image {
                    anchors.fill: parent
                    source: Requests.image(page.details.posterUrl)
                    sourceSize.width: 480
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: Theme.spacing

                Heading {
                    objectName: "titleName"
                    Layout.fillWidth: true
                    text: page.details.name
                    font.pixelSize: Theme.fontSizeHeadline
                    wrapMode: Text.Wrap
                    elide: Text.ElideNone
                }
                Text {
                    Layout.fillWidth: true
                    readonly property var parts: {
                        var out = [page.isSeries ? qsTr("Series") : qsTr("Film")]
                        if (page.details.year.length > 0)
                            out.push(page.details.year)
                        if (page.details.runtimeMinutes > 0)
                            out.push(qsTr("%1 min").arg(page.details.runtimeMinutes))
                        if (page.details.genres.length > 0)
                            out.push(page.details.genres.join(", "))
                        return out
                    }
                    text: parts.join(" · ")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                    wrapMode: Text.Wrap
                }
                Text {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: page.details.tagline
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                    font.italic: true
                    wrapMode: Text.Wrap
                }

                // Where it stands.
                StatusBadge {
                    objectName: "titleStatus"
                    kind: Requests.mediaStatusKind(page.details.status)
                    text: Requests.mediaStatusText(page.details.status)
                }
                StatusBadge {
                    objectName: "myRequestStatus"
                    kind: Requests.requestStatusKind(page.details.myRequestStatus)
                    text: page.details.myRequestStatus > 0
                        ? qsTr("Your request: %1").arg(Requests.requestStatusText(page.details.myRequestStatus))
                        : ""
                }

                Text {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: page.details.overview
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeBody
                    wrapMode: Text.Wrap
                }

                // Seasons of a series.
                SectionHeader {
                    visible: page.isSeries && page.details.seasons.length > 0
                    text: qsTr("Seasons")
                }
                Repeater {
                    model: page.isSeries ? page.details.seasons : []
                    delegate: RowLayout {
                        id: seasonRow
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        spacing: Theme.spacing

                        ToggleSwitch {
                            objectName: "season" + seasonRow.modelData.number
                            enabled: seasonRow.modelData.requestable && page.details.canRequest && !page.details.working
                            checked: page.chosen[seasonRow.modelData.number] === true
                            Accessible.name: qsTr("Request season %1").arg(seasonRow.modelData.number)
                            onToggled: {
                                var next = Object.assign({}, page.chosen)
                                next[seasonRow.modelData.number] = checked
                                page.chosen = next
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: (seasonRow.modelData.name || qsTr("Season %1").arg(seasonRow.modelData.number))
                                  + (seasonRow.modelData.episodeCount > 0
                                     ? " · " + qsTr("Episodes: %1").arg(seasonRow.modelData.episodeCount) : "")
                            color: Theme.text
                            font.pixelSize: Theme.fontSizeBody
                            elide: Text.ElideRight
                        }
                        StatusBadge {
                            kind: Requests.mediaStatusKind(seasonRow.modelData.status)
                            text: Requests.mediaStatusText(seasonRow.modelData.status)
                        }
                    }
                }

                // Actions.
                Text {
                    objectName: "requestsNotAllowed"
                    Layout.fillWidth: true
                    visible: page.details.requestsNotAllowed
                    text: qsTr("Your Seerr account cannot make requests.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                    wrapMode: Text.Wrap
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.spacing

                    ActionButton {
                        objectName: "requestButton"
                        visible: page.details.canRequest && (!page.isSeries || page.chosenNumbers.length > 0)
                        enabled: !page.details.working
                        primary: true
                        focus: true
                        text: page.isSeries ? qsTr("Request selected seasons") : qsTr("Request")
                        onClicked: {
                            page.resultText = ""
                            confirmRequest.open()
                        }
                    }
                    ActionButton {
                        objectName: "requestAllButton"
                        visible: page.details.canRequest && page.isSeries
                        enabled: !page.details.working
                        primary: page.chosenNumbers.length === 0
                        text: qsTr("Request all seasons")
                        onClicked: {
                            page.resultText = ""
                            confirmAll.open()
                        }
                    }
                    ActionButton {
                        objectName: "cancelMineButton"
                        visible: page.details.myRequestStatus === 1
                        enabled: !page.details.working
                        text: qsTr("Cancel my request")
                        onClicked: confirmCancel.open()
                    }
                }
                Text {
                    objectName: "requestResult"
                    Layout.fillWidth: true
                    visible: page.resultText.length > 0
                    text: page.resultText
                    color: page.resultIsError ? Theme.error : Theme.success
                    font.pixelSize: Theme.fontSizeBody
                    wrapMode: Text.Wrap
                    Accessible.role: Accessible.AlertMessage
                    Accessible.name: text
                }
            }
        }
    }

    AppDialog {
        id: confirmRequest
        objectName: "confirmRequest"
        title: qsTr("Request this?")
        text: page.isSeries
            ? qsTr("Request %1 on %2: seasons %3.").arg(page.details.name).arg(Requests.serverHost).arg(page.chosenNumbers.join(", "))
            : qsTr("Request %1 on %2.").arg(page.details.name).arg(Requests.serverHost)
        acceptText: qsTr("Request")
        onAccepted: page.details.request(false, page.chosenNumbers)
    }
    AppDialog {
        id: confirmAll
        objectName: "confirmAll"
        title: qsTr("Request all seasons?")
        text: qsTr("Every season of %1 that is not available yet will be requested. This can be a lot of downloads.").arg(page.details.name)
        acceptText: qsTr("Request all")
        onAccepted: page.details.request(true, [])
    }
    AppDialog {
        id: confirmCancel
        objectName: "confirmCancelMine"
        title: qsTr("Cancel your request?")
        text: qsTr("The request for %1 is withdrawn. You can request it again later.").arg(page.details.name)
        acceptText: qsTr("Cancel request")
        rejectText: qsTr("Keep it")
        destructive: true
        onAccepted: page.details.cancelMine()
    }
}
