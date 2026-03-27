# Exhaustive C++ Code Dissection

This document explains the "engine" of your application.

---

## 1. serialhandler.h (The Interface)

| Line | Code | Detailed Explanation |
|:---|:---|:---|
| 16 | `QML_NAMED_ELEMENT` | Registers this class as a QML type so it can be used like a tag in `Main.qml`. |
| 17 | `Q_PROPERTY(availablePorts)` | Exposes the list of USB ports to QML so the `ComboBox` can see them. |
| 18 | `Q_PROPERTY(isLogging)` | Exposes the "Recording" state so the UI button can change color. |
| 24-27 | `Q_INVOKABLE ...` | Marks these C++ functions as "callable" from the Javascript code in QML. |
| 34-38 | `signals: ...` | The "Broadcasters." These signals carry data from the hardware thread to the UI thread. |

---

## 2. serialhandler.cpp (The Implementation)

| Line | Code | Detailed Explanation |
|:---|:---|:---|
| 19-25 | `updateAvailablePorts()` | Uses `QSerialPortInfo` to scan the computer's USB bus and find all active COM/TTY ports. |
| 33-47 | `connectToRadio()` | Opens the port with standard STM32 settings: **115200 Baud, 8-N-1**. |
| 56-69 | `toggleLogging()` | Handles file I/O. If enabled, it creates a new CSV file named with the current date/time (e.g., `telemetry_log_20260327.csv`). |
| 81 | `readData()` | **The Heartbeat.** This function triggers whenever new bytes arrive from the STM32. |
| 82-88 | `m_serialBuffer` logic | Accumulates raw bytes. It only processes data once it finds a `\n` (newline), ensuring we never parse a "half-cut" message. |
| 104-123 | `Parsing Engine` | This is a robust string parser. It splits the incoming line by commas (`,`) and then splits each part by colons (`:`). |
| 113-120 | `Sequence Tracking` | **Packet Loss Logic.** It stores the last received ID. If the new ID is e.g. 10 and the last was 8, it knows 1 packet was lost. It then calculates the percentage. |
| 126-127 | `emit ...` | Blasts the final numeric data out to the QML charts. |
| 131-135 | `logData()` | If logging is on, it prepends a high-precision millisecond timestamp to the raw data and writes it to the CSV. |

---

## 3. Communication Protocol
The STM32 should send data in the following format for this software to work perfectly:
`RSSI:-85,BER:0.01,SNR:14.2,SEQ:550,I:0.707,Q:0.707\n`

*   `RSSI`: Received Signal Strength (Integer)
*   `BER`: Bit Error Rate (Float 0.0 - 100.0)
*   `SNR`: Signal to Noise Ratio (Float)
*   `SEQ`: Sequence Number (Integer)
*   `I/Q`: In-phase/Quadrature components (Float -1.0 to 1.0)
