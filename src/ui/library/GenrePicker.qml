pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme
import Jfd.Ui.Controls

// Several genres at once: a button that shows the chosen ones and opens a
// list of check boxes. An item matches when it has any chosen genre, as in
// Jellyfin Web. `genres` is every genre on offer, `selected` the chosen
// ones; `toggled(genre)` asks for one to be added or removed.
T.Button {
    id: control

    property var genres: []
    property var selected: []

    signal genreToggled(string genre)
    signal genresCleared()

    implicitWidth: 220
    implicitHeight: Theme.controlHeight
    leftPadding: Theme.spacing
    rightPadding: Theme.spacing + Theme.iconSize
    focusPolicy: Qt.StrongFocus
    font.pixelSize: Theme.fontSizeBody

    text: selected.length === 0 ? qsTr("All genres") : selected.join(", ")
    Accessible.name: qsTr("Genres: %1").arg(text)

    onClicked: popup.visible ? popup.close() : popup.open()
    Keys.onReturnPressed: popup.open()
    Keys.onEnterPressed: popup.open()

    contentItem: Text {
        text: control.text
        color: control.enabled ? Theme.text : Theme.textDisabled
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    Text {
        x: control.width - width - Theme.spacing
        anchors.verticalCenter: parent.verticalCenter
        text: "▾"
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeBody
    }

    background: Rectangle {
        radius: Theme.radius
        color: control.hovered ? Theme.surfaceHover : Theme.surfaceRaised
        border.color: control.selected.length > 0 ? Theme.accent : Theme.border
        FocusFrame { shown: control.visualFocus }
    }

    T.Popup {
        id: popup
        objectName: "genrePopup"
        y: control.height + Theme.spacingTiny
        width: Math.max(control.width, 260)
        implicitHeight: Math.min(list.contentHeight + clear.height + 3 * Theme.spacingTiny, 360)
        padding: Theme.spacingTiny
        focus: true
        onOpened: list.forceActiveFocus()

        background: Rectangle {
            radius: Theme.radius
            color: Theme.surfaceRaised
            border.color: Theme.border
        }

        contentItem: Column {
            spacing: Theme.spacingTiny

            T.ItemDelegate {
                id: clear
                width: parent.width
                implicitHeight: Theme.controlHeight
                leftPadding: Theme.spacing
                enabled: control.selected.length > 0
                contentItem: Text {
                    text: qsTr("Clear")
                    color: clear.enabled ? Theme.accent : Theme.textDisabled
                    font.pixelSize: Theme.fontSizeBody
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle { color: clear.hovered ? Theme.surfaceHover : "transparent" }
                onClicked: control.genresCleared()
            }

            ListView {
                id: list
                objectName: "genreList"
                width: parent.width
                height: popup.availableHeight - clear.height - parent.spacing
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                keyNavigationEnabled: true
                model: control.genres
                Keys.onSpacePressed: if (currentIndex >= 0) control.genreToggled(control.genres[currentIndex])
                Keys.onReturnPressed: if (currentIndex >= 0) control.genreToggled(control.genres[currentIndex])
                delegate: T.ItemDelegate {
                    id: option
                    required property string modelData
                    required property int index
                    readonly property bool chosen: control.selected.indexOf(modelData) >= 0
                    width: ListView.view.width
                    implicitHeight: Theme.controlHeight
                    leftPadding: Theme.spacing
                    highlighted: ListView.isCurrentItem && list.activeFocus
                    Accessible.role: Accessible.CheckBox
                    Accessible.checked: chosen
                    Accessible.name: modelData
                    contentItem: Row {
                        spacing: Theme.spacingSmall
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 18
                            height: 18
                            radius: Theme.radiusSmall
                            color: option.chosen ? Theme.accent : "transparent"
                            border.color: option.chosen ? Theme.accent : Theme.textSecondary
                            Text {
                                anchors.centerIn: parent
                                visible: option.chosen
                                text: "✓"
                                color: Theme.accentText
                                font.pixelSize: Theme.fontSizeSmall
                            }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: option.modelData
                            color: Theme.text
                            font.pixelSize: Theme.fontSizeBody
                        }
                    }
                    background: Rectangle {
                        color: option.hovered || option.highlighted ? Theme.surfaceHover : "transparent"
                    }
                    onClicked: control.genreToggled(modelData)
                }
            }
        }
    }
}
