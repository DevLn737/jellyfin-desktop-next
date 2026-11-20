.pragma library
// The one tooltip on screen at a time, shared by every ToolTipLabel, and
// when the last one hid: right after one, the next shows without the wait.
var current = null
var lastHidden = 0
