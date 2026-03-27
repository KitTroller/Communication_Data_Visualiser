# QML Code Documentation: Communications Visualiser

This document provides a line-by-line explanation of the QML architecture used in the project.

## 1. Main.qml (The Application Shell)
This file acts as the root of your application, managing the window, backend connection, and navigation.

| Line(s) | Code | Explanation |
|:---|:---|:---|
| 1-4 | `import ...` | Pulls in the standard Qt libraries for UI (QtQuick), UI Controls (Buttons/TextFields), Layouts, and your custom C++ module. |
| 6-12 | `Window { ... }` | Defines the main application window (1200x800) with a custom dark theme color. |
| 15-21 | `Image { ... }` | The background layer. `anchors.fill: parent` ensures it stretches with the window. The `Rectangle` inside adds a 20% black tint to ensure text remains readable over the image. |
| 24-34 | `SerialHandler { ... }` | **The Bridge:** This instantiates your C++ class. `onConnectionStatusChanged` is a signal handler that updates the UI text and button label based on whether the serial port is open. |
| 35-37 | `onRadioDataReceived: ...` | This is where data enters the UI. When the C++ side emits a signal with RSSI and BER values, it calls `dashboardPage.updateTelemetry()`. |
| 40-75 | `Rectangle { id: topBar ... }` | The header containing the connection controls. It uses a `RowLayout` to keep the port input and connect button neatly aligned. |
| 65 | `onClicked: ...` | Logic for the Connect button. It toggles between calling `connectToRadio()` and `disconnectRadio()` based on the current button text. |
| 79-88 | `TabBar { ... }` | Navigation system. It defines two buttons: "RF TELEMETRY" and "QAM CONSTELLATION". |
| 91-99 | `SwipeView { ... }` | The page container. `currentIndex: navBar.currentIndex` binds the view to the tab bar. Swiping between pages automatically updates the tabs and vice versa. |

## 2. DashboardPage.qml (Telemetry Charts)
This page handles the real-time plotting of data using `QtCharts`.

| Line(s) | Code | Explanation |
|:---|:---|:---|
| 4 | `Item { id: root }` | The base container for the page. |
| 7-8 | `property real ...` | Custom variables to track time (X-axis) and the visible window width (100 units). |
| 11-22 | `function updateTelemetry(...)` | **The Logic Engine:** Adds new points to the series. If `timeCounter` exceeds the window, it shifts the X-axis (`axisX.min` / `axisX.max`) to create a "scrolling" effect and removes old data to save memory. |
| 24-44 | `ChartView { ... }` | The chart container. It is set to `transparent` so the `Main.qml` background shows through. |
| 46-48 | `ValueAxis { ... }` | Defines the X and Y axes. Note the use of two Y-axes (`axisY_RSSI` for dBm and `axisY_BER` for percentage). |
| 50-51 | `LineSeries { ... }` | The actual data lines. `axisYRight` on the BER series allows it to use the secondary vertical scale on the right side of the chart. |

## 3. ConstellationPage.qml (Visualizer Placeholder)
Currently a stylized placeholder for your QAM/PSK visualization.

| Line(s) | Code | Explanation |
|:---|:---|:---|
| 5-15 | `Label { ... }` | Displays the status text. |
| 16 | `layer.enabled: true` | Enables high-quality rendering effects (like shadows or glows) for the text component. |
