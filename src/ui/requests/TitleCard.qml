pragma ComponentBehavior: Bound

import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell

// A Seerr title as a card: poster, name, and the year with the status in
// words. Opens the title's page.
PosterCard {
    id: card

    // The delegate's model roles (TitleListModel).
    required property var model
    required property int index

    readonly property string statusText: Requests.mediaStatusText(model.status)

    title: model.name || ""
    subtitle: statusText.length > 0
        ? ((model.subtitle || "").length > 0 ? model.subtitle + " · " + statusText : statusText)
        : (model.subtitle || "")
    imageSource: Requests.image(model.imageUrl)
    // The check mark is for what can be watched.
    played: model.status === 5

    onClicked: Navigator.navigate("seerr-title/" + model.key)
}
