import QtQuick 2.15

Item {
    id: root
    width: 255
    height: 292

    property int value: 0
    property var options: []
    property string title: ""

    signal selectorChanged(int index)

    onValueChanged: {
        value = Math.max(0, Math.min(3, value))
    }

    // Affichage du sprite découpé selon la valeur (4 tranches de 255px sur 1020px)
    Item {
        anchors.fill: parent
        clip: true

        Image {
            id: sprite
            source: "qrc:/img/led_rotary_red.png"
            height: parent.height
            width: parent.width * 4
            x: -root.value * parent.width
            smooth: true
        }
    }

    // Affichage circulaire des textes autour de la couronne
    Repeater {
        model: root.options

        Text {
            readonly property var angles: [130.0, 108.0, 72.0, 50.0]
            readonly property real rad: angles[index] * Math.PI / 180.0
            readonly property real centerX: root.width / 2.0
            readonly property real centerY: (root.width - ((root.width - root.height) / 2.0)) / 2.0
            readonly property real radius: root.width / 1.9

            text: modelData
            font.family: "Sans-Serif"
            font.pixelSize: 11
            font.bold: true
            color: index === root.value ? "#168de0" : "#738297"

            x: centerX + radius * Math.cos(rad) - (width / 2)
            y: centerY - radius * Math.sin(rad) - (height / 2)
        }
    }

    // Gestion du clic et de la molette
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor

        onPressed: function(mouse) {
            var centerX = root.width / 2.0
            var centerY = (root.width - ((root.width - root.height) / 2.0)) / 2.0
            var angle = Math.atan2(mouse.y - centerY, mouse.x - centerX) * 180.0 / Math.PI

            var angles = [-145, -110, -75, -45]
            var nearest = 0
            var bestDistance = 1000

            for (var i = 0; i < 4; ++i) {
                var d = Math.abs(angle - angles[i])
                if (d < bestDistance) {
                    bestDistance = d
                    nearest = i
                }
            }

            if (bestDistance < 55 && root.value !== nearest) {
                root.value = nearest
                root.selectorChanged(root.value)
            }
        }

        onWheel: function(wheel) {
            var direction = wheel.angleDelta.y > 0 ? 1 : -1
            var nextVal = (root.value + direction + 4) % 4
            if (root.value !== nextVal) {
                root.value = nextVal
                root.selectorChanged(root.value)
            }
            wheel.accepted = true
        }
    }
}
