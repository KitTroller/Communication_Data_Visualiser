import QtQuick
import QtQuick.Layouts
import QtCharts

Item {
    id: root

    property real timeCounter: 0.0
    property real windowSize: 100.0

    function updateTelemetry(rssi, ber, snr, plr) {
        seriesRSSI.append(timeCounter, rssi)
        seriesSNR.append(timeCounter, snr)
        seriesBER.append(timeCounter, ber)
        seriesPLR.append(timeCounter, plr)

        if (timeCounter > axisX_Signal.max) {
            // Keep both X-axes perfectly synchronized
            axisX_Signal.min = timeCounter - windowSize
            axisX_Signal.max = timeCounter
            axisX_Error.min = timeCounter - windowSize
            axisX_Error.max = timeCounter

            if (seriesRSSI.count > windowSize * 1.5) {
                seriesRSSI.remove(0)
                seriesSNR.remove(0)
                seriesBER.remove(0)
                seriesPLR.remove(0)
            }
        }
        timeCounter += 1.0
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        // CHART 1: RF Power (RSSI & SNR)
        ChartView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            backgroundColor: "transparent"
            titleColor: "white"
            title: "RF POWER LEVEL"
            titleFont.pointSize: 14
            titleFont.bold: true
            legend.labelColor: "white"
            legend.alignment: Qt.AlignBottom
            antialiasing: true

            ValueAxis { id: axisX_Signal; min: 0; max: windowSize; color: "#555555"; labelsColor: "#AAAAAA"; gridLineColor: "#333333"; labelFormat: "%.0f" }
            ValueAxis { id: axisY_Signal; min: -120; max: 60; color: "#555555"; labelsColor: "#00FFCC"; gridLineColor: "#333333" }

            LineSeries { id: seriesRSSI; name: "RSSI (dBm)"; axisX: axisX_Signal; axisY: axisY_Signal; color: "#00FFCC"; width: 3 }
            LineSeries { id: seriesSNR; name: "SNR (dB)"; axisX: axisX_Signal; axisY: axisY_Signal; color: "#FFFF00"; width: 3 }
        }

        // CHART 2: Link Health (BER & PLR)
        ChartView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            backgroundColor: "transparent"
            titleColor: "white"
            title: "LINK HEALTH (ERRORS)"
            titleFont.pointSize: 14
            titleFont.bold: true
            legend.labelColor: "white"
            legend.alignment: Qt.AlignBottom
            antialiasing: true

            ValueAxis { id: axisX_Error; min: 0; max: windowSize; color: "#555555"; labelsColor: "#AAAAAA"; gridLineColor: "#333333"; labelFormat: "%.0f" }
            // THE FIX: Clamped maximum to 10% so you can actually see the line move
            ValueAxis { id: axisY_Error; min: 0; max: 20; color: "#555555"; labelsColor: "#FF3333"; gridLineColor: "#333333" }

            LineSeries { id: seriesBER; name: "BER (%)"; axisX: axisX_Error; axisY: axisY_Error; color: "#FF3333"; width: 3 }
            LineSeries { id: seriesPLR; name: "PLR (%)"; axisX: axisX_Error; axisY: axisY_Error; color: "#FF00FF"; width: 3 }
        }
    }
}