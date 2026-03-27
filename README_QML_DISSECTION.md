# Exhaustive QML Code Dissection

This document provides a line-by-line breakdown of the entire QML UI layer.

---

## 1. Main.qml (The Application Orchestrator)
This file is the root of the UI. It manages the window, the C++ backend instance, and the overall layout.

| Line | Code | Detailed Explanation |
|:---|:---|:---|
| 1-3 | `import QtQuick`, `Layouts` | Loads the core UI and layout engines (RowLayout, ColumnLayout). |
| 4 | `import Comms...` | Imports your C++ `SerialHandler` class which was registered as a QML type. |
| 6-11 | `Window { ... }` | Creates the OS window. `color: "#050505"` sets the "Cyber" dark background. |
| 14-20 | `Image { ... }` | Layers the `background.png`. The internal `Rectangle` acts as a dark veil so white text is always readable. |
| 23-25 | `SerialHandler { id: radioBackend }` | **The Core Instance.** This is the bridge to your hardware. `id` allows other components to call its functions. |
| 26-30 | `onConnectionStatusChanged` | A signal handler. When C++ emits this signal, it updates the `statusText` and toggles the "Connect/Disconnect" button label. |
| 31-33 | `onRadioDataReceived` | Maps C++ telemetry (RSSI, BER, SNR, PLR) directly into the `DashboardPage` function. |
| 34-36 | `onConstellationDataReceived` | Maps C++ I/Q values into the `ConstellationPage`. |
| 40-44 | `Rectangle { id: topBar }` | The header container. `color: "#AA111111"` creates a semi-transparent dark bar. |
| 46 | `RowLayout { ... }` | Ensures all header controls (Label, ComboBox, Buttons) stay in a single horizontal line. |
| 52-62 | `ComboBox { id: portSelector }` | **New Feature.** `model: radioBackend.availablePorts` binds the dropdown list to the C++ string list of detected serial ports. |
| 64-70 | `Button { text: "Refresh" }` | Calls `radioBackend.refreshPorts()`, triggering a rescan of USB devices. |
| 72-78 | `Button { id: connectButton }` | Logic: If text is "Connect", it calls `connectToRadio()`. Otherwise, `disconnectRadio()`. |
| 80-86 | `Button { id: recordButton }` | **New Feature.** Toggles the CSV logging state in C++. The background color changes to red (`#FF3333`) when recording is active. |
| 95-104 | `TabBar { ... }` | Controls which page is visible in the `SwipeView`. |
| 107-113 | `SwipeView { ... }` | A container for multiple pages. Swiping left/right changes the view. `currentIndex` is synced with the `TabBar`. |

---

## 2. DashboardPage.qml (Real-time Telemetry)
This page visualizes signal quality over time.

| Line | Code | Detailed Explanation |
|:---|:---|:---|
| 7-8 | `property real timeCounter` | A simple counter that acts as the X-axis (seconds). |
| 11 | `function updateTelemetry(...)` | The entry point for data. It appends new points to all four line series simultaneously. |
| 17-21 | `if (timeCounter > axisX.max)` | **The Scrolling Logic.** Once data hits the right edge, it shifts the `min` and `max` of the X-axis left, creating a "live feed" effect. |
| 22-26 | `series.remove(0)` | Memory management: Removes the oldest point from the chart to prevent the app from slowing down over long sessions. |
| 31-42 | `ChartView { ... }` | The rendering engine. `antialiasing: true` ensures the lines look smooth, not pixelated. |
| 44 | `ValueAxis { id: axisX }` | The horizontal time axis. |
| 45 | `ValueAxis { id: axisY_Signal }` | Primary vertical axis for dB (RSSI/SNR). Range: -120 to +40. |
| 46 | `ValueAxis { id: axisY_Percent }` | Secondary vertical axis for percentages (BER/PLR). Range: 0 to 100. |
| 48-51 | `LineSeries { ... }` | The four data lines. Note that BER and PLR use `axisYRight`, which places their scale on the right side of the chart. |

---

## 3. ConstellationPage.qml (I/Q Visualization)
This page uses a scatter plot to show the complex signal space.

| Line | Code | Detailed Explanation |
|:---|:---|:---|
| 6 | `function updateConstellation(i, q)` | Adds a dot at coordinates (I, Q). |
| 8-10 | `if (count > 500) remove(0)` | Keeps the display clean by only showing the most recent 500 symbols. |
| 14-22 | `ChartView { ... }` | Formats the constellation grid. |
| 24-42 | `ValueAxis { id: axisI / axisQ }` | Defines a square grid from -1.5 to +1.5. This is where QAM/PSK patterns appear. |
| 44-50 | `ScatterSeries { ... }` | **The Visualizer.** Instead of lines, it draws individual points (`markerSize: 8`). |
| 53-72 | `LineSeries { ... }` | Static lines that draw the X and Y "crosshair" at 0.0 to help the user identify the quadrants. |
