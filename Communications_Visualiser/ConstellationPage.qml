import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCharts

Item {
    id: root

    // A fast javascript array to hold incoming data before the screen draws it
    property var pointBuffer: []
    function updateConstellation(i, q, isError) {
            pointBuffer.push({i: i, q: q, isError: isError})
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

                    // 1. Draw the phosphor fade (buries the background)
                    ctx.fillStyle = "rgba(5, 5, 5, 0.08)";
                    ctx.fillRect(0, 0, width, height);
                    // --- 2. THE NEW MATH: DYNAMIC DECISION BOUNDARIES ---
                    if (dspBackend.modType < 4) {
                        ctx.lineWidth = 1;
                        // Dynamically inject the opacity variable into the rgba string
                        ctx.strokeStyle = "rgba(0, 230, 240, " + dspBackend.gridOpacity + ")";
                        ctx.beginPath();

                        // Calculate M-size based on dropdown index (0->4, 1->16, 2->64, 3->256)
                        var mSize = Math.pow(2, (dspBackend.modType + 1) * 2);
                        var numLines = Math.sqrt(mSize) - 1;

                        // liquid-dsp normalizes QAM points within a ~[-1.2, 1.2] boundary
                        var limit = 1.2;
                        var gap = (limit * 2) / (numLines + 1);

                        // Draw the mathematical crosshairs
                        for(var k = 1; k <= numLines; k++) {
                            var val = -limit + (k * gap);

                            // Vertical Lines
                            var p1 = constellationChart.mapToPosition(Qt.point(val, -1.5), mappingSeries);
                            var p2 = constellationChart.mapToPosition(Qt.point(val, 1.5), mappingSeries);
                            ctx.moveTo(p1.x, p1.y);
                            ctx.lineTo(p2.x, p2.y);

                            // Horizontal Lines
                            var p3 = constellationChart.mapToPosition(Qt.point(-1.5, val), mappingSeries);
                            var p4 = constellationChart.mapToPosition(Qt.point(1.5, val), mappingSeries);
                            ctx.moveTo(p3.x, p3.y);
                            ctx.lineTo(p4.x, p4.y);
                        }
                        ctx.stroke();
                    }

                    // 3. Setup the glowing neon pen for the dots
                    ctx.fillStyle = "#00FFCC";
                    ctx.shadowColor = "#00FFCC";
                    ctx.shadowBlur = 10;

                    // 4. Draw the dots
                    while (pointBuffer.length > 0) {
                        var pt = pointBuffer.pop();
                        var mapped = constellationChart.mapToPosition(Qt.point(pt.i, pt.q), mappingSeries);

                        if (pt.isError) {
                            ctx.fillStyle = "#FF3333";   // Error! Neon Red
                            ctx.shadowColor = "#FF3333";
                        } else {
                            ctx.fillStyle = "#00FFCC";   // Nominal: Neon Cyan
                            ctx.shadowColor = "#00FFCC";
                        }

                        ctx.beginPath();
                        ctx.arc(mapped.x, mapped.y, 4, 0, 2 * Math.PI);
                        ctx.fill();
                    }
                }
            }
        }

        // RIGHT SIDE: The Control Panel

    }
}