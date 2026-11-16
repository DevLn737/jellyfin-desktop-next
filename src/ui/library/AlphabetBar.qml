pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme
import Jfd.Ui.Controls

// The letters beside a library listed by name: choosing one scrolls to the
// first title starting with it (`letterChosen`). "#" is the start. The
// letters are the language's own alphabet; where that is not the Latin one
// (Russian), a switch at the top changes between the two, since a library
// holds titles in both. For the pointer; from the keyboard, typing a letter
// on the grid does the same.
Item {
    id: bar

    readonly property string latin: "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    //: The letters titles start with, in order, for the library's
    //: alphabet bar; leave out letters no word starts with.
    readonly property string localLetters: qsTr("ABCDEFGHIJKLMNOPQRSTUVWXYZ")
    readonly property bool twoAlphabets: localLetters !== latin
    property bool showLatin: false
    readonly property var letters: ["#"].concat((twoAlphabets && !showLatin ? localLetters : latin).split(""))
    readonly property int step: Math.max(12, Math.min(26, Math.floor((height - switchButton.height) / Math.max(1, letters.length))))

    signal letterChosen(string letter)

    implicitWidth: 36

    T.AbstractButton {
        id: switchButton
        objectName: "alphabetSwitch"
        anchors.horizontalCenter: parent.horizontalCenter
        visible: bar.twoAlphabets
        width: bar.width
        height: visible ? 28 : 0
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        Accessible.name: qsTr("Switch alphabet")
        onClicked: bar.showLatin = !bar.showLatin
        Text {
            anchors.centerIn: parent
            readonly property string other: bar.showLatin ? bar.localLetters : bar.latin
            text: other.charAt(0) + "–" + other.charAt(other.length - 1)
            color: switchButton.hovered ? Theme.text : Theme.accent
            font.pixelSize: Theme.fontSizeSmall
            font.weight: Theme.fontWeightBold
        }
    }

    Column {
        anchors.top: switchButton.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        Repeater {
            model: bar.letters
            T.AbstractButton {
                id: letter
                required property string modelData
                objectName: "letter_" + modelData
                width: bar.width
                height: bar.step
                focusPolicy: Qt.NoFocus
                hoverEnabled: true
                Accessible.name: modelData
                onClicked: bar.letterChosen(modelData)
                Rectangle {
                    anchors.centerIn: parent
                    width: Math.min(parent.width, parent.height + 6)
                    height: parent.height
                    radius: Theme.radiusSmall
                    color: letter.hovered ? Theme.surfaceHover : "transparent"
                }
                Text {
                    anchors.centerIn: parent
                    text: letter.modelData
                    color: letter.hovered ? Theme.text : Theme.textSecondary
                    font.pixelSize: Math.min(Theme.fontSizeSmall, bar.step - 2)
                    font.weight: Font.Medium
                }
            }
        }
    }
}
