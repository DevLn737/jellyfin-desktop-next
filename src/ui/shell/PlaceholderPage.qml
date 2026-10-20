import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls

// Shown for routes whose screen is not built yet.
RoutePage {
    id: page

    title: routeName

    Column {
        anchors.centerIn: parent
        spacing: Theme.spacing

        Heading {
            anchors.horizontalCenter: parent.horizontalCenter
            text: page.route
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("This screen is not ready yet.")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeBody
        }
    }
}
