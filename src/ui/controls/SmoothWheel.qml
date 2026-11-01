import QtQuick
import Jfd.Ui.Theme

// Mouse-wheel scrolling for the Flickable or ListView it is declared in:
// eased instead of jumping, and a fixed step per wheel notch (Qt's own step
// is small and abrupt). Touchpads, which send exact pixel deltas, move the
// view one to one. Rapid notches add up. Declare it inside the view:
//
//   ListView { SmoothWheel {} }
//
// Only the plain vertical wheel is handled; Shift + wheel and sideways
// swipes stay with the view (CardRow scrolls its row sideways on Shift).
WheelHandler {
    id: handler

    // A handler declared in a Flickable has its content item as parent.
    readonly property Flickable view: parent && parent.hasOwnProperty("contentY") ? parent : parent.parent
    // Pixels per wheel notch.
    property real notch: 130
    property real goal: 0

    orientation: Qt.Vertical
    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
    target: null

    function limit(value) {
        const top = view.originY - view.topMargin
        const bottom = Math.max(top, view.originY + view.contentHeight - view.height + view.bottomMargin)
        return Math.max(top, Math.min(bottom, value))
    }

    onWheel: event => {
        if (event.modifiers !== Qt.NoModifier || !view || view.contentHeight <= view.height)
            return
        const exact = event.pixelDelta.y !== 0
        const delta = exact ? event.pixelDelta.y : event.angleDelta.y / 120 * notch
        if (!anim.running)
            goal = view.contentY
        goal = limit(goal - delta)
        anim.stop()
        if (exact) {
            view.contentY = goal
        } else {
            anim.from = view.contentY
            anim.to = goal
            anim.start()
        }
        event.accepted = true
    }

    property NumberAnimation anim: NumberAnimation {
        target: handler.view
        property: "contentY"
        duration: Theme.durationNormal + Theme.durationFast
        easing.type: Easing.OutCubic
    }
}
