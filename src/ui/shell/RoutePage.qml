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
}
