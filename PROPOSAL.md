# Engineering Proposal: Communications Visualiser Enhancements

## 1. Constellation Diagram (QAM/PSK)
Since your current `ConstellationPage.qml` is a placeholder, this is your highest priority.
- **Data Requirement:** You need your STM32 to send raw `I` and `Q` values (e.g., `I:0.7,Q:-0.3`).
- **Implementation:** Use a `ScatterSeries` in `QtCharts`. Each received symbol is a dot on a 2D plane.
- **Thesis Value:** Visualising "clouding" in the constellation diagram under noise (attenuation or interference) perfectly complements your BER charts.

## 2. Signal-to-Noise Ratio (SNR)
You mentioned "SRRI"—if you meant SNR or RSSI, ensure both are present.
- **Why:** BER is a function of $E_b/N_0$ (Energy per bit to Noise power spectral density).
- **Feature:** Add a third line to your `DashboardPage` chart for SNR. It allows you to demonstrate the correlation: *As SNR drops, BER rises.*

## 3. Data Logging (CSV/JSON)
For a university thesis, you will need hard data for your final report/graphs.
- **Feature:** Add a "Record" button in the UI.
- **Backend:** In `SerialHandler`, open a file and write every received packet with a timestamp. This allows you to generate professional graphs in MATLAB or Python for your paper.

## 4. Hardware Automation: Port Discovery
Instead of manually typing `/dev/cu.usbmodem`, use C++ to find the port.
- **Code:** Use `QSerialPortInfo::availablePorts()` in C++ to populate a list.
- **UI:** Replace the `TextField` with a `ComboBox`. This makes the software feel "retail-ready."

## 5. Packet Loss Rate (PLR)
In addition to Bit Error Rate, track Packet Loss.
- **How:** Add a sequence number to your STM32 packets. If the UI receives packet #10 then #12, it knows packet #11 was lost.
- **Thesis Value:** Distinguishes between "corrupted data" (BER) and "lost data" (PLR/Fading).

## 6. Spectrum View (Advanced)
Since you have an **RTL-SDR v4**, you could potentially pipe a Fast Fourier Transform (FFT) of the 868MHz band into a "Waterfall" display in a third tab. This would make your project a complete "Software Defined Radio Ground Station."
