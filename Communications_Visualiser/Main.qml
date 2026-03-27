import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Communications_Visualiser

Window {
    width: 1200
    height: 800
    visible: true
    title: "Ground Station Visualizer"
    color: "#050505"

    // 1. Background Image (Opacity fixed)
    Image {
        id: backgroundImage
        anchors.fill: parent
        source: "assets/background.png"
        fillMode: Image.PreserveAspectCrop
        Rectangle { anchors.fill: parent; color: "black"; opacity: 0.2 }
    }

    // 2. C++ Backend
    SerialHandler {
        id: radioBackend
        onConnectionStatusChanged: (isConnected, message) => {
                                       statusText.text = message
                                       statusText.color = isConnected ? "#00FFCC" : "#FF3333"
                                       connectButton.text = isConnected ? "Disconnect" : "Connect"
                                   }
        onRadioDataReceived: (rssi, ber, snr, plr) => {
                                 dashboardPage.updateTelemetry(rssi, ber, snr, plr)
                             }
        onConstellationDataReceived: (i, q) => {
                                         constellationPage.updateConstellation(i, q)
                                     }
    }
    DspEngine {
            id: dspBackend
            Component.onCompleted: dspBackend.startSimulation()

            onNewConstellationData: (i, q, isError) => {
                        if (navBar.currentIndex === 1) {
                            constellationPage.updateConstellation(i, q, isError)
                        }
                    }

            onSimulatedTelemetry: (rssi, ber, snr, plr) => {
             // Index 0 is the Telemetry Tab.
             // THE FIX: Allow simulation if we are Offline, Disconnected, or Failed.
            if (navBar.currentIndex === 0 && !statusText.text.startsWith("Connected")) {
                 dashboardPage.updateTelemetry(rssi, ber, snr, plr)
                }
            }
        }
    // 3. Top Control Bar
    Rectangle {
        id: topBar
        anchors.top: parent.top
        width: parent.width
        height: 60
        color: "#AA111111"

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 15

            Label { text: "TARGET PORT:"; color: "#00FFCC"; font.pixelSize: 14; font.bold: true }

            ComboBox {
                id: portSelector
                Layout.preferredWidth: 200
                model: radioBackend.availablePorts
                background: Rectangle { color: "#44FFFFFF"; radius: 4; border.color: "#555555" }
                contentItem: Text {
                    text: parent.displayText
                    color: "white"
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 10
                }
            }
            
            Button {
                id: refreshButton
                text: "Refresh"
                Layout.preferredWidth: 80
                background: Rectangle { color: parent.pressed ? "#555555" : "#333333"; radius: 4 }
                contentItem: Text { text: parent.text; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                onClicked: radioBackend.refreshPorts()
            }

            Button {
                id: connectButton
                text: "Connect"
                background: Rectangle { color: parent.pressed ? "#00AA88" : "#00FFCC"; radius: 4 }
                contentItem: Text { text: parent.text; color: "black"; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                onClicked: text === "Connect" ? radioBackend.connectToRadio(portSelector.currentText) : radioBackend.disconnectRadio()
            }

            Button {
                id: recordButton
                text: radioBackend.isLogging ? "STOP RECORDING" : "START LOGGING"
                background: Rectangle { color: radioBackend.isLogging ? "#FF3333" : "#333333"; radius: 4; border.color: "#FF3333"; border.width: 1 }
                contentItem: Text { text: parent.text; color: "white"; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                onClicked: radioBackend.toggleLogging(!radioBackend.isLogging)
            }

            Label {
                id: statusText; text: "SYSTEM OFFLINE"; color: "#FF3333"; font.pixelSize: 14; font.bold: true
                Layout.fillWidth: true; horizontalAlignment: Text.AlignRight
            }
        }
    }

    // --- NEW GLOBAL LAYOUT ---
    RowLayout {
        anchors.top: topBar.bottom
        anchors.bottom: parent.bottom
        width: parent.width
        spacing: 0

        // LEFT SIDE (The Visualizer Pages)
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            TabBar {
                id: navBar
                Layout.fillWidth: true
                background: Rectangle { color: "#1A1A1A" }

                component CyberTab: TabButton {
                    id: control
                    background: Rectangle {
                        color: control.checked ? "#33FFFFFF" : "transparent"
                        Rectangle { width: parent.width; height: 3; anchors.bottom: parent.bottom; color: control.checked ? "#00FFCC" : "transparent" }
                    }
                    contentItem: Text { text: control.text; color: control.checked ? "#00FFCC" : "#888888"; font.bold: true; font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter }
                }

                CyberTab { text: "RF TELEMETRY" }
                CyberTab { text: "SIGNAL CONSTELLATION" }
            }

            SwipeView {
                id: viewPager
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: navBar.currentIndex
                DashboardPage { id: dashboardPage }
                ConstellationPage { id: constellationPage }
            }
        }

        // RIGHT SIDE (Global Master Sidebar)
        Rectangle {
            Layout.preferredWidth: 400
            Layout.fillHeight: true
            color: "#0A0A0A"
            border.color: "#333333"
            border.width: 1

            ListModel { id: packetLogModel }

            // Connect the C++ Packet Engine to the UI list
            Connections {
                target: dspBackend
                function onPacketReceived(modName, speed, timeMs, tx, rx, hasError) {
                    packetLogModel.insert(0, { "modName": modName, "speed": speed, "timeMs": timeMs, "txStr": tx, "rxStr": rx, "isError": hasError })
                    if (packetLogModel.count > 10) packetLogModel.remove(10) // Keep only last 10
                }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 15
                spacing: 15

                Label { text: "MASTER DSP CONTROLS"; color: "#00FFCC"; font.bold: true; font.pixelSize: 16 }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#333" }

                ComboBox {
                    Layout.fillWidth: true
                    model: ["QPSK (4-PSK)", "16-QAM", "64-QAM", "256-QAM", "CC1101 FSK (Simulated)"]
                    currentIndex: dspBackend.modType
                    onActivated: dspBackend.modType = currentIndex
                    background: Rectangle { color: "#222"; radius: 4; border.color: "#444" }
                    contentItem: Text { text: parent.displayText; color: "white"; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                }

                Label { text: "Signal-to-Noise Ratio (SNR): " + dspBackend.snr.toFixed(1) + " dB"; color: "white"; font.bold: true }
                Slider {
                    Layout.fillWidth: true
                    from: 10.0; to: 50.0; value: dspBackend.snr
                    onValueChanged: dspBackend.snr = value
                }

                Item { height: 10 }
                Label { text: "LIVE PACKET LOG"; color: "#00FFCC"; font.bold: true; font.pixelSize: 16 }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#333" }

                // The Stacked History Log
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: packetLogModel
                    clip: true
                    spacing: 8
                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 55
                        color: "#111"
                        border.color: isError ? "#551111" : "#115511"
                        border.width: 1
                        radius: 4

                        Column {
                            anchors.fill: parent
                            anchors.margins: 5
                            spacing: 3
                            Row {
                                spacing: 15
                                Text { text: "[" + modName + "]"; color: "#888"; font.pixelSize: 11; font.bold: true }
                                Text { text: speed + " bits/sym"; color: "#666"; font.pixelSize: 11 }
                                Text { text: "⏱ " + timeMs + "ms"; color: "#00FFCC"; font.pixelSize: 11 }
                            }
                            Text { text: "TX: " + txStr; color: "#555"; font.family: "monospace"; font.pixelSize: 11 }
                            Text { text: "RX: " + rxStr; color: isError ? "#FF3333" : "#00FFCC"; font.family: "monospace"; font.pixelSize: 11; font.bold: true }
                        }
                    }
                }
            }
        }
    }
}