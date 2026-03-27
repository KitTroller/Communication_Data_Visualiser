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

            ValueAxis { id: axisI; min: -1.5; max: 1.5; tickCount: 7; labelFormat: "%.1f"; color: "#555555"; labelsColor: "#00FFCC"; gridLineColor: "#4400FFCC" }
            ValueAxis { id: axisQ; min: -1.5; max: 1.5; tickCount: 7; labelFormat: "%.1f"; color: "#555555"; labelsColor: "#00FFCC"; gridLineColor: "#4400FFCC" }

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

                    // --- THE FIX: Turn off the neon shadow for the background fade ---
                    ctx.shadowBlur = 0;
                    ctx.shadowColor = "transparent";

                    // 1. Draw a 8% transparent black square to slowly fade old dots
                    ctx.fillStyle = "rgba(5, 5, 5, 0.08)";
                    ctx.fillRect(0, 0, width, height);

                    // 2. Setup the glowing neon cyan pen for the dots
                    ctx.fillStyle = "#00FFCC";
                    ctx.shadowColor = "#00FFCC";
                    ctx.shadowBlur = 10; // Turn the cyber-glow back on

                    // 3. Draw every new point currently waiting in the buffer
                    while (pointBuffer.length > 0) {
                        var pt = pointBuffer.pop();
                        var mapped = constellationChart.mapToPosition(Qt.point(pt.i, pt.q), mappingSeries);

                        // --- THE MASTERPIECE: Dynamic Error Rendering ---
                        if (pt.isError) {
                            ctx.fillStyle = "#FF3333";   // Neon Red
                            ctx.shadowColor = "#FF3333";
                        } else {
                            ctx.fillStyle = "#00FFCC";   // Neon Cyan
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