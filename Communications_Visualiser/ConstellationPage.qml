import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCharts

Item {
    // FIX 1: Renamed from 'root' to stop shadowing Main.qml's 'root'
    id: constellationPageRoot

    property var pointBuffer: []
    property real lastI: NaN
    property real lastQ: NaN

    // FIX 2: A flag to prevent the grid from accumulating into a solid wall
    property bool needsGridRedraw: true

    onVisibleChanged: {
        if (visible)
            clearCanvas();
    }

    function clearCanvas() {
        var ctx = phosphorCanvas.getContext("2d");
        ctx.fillStyle = "#050505";
        ctx.fillRect(0, 0, phosphorCanvas.width, phosphorCanvas.height);
        pointBuffer = [];
        lastI = NaN;
        lastQ = NaN;
        needsGridRedraw = true; // Tell the canvas it is safe to draw the grid once
        phosphorCanvas.requestPaint();
    }

    Connections {
        target: dspBackend
        function onModTypeChanged() {
            clearCanvas();
        }

        // --- DATASET AI EXPORTER ---
        function onTriggerScreenshot(filePath) {
            // Only capture the ChartView, and compress exactly to 224x224 for ResNet/VGG
            constellationChart.grabToImage(function (result) {
                result.saveToFile(filePath);
                // THE SNIPER FIX: Wipe the canvas clean for the next photo!
                clearCanvas();
                dspBackend.confirmImageSaved(); // Tell C++ to advance the loop
            }, Qt.size(224, 224));
        }
    }

    function updateConstellation(i, q, isError, isPeak) {
        pointBuffer.push({
            i: i,
            q: q,
            isError: isError,
            isPeak: isPeak
        });
        if (pointBuffer.length > 200) {
            pointBuffer.shift();
        }
        phosphorCanvas.requestPaint();
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        ChartView {
            id: constellationChart
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 20

            backgroundColor: "transparent"
            plotAreaColor: "transparent" // THE FIX: Ensures axes don't block the canvas
            // THE FIX: Hide title during automation
            title: dspBackend.isAutomating ? "" : "DSP ENGINE CONSTELLATION"
            titleColor: "white"
            titleFont.pointSize: 18
            legend.visible: false
            antialiasing: true

            ValueAxis {
                id: axisI
                min: -2.5
                max: 2.5
                tickCount: 7
                labelFormat: "%.1f"
                color: "#555555"
                labelsColor: "#00FFCC"
                gridLineColor: "transparent"
                // THE FIX: Hide axis text and lines during automation
                labelsVisible: !dspBackend.isAutomating
                lineVisible: !dspBackend.isAutomating
            }
            ValueAxis {
                id: axisQ
                min: -2.5
                max: 2.5
                tickCount: 7
                labelFormat: "%.1f"
                color: "#555555"
                labelsColor: "#00FFCC"
                gridLineColor: "transparent"
                // THE FIX: Hide axis text and lines during automation
                labelsVisible: !dspBackend.isAutomating
                lineVisible: !dspBackend.isAutomating
            }

            LineSeries {
                id: mappingSeries
                axisX: axisI
                axisY: axisQ
                visible: false
            }

            Canvas {
                id: phosphorCanvas
                anchors.fill: parent
                z: -1 // THE FIX: Puts the canvas behind the QtCharts axes!
                renderTarget: Canvas.Image

                onPaint: {
                    var ctx = getContext("2d");
                    ctx.shadowBlur = 0;
                    ctx.shadowColor = "transparent";

                    // --- THE FADE LOGIC ---
                    // 'root' now successfully points to Main.qml's datasetMode property
                    if (!root.datasetMode) {
                        var baseFade = dspBackend.useRrc ? 0.02 : 0.08;
                        var speedMultiplier = dspBackend.timerInterval / 15.0;
                        var fadeOpacity = Math.min(baseFade * speedMultiplier, 1.0);
                        ctx.fillStyle = "rgba(5, 5, 5, " + fadeOpacity + ")";
                        ctx.fillRect(0, 0, width, height);
                    }

                    // --- THE GRID LOGIC (SAFE MODE) ---
                    // Only draw the grid if we are NOT accumulating, OR if the screen was just cleared.
                    if (!root.datasetMode || constellationPageRoot.needsGridRedraw) {
                        ctx.lineWidth = 1;
                        ctx.strokeStyle = "rgba(0, 230, 240, " + dspBackend.gridOpacity + ")";
                        ctx.beginPath();

                        var xAxisLeft = constellationChart.mapToPosition(Qt.point(-2.5, 0), mappingSeries);
                        var xAxisRight = constellationChart.mapToPosition(Qt.point(2.5, 0), mappingSeries);
                        ctx.moveTo(xAxisLeft.x, xAxisLeft.y);
                        ctx.lineTo(xAxisRight.x, xAxisRight.y);

                        var yAxisTop = constellationChart.mapToPosition(Qt.point(0, 2.5), mappingSeries);
                        var yAxisBot = constellationChart.mapToPosition(Qt.point(0, -2.5), mappingSeries);
                        ctx.moveTo(yAxisTop.x, yAxisTop.y);
                        ctx.lineTo(yAxisBot.x, yAxisBot.y);

                        var validGridSize = 0;
                        if (dspBackend.modType === 3)
                            validGridSize = 4;
                        else if (dspBackend.modType === 7)
                            validGridSize = 16;
                        else if (dspBackend.modType === 9)
                            validGridSize = 64;
                        else if (dspBackend.modType === 11)
                            validGridSize = 256;

                        if (validGridSize > 0) {
                            var numLines = Math.sqrt(validGridSize) - 1;
                            var limit = 1.2;
                            var gap = (limit * 2) / (numLines + 1);

                            for (var k = 1; k <= numLines; k++) {
                                var val = -limit + (k * gap);
                                var p1 = constellationChart.mapToPosition(Qt.point(val, -2.5), mappingSeries);
                                var p2 = constellationChart.mapToPosition(Qt.point(val, 2.5), mappingSeries);
                                ctx.moveTo(p1.x, p1.y);
                                ctx.lineTo(p2.x, p2.y);
                                var p3 = constellationChart.mapToPosition(Qt.point(-2.5, val), mappingSeries);
                                var p4 = constellationChart.mapToPosition(Qt.point(2.5, val), mappingSeries);
                                ctx.moveTo(p3.x, p3.y);
                                ctx.lineTo(p4.x, p4.y);
                            }
                        }
                        ctx.stroke();
                        constellationPageRoot.needsGridRedraw = false; // Lock the grid so it doesn't accumulate
                    }

                    // --- DRAW THE DOTS ---
                    ctx.lineWidth = 2;
                    while (pointBuffer.length > 0) {
                        var pt = pointBuffer.shift();
                        var mapped = constellationChart.mapToPosition(Qt.point(pt.i, pt.q), mappingSeries);

                        if (dspBackend.useRrc && !isNaN(lastI)) {
                            var prevMapped = constellationChart.mapToPosition(Qt.point(lastI, lastQ), mappingSeries);
                            ctx.beginPath();
                            ctx.moveTo(prevMapped.x, prevMapped.y);
                            ctx.lineTo(mapped.x, mapped.y);
                            ctx.strokeStyle = pt.isError ? "rgba(255, 51, 51, 0.6)" : "rgba(0, 255, 204, 0.4)";
                            ctx.stroke();
                        }

                        if (pt.isPeak || !dspBackend.useRrc) {
                            ctx.beginPath();

                            // THE FIX: Decouple Infinite Persistence from AI Generation
                            // If AI is automating, force everything to pure cyan.
                            // If a human is watching, allow the red error dots.
                            var dotColor = "#00FFCC";
                            if (pt.isError && !dspBackend.isAutomating) {
                                dotColor = "#FF3333";
                            }

                            ctx.fillStyle = dotColor;
                            ctx.shadowColor = dotColor;

                            // Strip shadows ONLY when the AI is taking screenshots
                            if (dspBackend.isAutomating) {
                                ctx.shadowBlur = 0;
                                ctx.shadowColor = "transparent"; // THE KILL SHOT
                            } else {
                                ctx.shadowBlur = dspBackend.useRrc ? 4 : (pt.isError ? 12 : 6);
                            }

                            ctx.arc(mapped.x, mapped.y, 2.5, 0, 2 * Math.PI);
                            ctx.fill();
                        }
                        lastI = pt.i;
                        lastQ = pt.q;
                    }
                }
            }
        }
    }
}
