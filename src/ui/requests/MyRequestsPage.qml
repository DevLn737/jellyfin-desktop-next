pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell

// The user's own requests with where each one stands. A request still
// waiting for approval can be cancelled after a confirmation.
RoutePage {
    id: page

    property var mine: null
    property string errorText
    property int cancelRow: -1

    title: qsTr("My requests")

    Component.onCompleted: {
        mine = Requests.createMine(page)
        mine.reload()
    }

    Connections {
        target: page.mine
        function onCancelFinished(outcome) {
            page.errorText = outcome === 0 ? "" : Requests.outcomeText(outcome)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.pageMargin
        anchors.rightMargin: Theme.pageMargin
        anchors.topMargin: Theme.spacingLarge
        spacing: Theme.spacing

        Heading { text: qsTr("My requests"); Layout.fillWidth: true }

        Text {
            objectName: "myRequestsError"
            Layout.fillWidth: true
            readonly property int code: page.mine.error
            visible: text.length > 0
            text: page.errorText.length > 0 ? page.errorText : (code !== 0 ? Requests.outcomeText(code) : "")
            color: Theme.error
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.Wrap
            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
        }

        ListView {
            id: list
            objectName: "myRequestsList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            focus: true
            activeFocusOnTab: true
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds
            spacing: Theme.spacing
            model: page.mine
            onAtYEndChanged: if (atYEnd && page.mine.hasMore) page.mine.loadMore()

            delegate: FocusScope {
                id: row
                required property var model
                required property int index
                readonly property string kindText: model.mediaType === "tv" ? qsTr("Series") : qsTr("Film")
                readonly property var seasons: model.seasons
                readonly property string detail: {
                    var parts = [kindText]
                    if ((model.subtitle || "").length > 0)
                        parts.push(model.subtitle)
                    if (model.mediaType === "tv" && seasons.length > 0)
                        parts.push(qsTr("Seasons: %1").arg(seasons.join(", ")))
                    return parts.join(" · ")
                }

                width: list.width
                height: 96
                activeFocusOnTab: true
                Accessible.name: [model.name, detail, Requests.requestStatusText(model.requestStatus)].join(", ")

                Keys.onReturnPressed: Navigator.navigate("seerr-title/" + model.key)
                Keys.onEnterPressed: Navigator.navigate("seerr-title/" + model.key)

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radius
                    color: Theme.surface
                    FocusFrame { shown: row.activeFocus }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: Navigator.navigate("seerr-title/" + row.model.key)
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSmall
                    spacing: Theme.spacing

                    Rectangle {
                        Layout.preferredWidth: 56
                        Layout.preferredHeight: 80
                        radius: Theme.radiusSmall
                        color: Theme.surfaceRaised
                        clip: true
                        Image {
                            anchors.fill: parent
                            source: Requests.image(row.model.imageUrl)
                            sourceSize.width: 112
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingTiny
                        Text {
                            Layout.fillWidth: true
                            text: row.model.name || "…"
                            color: Theme.text
                            font.pixelSize: Theme.fontSizeLarge
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: row.detail
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeSmall
                            elide: Text.ElideRight
                        }
                        StatusBadge {
                            kind: Requests.requestStatusKind(row.model.requestStatus)
                            text: Requests.requestStatusText(row.model.requestStatus)
                        }
                    }
                    ActionButton {
                        objectName: "cancelRequestButton"
                        visible: row.model.canCancel
                        text: qsTr("Cancel request")
                        Accessible.name: qsTr("Cancel the request for %1").arg(row.model.name)
                        onClicked: {
                            page.cancelRow = row.index
                            confirmCancel.open()
                        }
                    }
                }
            }

            LoadingIndicator {
                anchors.centerIn: parent
                visible: page.mine.loading && list.count === 0
            }
            Text {
                anchors.centerIn: parent
                visible: !page.mine.loading && list.count === 0 && page.mine.error === 0
                text: qsTr("You have not requested anything yet.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
            }
        }
    }

    AppDialog {
        id: confirmCancel
        objectName: "cancelRequestConfirm"
        title: qsTr("Cancel this request?")
        text: qsTr("The request is withdrawn. You can request it again later.")
        acceptText: qsTr("Cancel request")
        rejectText: qsTr("Keep it")
        destructive: true
        onAccepted: page.mine.cancel(page.cancelRow)
    }
}
