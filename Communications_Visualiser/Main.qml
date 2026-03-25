import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCharts           // Injects the C++ charting engine
import Communications_Visualiser

Window {
    width: 1200
    height: 800
    visible: true
    title: "Ground Station Visualizer"
    color: "#050505" // Fallback deep black

    // 1. The Background Image (Qt 6 QRC Path)
    Image {
        id: backgroundImage
        anchors.fill: parent
        source: "qrc:/qt/qml/Communications_Visualiser/assets/background.png"
        fillMode: Image.PreserveAspectCrop

        Rectangle {
            anchors.fill: parent
            color: "black"
            opacity: 0.00 // Heavily darkened to make the neon chart lines pop
        }
    }

    // 2. The C++ Backend Bridge
    SerialHandler {
        id: radioBackend

        onConnectionStatusChanged: (isConnected, message) => {
            statusText.text = message
            statusText.color = isConnected ? "#00FFCC" : "#FF3333"
            connectButton.text = isConnected ? "Disconnect" : "Connect"
        }

        onRadioDataReceived: (rssi, ber) => {
            // Push the physical C++ data into our QML Javascript function
            updateCharts(rssi, ber)
        }
    }

    // 3. The Top Control Bar
    Rectangle {
        id: topBar
        anchors.top: parent.top
        width: parent.width
        height: 60
        color: "#AA111111" // Hex with Alpha (AA) for a frosted glass transparency

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 15

            Label {
                text: "TARGET PORT:"
                color: "#00FFCC" // Cyber-cyan
                font.pixelSize: 14
                font.bold: true
            }

            TextField {
                id: portInput
                Layout.preferredWidth: 250
                placeholderText: "/dev/cu.usbmodem"
                color: "white"
                background: Rectangle {
                    color: "#44FFFFFF"
                    radius: 4
                    border.color: "#555555"
                }
            }

            Button {
                id: connectButton
                text: "Connect"
                // Custom professional button styling
                background: Rectangle {
                    color: parent.pressed ? "#00AA88" : "#00FFCC"
                    radius: 4
                }
                contentItem: Text {
                    text: parent.text
                    color: "black"
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                onClicked: {
                    if (text === "Connect") {
                        radioBackend.connectToRadio(portInput.text)
                    } else {
                        radioBackend.disconnectRadio()
                    }
                }
            }

            Label {
                id: statusText
                text: "SYSTEM OFFLINE"
                color: "#FF3333"
                font.pixelSize: 14
                font.bold: true
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
            }
        }
    }

    // 4. The Oscilloscope Engine
    property real timeCounter: 0.0
    property real windowSize: 100.0 // Keep exactly 100 data points on the screen

    ChartView {
        id: telemetryChart
        anchors.top: topBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 20

        // Professional Styling
        backgroundColor: "transparent"
        titleColor: "white"
        title: "LIVE RF TELEMETRY (868 MHz FSK)"
        titleFont.pointSize: 18
        titleFont.bold: true
        legend.labelColor: "white"
        legend.alignment: Qt.AlignBottom
        antialiasing: true // Hardware acceleration for smooth lines

        // The X-Axis (Time)
        ValueAxis {
            id: axisX
            min: 0
            max: windowSize
            color: "#555555"
            labelsColor: "#AAAAAA"
            gridLineColor: "#333333"
            labelFormat: "%.0f"
        }

        // The Left Y-Axis (RSSI)
        ValueAxis {
            id: axisY_RSSI
            min: -120
            max: 0
            color: "#555555"
            labelsColor: "#00FFCC"
            gridLineColor: "#333333"
        }

        // The Right Y-Axis (BER)
        ValueAxis {
            id: axisY_BER
            min: 0
            max: 10
            color: "#555555"
            labelsColor: "#FF3333"
            gridLineColor: "transparent" // Hide secondary grid so it doesn't look messy
        }

        // The Data Lines
        LineSeries {
            id: seriesRSSI
            name: "Signal Strength (dBm)"
            axisX: axisX
            axisY: axisY_RSSI
            color: "#00FFCC"
            width: 3
        }

        LineSeries {
            id: seriesBER
            name: "Bit Error Rate (%)"
            axisX: axisX
            axisYRight: axisY_BER // Maps this line to the right-side scale
            color: "#FF3333"
            width: 3
        }
    }

    // 5. The Dynamic Javascript Logic
    function updateCharts(rssi, ber) {
        // Drop the new data points onto the line
        seriesRSSI.append(timeCounter, rssi)
        seriesBER.append(timeCounter, ber)

        // The Scrolling Logic: If we hit the right wall, shift the camera forward
        if (timeCounter > axisX.max) {
            axisX.min = timeCounter - windowSize
            axisX.max = timeCounter

            // Critical Memory Management: Delete data that falls off the left side of the screen
            seriesRSSI.remove(0)
            seriesBER.remove(0)
        }

        timeCounter += 1.0 // Tick the clock forward
    }
}