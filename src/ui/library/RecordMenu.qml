import QtQuick
import Jfd.Ui.Controls
import Jfd.Ui.Library

// The recording actions for one programme of the guide or a channel's
// page, as a menu (the Menu key, a gamepad's View button): Record and
// Record series for one nothing records, Cancel recording and Cancel
// series recording for one that is. `program` has id, timerId,
// seriesTimerId and isSeries. Only there while Library.recorder.available.
PopupMenu {
    id: menu

    property var program: ({})
    readonly property bool available: Library.recorder !== null && Library.recorder.available
    readonly property var actions: menu.actionsFor(menu.program)

    objectName: "recordMenu"
    minimumWidth: 280
    entries: menu.actions.map((a) => a.text)
    onActivated: (index) => menu.run(menu.actions[index].name)

    // The actions for programme `p`: names and texts.
    function actionsFor(p) {
        if (!menu.available || !p || !p.id) return []
        const list = []
        if (p.timerId) {
            list.push({ name: "cancel", text: qsTr("Cancel recording") })
        } else {
            list.push({ name: "record", text: qsTr("Record") })
            if (p.isSeries && !p.seriesTimerId) list.push({ name: "recordSeries", text: qsTr("Record series") })
        }
        if (p.seriesTimerId) list.push({ name: "cancelSeries", text: qsTr("Cancel series recording") })
        return list
    }

    function runFor(p, name) {
        const recorder = Library.recorder
        if (!recorder || !p || !p.id) return
        switch (name) {
        case "record": recorder.record(p.id); break
        case "recordSeries": recorder.recordSeries(p.id); break
        case "cancel": recorder.cancel(p.timerId); break
        case "cancelSeries": recorder.cancelSeries(p.seriesTimerId); break
        }
    }

    function run(name) { menu.runFor(menu.program, name) }

    // The first action at once (the R key): records what nothing records,
    // cancels what is.
    function toggle() {
        if (menu.actions.length > 0) menu.run(menu.actions[0].name)
    }

    // Opens beside `item`, for `p` when given (else `program`); false when
    // there is nothing to do.
    function openFor(item, p) {
        if (p !== undefined) menu.program = p || {}
        if (menu.actions.length === 0) return false
        menu.anchorItem = item
        menu.open()
        return true
    }
}
