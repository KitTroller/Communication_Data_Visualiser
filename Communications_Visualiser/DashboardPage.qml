import QtQuick
import QtCharts

Item {
    id: root

    // Properties to manage the scrolling
    property real timeCounter: 0.0
    property real windowSize: 100.0

    // Public function that Main.qml will call to inject data
    function updateTelemetry(rssi, ber, snr, plr) {
        seriesRSSI.append(timeCounter, rssi)
        seriesBER.append(timeCounter, ber)
        seriesSNR.append(timeCounter, snr)
        seriesPLR.append(timeCounter, plr)

        if (timeCounter > axisX.max) {
            axisX.min = timeCounter - windowSize
            axisX.max = timeCounter
            if (seriesRSSI.count > windowSize * 1.5) {
                seriesRSSI.remove(0)
                seriesBER.remove(0)
                seriesSNR.remove(0)
                seriesPLR.remove(0)
            }
        }
        timeCounter += 1.0
    }

    ChartView {
        id: telemetryChart
        anchors.fill: parent
        anchors.margins: 10

        backgroundColor: "transparent"
        titleColor: "white"
        title: "LIVE RF TELEMETRY (868 MHz FSK/QAM)"
        titleFont.pointSize: 18
        titleFont.bold: true
        legend.labelColor: "white"
        legend.alignment: Qt.AlignBottom
        antialiasing: true

        ValueAxis { id: axisX; min: 0; max: windowSize; color: "#555555"; labelsColor: "#AAAAAA"; gridLineColor: "#333333"; labelFormat: "%.0f" }
        ValueAxis { id: axisY_Signal; min: -120; max: 60; color: "#555555"; labelsColor: "#00FFCC"; gridLineColor: "#333333" }
        ValueAxis { id: axisY_Percent; min: 0; max: 100; color: "#555555"; labelsColor: "#FF3333"; gridLineColor: "transparent" }

        LineSeries { id: seriesRSSI; name: "RSSI (dBm)"; axisX: axisX; axisY: axisY_Signal; color: "#00FFCC"; width: 3 }
        LineSeries { id: seriesSNR; name: "SNR (dB)"; axisX: axisX; axisY: axisY_Signal; color: "#FFFF00"; width: 3 }
        LineSeries { id: seriesBER; name: "BER (%)"; axisX: axisX; axisYRight: axisY_Percent; color: "#FF3333"; width: 3 }
        LineSeries { id: seriesPLR; name: "PLR (%)"; axisX: axisX; axisYRight: axisY_Percent; color: "#FF00FF"; width: 3 }
    }
}