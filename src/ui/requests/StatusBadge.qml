import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls

// A status as a symbol and words, never colour alone: `kind` is one of
// "available", "pending", "processing", "partial", "declined" (empty hides
// the badge).
Row {
    id: badge

    property string kind
    property string text

    visible: kind.length > 0 && text.length > 0
    spacing: Theme.spacingSmall
    Accessible.role: Accessible.StaticText
    Accessible.name: text

    readonly property url iconFile: {
        switch (kind) {
        case "available": return Icons.check
        case "declined": return Icons.error
        case "pending": return Qt.resolvedUrl("icons/pending.svg")
        case "processing": return Qt.resolvedUrl("icons/processing.svg")
        case "partial": return Qt.resolvedUrl("icons/partial.svg")
        default: return ""
        }
    }
    readonly property color tint: kind === "available" ? Theme.success
        : kind === "declined" ? Theme.error : Theme.textSecondary

    Icon {
        source: badge.iconFile
        color: badge.tint
        size: 18
        anchors.verticalCenter: parent.verticalCenter
    }
    Text {
        text: badge.text
        color: badge.tint
        font.pixelSize: Theme.fontSizeBody
        font.weight: Theme.fontWeightBold
        anchors.verticalCenter: parent.verticalCenter
    }
}
