import QtQuick
import Jfd.Ui.Theme

// Base for every page the Navigator opens. The navigator fills in the
// route ("item/abc"), its name ("item") and argument ("abc"). `title` is
// the window title while the page is current.
FocusScope {
    property string route
    property string routeName
    property string routeArgument
    property string title
    property bool hasTopBar: false
    // The page draws under the navigation bar (Home's banner): it starts at
    // the window's top and keeps its content below `topInset`, which the
    // shell sets to the bar's height.
    property bool underTopBar: false
    property real topInset: 0
    // While true the bar has no fill of its own (the page is at its top).
    property bool topBarTransparent: false
}
