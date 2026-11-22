import QtQuick 2.15

Item {
    id: root
    width: 84
    height: 84

    property int value: 128
    property int maxValue: 255
    signal knobValueChanged(int newValue)

    onValueChanged: {
        var limit = root.maxValue > 0 ? root.maxValue : 255
        var clamped = Math.max(0, Math.min(limit, value))
        if (value !== clamped) {
            value = clamped
            return
        }
        canvas.requestPaint()
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        antialiasing: true

        Component.onCompleted: requestPaint()

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()

            var cx = width / 2.0
            var cy = height / 2.0
            var limit = root.maxValue > 0 ? root.maxValue : 255
            var ratio = Math.max(0.0, Math.min(1.0, root.value / limit))

            // --- 0. HALO BLEU ÉTENDU ET DYNAMIQUE ---
            var haloOuterRadius = 28.0 + (13.5 * ratio)
            var coreAlpha = 0.14 + (0.84 * ratio)
            var midAlpha = 0.05 + (0.50 * ratio)

            var haloGrad = ctx.createRadialGradient(cx, cy, 24.0, cx, cy, haloOuterRadius)
            haloGrad.addColorStop(0.0, "rgba(35, 185, 255, " + coreAlpha + ")")
            haloGrad.addColorStop(0.50, "rgba(18, 125, 235, " + midAlpha + ")")
            haloGrad.addColorStop(1.0, "rgba(5, 50, 150, 0.0)")

            ctx.beginPath()
            ctx.arc(cx, cy, haloOuterRadius, 0, 2 * Math.PI)
            ctx.fillStyle = haloGrad
            ctx.fill()

            // --- 1. BAGUE EXTÉRIEURE BLEUE TRANSLUCIDE ---
            var ringRadius = 28.0
            var ringGrad = ctx.createLinearGradient(cx, cy - ringRadius, cx, cy + ringRadius)
            var blueBoost = Math.round(50 * ratio)
            ringGrad.addColorStop(0.0, "rgb(" + (20 + blueBoost) + ", " + (125 + blueBoost) + ", 240)")
            ringGrad.addColorStop(0.45, "#0d4a96")
            ringGrad.addColorStop(0.85, "#08295c")
            ringGrad.addColorStop(1.0, "#1967c2")

            ctx.beginPath()
            ctx.arc(cx, cy, ringRadius, 0, 2 * Math.PI)
            ctx.fillStyle = ringGrad
            ctx.fill()

            ctx.lineWidth = 1.8
            ctx.strokeStyle = "rgba(55, 175, 255, " + (0.55 + 0.45 * ratio) + ")"
            ctx.stroke()

            // --- 2. ZONE SOMBRE INTERNE ET CANNELURES ROTATIVES ---
            var innerCavityRadius = 24.8
            var cavityGrad = ctx.createLinearGradient(cx, cy - innerCavityRadius, cx, cy + innerCavityRadius)
            cavityGrad.addColorStop(0.0, "#051124")
            cavityGrad.addColorStop(0.6, "#03070d")
            cavityGrad.addColorStop(1.0, "#0b2242")

            ctx.beginPath()
            ctx.arc(cx, cy, innerCavityRadius, 0, 2 * Math.PI)
            ctx.fillStyle = cavityGrad
            ctx.fill()

            ctx.save()
            ctx.beginPath()
            ctx.arc(cx, cy, innerCavityRadius, 0, 2 * Math.PI)
            ctx.clip()

            var numFlutes = 48
            var rotAngle = (ratio * 270.0) * Math.PI / 180.0
            for (var i = 0; i < numFlutes; i++) {
                var a = ((i / numFlutes) * 2 * Math.PI) + rotAngle
                var fx1 = cx + 20.0 * Math.cos(a)
                var fy1 = (cy + 2.2) + 20.0 * Math.sin(a)
                var fx2 = cx + 25.0 * Math.cos(a)
                var fy2 = (cy - 1.4) + 25.0 * Math.sin(a)

                var topFactor = Math.max(0, -Math.sin(a))
                var alpha = 0.16 + 0.48 * topFactor
                ctx.beginPath()
                ctx.moveTo(fx1, fy1)
                ctx.lineTo(fx2, fy2)
                ctx.lineWidth = 1.4
                ctx.strokeStyle = "rgba(170, 215, 255, " + alpha + ")"
                ctx.stroke()
            }
            ctx.restore()

            // --- 3. DÔME CHROMÉ TRÈS CONVEXE ---
            var domeCy = cy + 2.3
            var domeRadius = 20.8

            var chromeBase = ctx.createRadialGradient(
                cx - 3.5, domeCy - 7.5, 1.5,
                cx, domeCy + 1.0, domeRadius * 1.15
            )
            chromeBase.addColorStop(0.00, "#ffffff")
            chromeBase.addColorStop(0.22, "#e4ebf2")
            chromeBase.addColorStop(0.55, "#a6b2c0")
            chromeBase.addColorStop(0.85, "#687687")
            chromeBase.addColorStop(1.00, "#7b899b")

            ctx.beginPath()
            ctx.arc(cx, domeCy, domeRadius, 0, 2 * Math.PI)
            ctx.fillStyle = chromeBase
            ctx.fill()

            ctx.save()
            ctx.beginPath()
            ctx.arc(cx, domeCy, domeRadius, 0, 2 * Math.PI)
            ctx.clip()

            var topGlow = ctx.createRadialGradient(
                cx - 3.0, domeCy - 8.5, 0.5,
                cx - 2.0, domeCy - 6.0, 14.0
            )
            topGlow.addColorStop(0.0, "rgba(255, 255, 255, 0.85)")
            topGlow.addColorStop(0.6, "rgba(255, 255, 255, 0.25)")
            topGlow.addColorStop(1.0, "rgba(255, 255, 255, 0.0)")

            ctx.beginPath()
            ctx.ellipse(cx - 14.0, domeCy - 17.5, 28.0, 18.0)
            ctx.fillStyle = topGlow
            ctx.fill()

            ctx.beginPath()
            ctx.arc(cx - 1.5, domeCy - 1.2, domeRadius - 0.8, 1.08 * Math.PI, 1.70 * Math.PI, false)
            ctx.lineWidth = 2.4
            ctx.lineCap = "round"
            ctx.strokeStyle = "rgba(255, 255, 255, 0.90)"
            ctx.stroke()

            ctx.beginPath()
            ctx.arc(cx, domeCy, domeRadius - 0.7, 1.15 * Math.PI, 1.85 * Math.PI, false)
            ctx.lineWidth = 1.6
            ctx.strokeStyle = "rgba(40, 155, 255, " + (0.35 + 0.55 * ratio) + ")"
            ctx.stroke()

            ctx.restore()

            ctx.beginPath()
            ctx.arc(cx, domeCy, domeRadius, 0, 2 * Math.PI)
            ctx.lineWidth = 1.1
            ctx.strokeStyle = "#1a2636"
            ctx.stroke()

            // --- 4. PASTILLE REPÈRE CREUSE ---
            var indicatorAngle = (225.0 - ratio * 270.0) * Math.PI / 180.0
            var distFromCenter = 12.8
            var ix = cx + distFromCenter * Math.cos(indicatorAngle)
            var iy = domeCy - distFromCenter * Math.sin(indicatorAngle)
            var dotRadius = 3.5

            var dotGrad = ctx.createLinearGradient(ix, iy - dotRadius, ix, iy + dotRadius)
            dotGrad.addColorStop(0.0, "#4e5866")
            dotGrad.addColorStop(0.5, "#cfd6de")
            dotGrad.addColorStop(1.0, "#ffffff")

            ctx.beginPath()
            ctx.arc(ix, iy, dotRadius, 0, 2 * Math.PI)
            ctx.fillStyle = dotGrad
            ctx.fill()
            ctx.lineWidth = 1.1
            ctx.strokeStyle = "#2b3542"
            ctx.stroke()

            ctx.beginPath()
            ctx.arc(ix, iy, 1.2, 0, 2 * Math.PI)
            ctx.fillStyle = ratio > 0.05 ? "#1b78d4" : "#556273"
            ctx.fill()
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        property real lastY: 0

        onPressed: function(mouse) {
            lastY = mouse.y
        }

        onPositionChanged: function(mouse) {
                    if (!(mouse.buttons & Qt.LeftButton)) return
                    var delta = lastY - mouse.y
                    lastY = mouse.y

                    var limit = root.maxValue > 0 ? root.maxValue : 255
                    var speed = (mouse.modifiers & Qt.ShiftModifier) ? 0.3 : Math.max(0.5, limit / 140.0)
                    var nextVal = Math.max(0, Math.min(limit, Math.round(root.value + delta * speed)))

                    if (root.value !== nextVal) {
                        root.value = nextVal
                        knobBridge.notifyValueFromQml(root.value)
                    }
                }

                onDoubleClicked: {
                    var limit = root.maxValue > 0 ? root.maxValue : 255
                    var mid = Math.round(limit / 2)
                    if (root.value !== mid) {
                        root.value = mid
                        knobBridge.notifyValueFromQml(root.value)
                    }
                }

                onWheel: function(wheel) {
                    var limit = root.maxValue > 0 ? root.maxValue : 255
                    var step = (wheel.modifiers & Qt.ShiftModifier) ? 1 : (limit > 100 ? 2 : 1)
                    var direction = wheel.angleDelta.y > 0 ? step : -step
                    var nextVal = Math.max(0, Math.min(limit, root.value + direction))

                    if (root.value !== nextVal) {
                        root.value = nextVal
                        knobBridge.notifyValueFromQml(root.value)
                    }
                    wheel.accepted = true
                }
    }
}
