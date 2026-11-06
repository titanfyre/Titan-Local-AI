pragma ComponentBehavior: Bound
import QtQuick

Row {
    id: root
    required property var theme
    property bool running: true
    spacing: 5
    Repeater {
        model: 3
        Rectangle {
            id: dot
            required property int index
            width: 5; height: 5; radius: 3
            color: root.theme.accent
            opacity: 0.6
            SequentialAnimation {
                running: root.running && root.visible && !root.theme.reducedMotion
                loops: Animation.Infinite
                PauseAnimation { duration: dot.index * 130 }
                NumberAnimation { target: dot; property: "opacity"; to: 1; duration: 280 }
                NumberAnimation { target: dot; property: "opacity"; to: 0.3; duration: 380 }
                PauseAnimation { duration: (2-dot.index)*130 + 100 }
            }
        }
    }
}
