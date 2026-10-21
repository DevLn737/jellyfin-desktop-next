import QtQuick
import QtQuick.Window
import Jfd.Ui.Player

// The window's content with HDR10 output in mind. Children go into the
// interface layer. With an HDR10 swap chain (HdrOutput.active) the layer
// encodes them to PQ at the SDR white level, and the player puts the video
// underneath, outside the layer, since mpv already renders it in PQ. The
// window behind should be black then: its clear colour is not encoded.
Item {
    id: scene

    default property alias content: ui.data
    readonly property bool hdr: HdrOutput.active && HdrOutput.shader.length > 0

    readonly property var window: Window.window

    onWindowChanged: HdrOutput.watch(window)
    Component.onCompleted: {
        HdrOutput.videoLayer = videoLayer
        HdrOutput.watch(window)
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
        visible: HdrOutput.videoShown
    }

    Item {
        id: videoLayer
        objectName: "hdrVideoLayer"
        anchors.fill: parent
    }

    Item {
        id: ui
        anchors.fill: parent
        layer.enabled: scene.hdr
        layer.effect: ShaderEffect {
            property real sdrWhiteNits: HdrOutput.sdrWhiteNits
            fragmentShader: HdrOutput.shader
        }
    }
}
