import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCharts

Item {
    id: root

    // A fast javascript array to hold incoming data before the screen draws it
    property var pointBuffer: []
    property real lastI: NaN
    property real lastQ: NaN

    onVisibleChanged: {
            if (visible) clearCanvas()
        }

    function clearCanvas() {
            var ctx = phosphorCanvas.getContext("2d");
            ctx.fillStyle = "#050505";
            ctx.fillRect(0, 0, phosphorCanvas.width, phosphorCanvas.height);
            pointBuffer = [];
            lastI = NaN;
            lastQ = NaN;
            phosphorCanvas.requestPaint();
        }

    // Reset trajectory when modulation changes to avoid jump-lines
    Connections {
        target: dspBackend
        function onModTypeChanged() {
            pointBuffer = []
            lastI = NaN
            lastQ = NaN
            phosphorCanvas.requestPaint()
        }
    }

    function updateConstellation(i, q, isError, isPeak) {
            pointBuffer.push({i: i, q: q, isError: isError, isPeak: isPeak})

            // --- FIX 2: Clamp the memory so it never hoards points while hidden ---
            if (pointBuffer.length > 200) {
                pointBuffer.shift();
            }
            phosphorCanvas.requestPaint()
        }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // LEFT SIDE: The Visualizer
        ChartView {
            id: constellationChart
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 20

            backgroundColor: "transparent"
            title: "DSP ENGINE CONSTELLATION"
            titleColor: "white"
            titleFont.pointSize: 18
            legend.visible: false
            antialiasing: true

            ValueAxis { id: axisI; min: -1.5; max: 1.5; tickCount: 7; labelFormat: "%.1f"; color: "#555555"; labelsColor: "#00FFCC"; gridLineColor: "transparent" }
            ValueAxis { id: axisQ; min: -1.5; max: 1.5; tickCount: 7; labelFormat: "%.1f"; color: "#555555"; labelsColor: "#00FFCC"; gridLineColor: "transparent" }

            // An invisible series purely to allow the Canvas to mathematically map the axes
            LineSeries { id: mappingSeries; axisX: axisI; axisY: axisQ; visible: false }

            // THE MASTERPIECE: The Phosphor Radar Canvas
            Canvas {
                id: phosphorCanvas
                anchors.fill: parent
                //renderTarget: Canvas.FramebufferObject // Forces GPU rendering
                renderTarget: Canvas.Image

                onPaint: {
                    var ctx = getContext("2d");

                    ctx.shadowBlur = 0;
                    ctx.shadowColor = "transparent";

                    // --- FIX 3: DYNAMIC TIME-SCALED PHOSPHOR FADE ---
                    // Calculate a base fade (0.02 for lines, 0.08 for dots)
                    var baseFade = dspBackend.useRrc ? 0.02 : 0.08;

                    // Multiply the fade by how slow the simulation is running
                    // If running at 150ms, fade is 10x stronger per frame to compensate.
                    var speedMultiplier = dspBackend.timerInterval / 15.0;
                    var fadeOpacity = Math.min(baseFade * speedMultiplier, 1.0);

                    ctx.fillStyle = "rgba(5, 5, 5, " + fadeOpacity + ")";
                    ctx.fillRect(0, 0, width, height);

                    // --- 2. THE NEW MATH: DYNAMIC DECISION BOUNDARIES ---
                    if (dspBackend.modType < 4) {
                        ctx.lineWidth = 1;
                        ctx.strokeStyle = "rgba(0, 230, 240, " + dspBackend.gridOpacity + ")";
                        ctx.beginPath();

                        var mSize = Math.pow(2, (dspBackend.modType + 1) * 2);
                        var numLines = Math.sqrt(mSize) - 1;
                        var limit = 1.2;
                        var gap = (limit * 2) / (numLines + 1);

                        for(var k = 1; k <= numLines; k++) {
                            var val = -limit + (k * gap);
                            var p1 = constellationChart.mapToPosition(Qt.point(val, -1.5), mappingSeries);
                            var p2 = constellationChart.mapToPosition(Qt.point(val, 1.5), mappingSeries);
                            ctx.moveTo(p1.x, p1.y); ctx.lineTo(p2.x, p2.y);
                            var p3 = constellationChart.mapToPosition(Qt.point(-1.5, val), mappingSeries);
                            var p4 = constellationChart.mapToPosition(Qt.point(1.5, val), mappingSeries);
                            ctx.moveTo(p3.x, p3.y); ctx.lineTo(p4.x, p4.y);
                        }
                        ctx.stroke();
                    }

                    // 3. Draw the trajectory and points
                    ctx.lineWidth = 2;
                    
                    while (pointBuffer.length > 0) {
                        var pt = pointBuffer.shift(); // FIFO order is critical for lines
                        var mapped = constellationChart.mapToPosition(Qt.point(pt.i, pt.q), mappingSeries);

                        // Draw line from previous point if RRC is enabled
                        if (dspBackend.useRrc && !isNaN(lastI)) {
                            var prevMapped = constellationChart.mapToPosition(Qt.point(lastI, lastQ), mappingSeries);
                            ctx.beginPath();
                            ctx.moveTo(prevMapped.x, prevMapped.y);
                            ctx.lineTo(mapped.x, mapped.y);
                            ctx.strokeStyle = pt.isError ? "rgba(255, 51, 51, 0.6)" : "rgba(0, 255, 204, 0.4)";
                            ctx.stroke();
                        }

                        // Draw the sample point (dot)
                        if (pt.isPeak || !dspBackend.useRrc) {
                            ctx.beginPath();
                            ctx.fillStyle = pt.isError ? "#FF3333" : "#00FFCC";
                            ctx.shadowColor = ctx.fillStyle;
                            // Add a tiny bit of glow to the peaks to make them pop out of the wire
                            ctx.shadowBlur = dspBackend.useRrc ? 4 : (pt.isError ? 12 : 6);
                            ctx.arc(mapped.x, mapped.y, 2.5, 0, 2 * Math.PI);
                            ctx.fill();
                        }
                        lastI = pt.i;
                        lastQ = pt.q;
                    }
                }
            }
        }

        // RIGHT SIDE: The Control Panel

    }
}