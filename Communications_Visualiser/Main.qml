import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Communications_Visualiser

Window {
    id: root
    width: 1200
    height: 800
    visible: true
    title: "Ground Station Visualizer"
    color: "#050505"

    // --- THE RESPONSIVE TRIGGER ---
    readonly property bool isMobile: root.width < 850
    // Value to turn off phosphor fade
    property bool datasetMode: false

    // 1. Background Image
    Image {
        id: backgroundImage
        anchors.fill: parent
        source: "assets/background.png"
        fillMode: Image.PreserveAspectCrop
        Rectangle {
            anchors.fill: parent
            color: "black"
            opacity: 0.2
        }
    }

    // 2. C++ Backend
    SerialHandler {
        id: radioBackend
        onConnectionStatusChanged: (isConnected, message) => {
            statusText.text = message;
            statusText.color = isConnected ? "#00FFCC" : "#FF3333";
            connectButton.text = isConnected ? "Disconnect" : "Connect";
        }
        onRadioDataReceived: (rssi, ber, snr, plr) => {
            dashboardPage.updateTelemetry(rssi, ber, snr, plr);
        }
        onConstellationDataReceived: (i, q) => {
            constellationPage.updateConstellation(i, q);
        }
    }

    DspEngine {
        id: dspBackend
        Component.onCompleted: dspBackend.startSimulation()
        onNewConstellationData: (i, q, isError, isPeak) => {
            if (navBar.currentIndex === 1)
                constellationPage.updateConstellation(i, q, isError, isPeak);
        }
        onSimulatedTelemetry: (rssi, ber, snr, plr) => {
            if (navBar.currentIndex === 0 && !statusText.text.startsWith("Connected")) {
                dashboardPage.updateTelemetry(rssi, ber, snr, plr);
            }
        }
    }

    // --- GLOBAL PACKET LOG ---
    ListModel {
        id: globalPacketLogModel
    }
    Connections {
        target: dspBackend
        function onPacketReceived(modName, speed, timeMs, tx, rx, hasError) {
            globalPacketLogModel.insert(0, {
                "modName": modName,
                "speed": speed,
                "timeMs": timeMs,
                "txStr": tx,
                "rxStr": rx,
                "isError": hasError
            });
            if (globalPacketLogModel.count > 10)
                globalPacketLogModel.remove(10);
        }
    }

    // 3. Top Control Bar (Horizontally Scrollable)
    Rectangle {
        id: topBar
        anchors.top: parent.top
        width: parent.width
        height: 60
        color: "#AA111111"

        Flickable {
            anchors.fill: parent
            contentWidth: topRow.implicitWidth + 40
            contentHeight: height
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            RowLayout {
                id: topRow
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.leftMargin: 10
                spacing: 15

                Label {
                    text: "TARGET PORT:"
                    color: "#00FFCC"
                    font.pixelSize: 14
                    font.bold: true
                }

                ComboBox {
                    id: portSelector
                    Layout.preferredWidth: 200
                    model: radioBackend.availablePorts
                    background: Rectangle {
                        color: "#44FFFFFF"
                        radius: 4
                        border.color: "#555555"
                    }
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
                    background: Rectangle {
                        color: parent.pressed ? "#555555" : "#333333"
                        radius: 4
                    }
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: radioBackend.refreshPorts()
                }

                Button {
                    id: connectButton
                    text: "Connect"
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
                    onClicked: text === "Connect" ? radioBackend.connectToRadio(portSelector.currentText) : radioBackend.disconnectRadio()
                }

                Button {
                    id: recordButton
                    text: radioBackend.isLogging ? "STOP RECORDING" : "START LOGGING"
                    background: Rectangle {
                        color: radioBackend.isLogging ? "#FF3333" : "#333333"
                        radius: 4
                        border.color: "#FF3333"
                        border.width: 1
                    }
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: radioBackend.toggleLogging(!radioBackend.isLogging)
                }

                Item {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 20
                }

                Label {
                    id: statusText
                    text: "SYSTEM OFFLINE"
                    color: "#FF3333"
                    font.pixelSize: 14
                    font.bold: true
                    Layout.alignment: Qt.AlignRight
                }

                // DELETED the dead "⚙ CONTROLS" button from here.
            }
        }
    }

    // --- 4. THE MASTER UI COMPONENT (Write once, use dynamically) ---
    Component {
        id: masterControlsComponent

        Rectangle {
            anchors.fill: parent // THE FIX: Forces the component to fit perfectly inside the Loader
            color: "#0A0A0A"
            border.color: "#333333"
            border.width: 1

            ScrollView {
                anchors.fill: parent
                clip: true
                contentWidth: availableWidth

                Column {
                    id: controlCol // THE FIX: ID to reference the true width
                    width: parent.width
                    spacing: 15
                    padding: 15

                    Label {
                        text: "MASTER DSP CONTROLS"
                        color: "#00FFCC"
                        font.bold: true
                        font.pixelSize: 16
                    }

                    // THE FIX: Subtract 30px from all children to account for the 15px left/right padding
                    Rectangle {
                        width: controlCol.width - 30
                        height: 1
                        color: "#333"
                    }

                    ComboBox {
                        width: controlCol.width - 30
                        // The exact 16 modulations required by the project specifications
                        model: ["4-ASK", "8-ASK", "BPSK", "QPSK", "4-HQAM", "16-HQAM", "64-HQAM", "16-QAM", "32-QAM", "64-QAM", "128-QAM", "256-QAM", "16-APSK", "32-APSK", "64-APSK", "128-APSK"]
                        currentIndex: dspBackend.modType
                        onActivated: dspBackend.modType = currentIndex
                        background: Rectangle {
                            color: "#222"
                            radius: 4
                            border.color: "#444"
                        }
                        contentItem: Text {
                            text: parent.displayText
                            color: "white"
                            leftPadding: 10
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    Label {
                        text: "Signal-to-Noise Ratio (SNR): " + dspBackend.snr.toFixed(1) + " dB"
                        color: "white"
                        font.bold: true
                    }
                    Slider {
                        width: controlCol.width - 30
                        from: 0.0
                        to: 50.0
                        value: dspBackend.snr
                        onValueChanged: dspBackend.snr = value
                    }

                    Label {
                        text: "Decision Grid Opacity: " + dspBackend.gridOpacity.toFixed(2)
                        color: "white"
                        font.bold: true
                        font.pixelSize: 12
                    }
                    Slider {
                        width: controlCol.width - 30
                        from: 0.0
                        to: 1.0
                        value: dspBackend.gridOpacity
                        onValueChanged: dspBackend.gridOpacity = value
                    }
                    // Helper array to translate the 0-3 integers into UI text
                    property var severityLabels: ["None", "Low", "Medium", "High"]

                    // --- 1. PHASE NOISE ---
                    Label {
                        text: "Phase Noise: " + parent.severityLabels[dspBackend.phaseNoiseLevel]
                        color: "white"
                        font.bold: true
                        font.pixelSize: 12
                    }
                    Slider {
                        width: controlCol.width - 30
                        from: 0
                        to: 3
                        stepSize: 1
                        snapMode: Slider.SnapAlways
                        value: dspBackend.phaseNoiseLevel
                        onValueChanged: dspBackend.phaseNoiseLevel = value
                    }

                    // --- 2. I/Q IMBALANCE ---
                    Label {
                        text: "I/Q Imbalance: " + parent.severityLabels[dspBackend.iqImbalanceLevel]
                        color: "white"
                        font.bold: true
                        font.pixelSize: 12
                    }
                    Slider {
                        width: controlCol.width - 30
                        from: 0
                        to: 3
                        stepSize: 1
                        snapMode: Slider.SnapAlways
                        value: dspBackend.iqImbalanceLevel
                        onValueChanged: dspBackend.iqImbalanceLevel = value
                    }

                    // --- 3. EXTERNAL INTERFERENCE ---
                    Label {
                        text: "Jamming Interference: " + parent.severityLabels[dspBackend.interferenceLevel]
                        color: "white"
                        font.bold: true
                        font.pixelSize: 12
                    }
                    Slider {
                        width: controlCol.width - 30
                        from: 0
                        to: 3
                        stepSize: 1
                        snapMode: Slider.SnapAlways
                        value: dspBackend.interferenceLevel
                        onValueChanged: dspBackend.interferenceLevel = value
                    }

                    RowLayout {
                        width: controlCol.width - 30
                        Label {
                            text: "Enable RRC Pulse Shaping"
                            color: "white"
                            font.bold: true
                            Layout.fillWidth: true
                        }
                        Switch {
                            checked: dspBackend.useRrc
                            onCheckedChanged: dspBackend.useRrc = checked
                        }
                    }

                    RowLayout {
                        width: controlCol.width - 30
                        Label {
                            text: "Infinite Persistence (Dataset Mode)"
                            color: "white"
                            font.bold: true
                            Layout.fillWidth: true
                        }
                        Switch {
                            checked: root.datasetMode
                            onCheckedChanged: root.datasetMode = checked
                        }
                    }
                    // --- THE NEW CLEAR BUTTON ---
                    Button {
                        text: "CLEAR CANVAS"
                        width: controlCol.width - 30
                        background: Rectangle {
                            color: "#331111"
                            radius: 4
                            border.color: "#FF3333"
                            border.width: 1
                        }
                        contentItem: Text {
                            text: parent.text
                            color: "white"
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                        }
                        onClicked: constellationPage.clearCanvas()
                    }
                    Button {
                        text: "GENERATE ML DATASET"
                        width: controlCol.width - 30
                        background: Rectangle {
                            color: "#113311"
                            radius: 4
                            border.color: "#00FFCC"
                            border.width: 1
                        }
                        contentItem: Text {
                            text: parent.text
                            color: "#00FFCC"
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                        }
                        onClicked: {
                            // Prepare the environment for automation
                            root.datasetMode = true;
                            dspBackend.timerInterval = 15;
                            // THE SNIPER FIX: Purge any existing red dots/shadows from the human view
                            constellationPage.clearCanvas();
                            dspBackend.startDatasetGeneration();
                        }
                    }

                    Button {
                        text: "EXPORT SEP CURVES"
                        width: controlCol.width - 30
                        background: Rectangle {
                            color: "#111133"
                            radius: 4
                            border.color: "#00CCFF"
                            border.width: 1
                        }
                        contentItem: Text {
                            text: parent.text
                            color: "#00CCFF"
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                        }
                        onClicked: dspBackend.generateSEPCurves()
                    }

                    Label {
                        text: "RRC Roll-off Factor (β): " + dspBackend.rollOff.toFixed(2)
                        color: "white"
                        font.bold: true
                        font.pixelSize: 12
                    }
                    Slider {
                        width: controlCol.width - 30
                        from: 0.1
                        to: 1.0
                        value: dspBackend.rollOff
                        onValueChanged: dspBackend.rollOff = value
                    }

                    Label {
                        text: "Simulation Speed Delay: " + dspBackend.timerInterval + " ms"
                        color: "white"
                        font.bold: true
                        font.pixelSize: 12
                    }
                    Slider {
                        width: controlCol.width - 30
                        from: 15
                        to: 250
                        value: dspBackend.timerInterval
                        onValueChanged: dspBackend.timerInterval = Math.round(value)
                    }

                    Item {
                        width: 1
                        height: 5
                    }

                    Label {
                        text: "LIVE PACKET LOG"
                        color: "#00FFCC"
                        font.bold: true
                        font.pixelSize: 16
                    }
                    Rectangle {
                        width: controlCol.width - 30
                        height: 1
                        color: "#333"
                    }

                    Repeater {
                        model: globalPacketLogModel
                        delegate: Rectangle {
                            width: controlCol.width - 30
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
                                    Text {
                                        text: "[" + modName + "]"
                                        color: "#888"
                                        font.pixelSize: 11
                                        font.bold: true
                                    }
                                    Text {
                                        text: speed + " bits/sym"
                                        color: "#666"
                                        font.pixelSize: 11
                                    }
                                    Text {
                                        text: "⏱ " + timeMs + "ms"
                                        color: "#00FFCC"
                                        font.pixelSize: 11
                                    }
                                }
                                Text {
                                    text: "TX: " + txStr
                                    color: "#555"
                                    font.family: "Courier New"
                                    font.pixelSize: 11
                                }
                                Text {
                                    text: "RX: " + rxStr
                                    color: isError ? "#FF3333" : "#00FFCC"
                                    font.family: "Courier New"
                                    font.pixelSize: 11
                                    font.bold: true
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // --- 5. THE RESPONSIVE LAYOUT ENGINE ---
    GridLayout {
        anchors.top: topBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        columns: isMobile ? 1 : 2
        rows: isMobile ? 2 : 1
        columnSpacing: 0
        rowSpacing: 0

        // LEFT/TOP: Visualizer
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            TabBar {
                id: navBar
                Layout.fillWidth: true
                background: Rectangle {
                    color: "#1A1A1A"
                }

                CyberTab {
                    text: "RF TELEMETRY"
                }
                CyberTab {
                    text: "SIGNAL CONSTELLATION"
                }
            }

            SwipeView {
                id: viewPager
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: navBar.currentIndex
                DashboardPage {
                    id: dashboardPage
                }
                ConstellationPage {
                    id: constellationPage
                }
            }
        }

        // RIGHT/BOTTOM: Unified Control Panel
        Rectangle {
            id: controlContainer
            Layout.preferredWidth: isMobile ? parent.width : 400
            Layout.preferredHeight: isMobile ? (controlToggle.checked ? parent.height * 0.45 : 0) : parent.height
            Layout.fillHeight: !isMobile
            clip: true
            color: "#0A0A0A"
            border.color: "#333333"
            border.width: 1

            Behavior on Layout.preferredHeight {
                NumberAnimation {
                    duration: 250
                    easing.type: Easing.InOutQuad
                }
            }

            Loader {
                anchors.fill: parent
                sourceComponent: masterControlsComponent
            }
        }
    }

    component CyberTab: TabButton {
        id: control
        background: Rectangle {
            color: control.checked ? "#33FFFFFF" : "transparent"
            Rectangle {
                width: parent.width
                height: 3
                anchors.bottom: parent.bottom
                color: control.checked ? "#00FFCC" : "transparent"
            }
        }
        contentItem: Text {
            text: control.text
            color: control.checked ? "#00FFCC" : "#888888"
            font.bold: true
            font.pixelSize: 14
            horizontalAlignment: Text.AlignHCenter
        }
    }

    // --- 6. MOBILE TOGGLE BUTTON ---
    Button {
        id: controlToggle
        text: checked ? "↓ HIDE CONTROLS" : "↑ SHOW CONTROLS"
        visible: isMobile
        checkable: true
        checked: false
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 10
        z: 100

        background: Rectangle {
            color: "#CC00FFCC"
            radius: 20
            border.color: "white"
            border.width: 1
        }
        contentItem: Text {
            text: parent.text
            color: "black"
            font.bold: true
            padding: 10
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
