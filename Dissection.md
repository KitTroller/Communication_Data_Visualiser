# Dissection: A Deep Dive into the C++ DSP Backend

## Abstract
This document dissects the core C++ engines of the Ground Station Visualizer: `DspEngine` and `SerialHandler`. It provides an exhaustive analysis of how the application simulates complex RF phenomena using the `liquid-dsp` library, and how it safely ingests asynchronous serial data. This is a technical post-mortem aimed at senior engineers and researchers.

---

## 1. The `DspEngine`: A Stochastic Simulation Environment

The `DspEngine` class is a high-frequency simulation loop designed to mimic the behavior of a physical modem transmitting over a noisy channel.

### 1.1 `liquid-dsp` Integration and the `modemcf` Object
`liquid-dsp` is a highly optimized, open-source digital signal processing library for software-defined radios. The class relies heavily on the `modemcf` (Modem, Complex Float) object family.

The lifecycle of the modem is managed via `rebuildModem()`. This function is pivotal; whenever the user changes the modulation scheme (e.g., from QPSK to 64-QAM), the existing modem instance must be destroyed and reallocated to load the correct constellation map and decision boundaries.

```cpp
void DspEngine::rebuildModem()
{
    if (m_modem) modemcf_destroy(m_modem);
    m_modem = nullptr;
    // ... logic to determine scheme (e.g., LIQUID_MODEM_QAM16)
    if (m_modType != 4) { // 4 is a custom FSK simulation
        m_modem = modemcf_create(scheme);
    }
}
```

### 1.2 Mathematical Formulation of Noise and Demodulation
The `processDspMath()` function, triggered every 15ms by a `QTimer`, represents the main physics loop.

**Step 1: Symbol Generation and Modulation**
```cpp
unsigned int sym_in = arc4random_uniform(m_mSize);
std::complex<float> iq_tx;
modemcf_modulate(m_modem, sym_in, reinterpret_cast<liquid_float_complex*>(&iq_tx));
```
A random symbol (`sym_in`) is generated bounded by the alphabet size ($M$). The `modemcf_modulate` function maps this integer to an ideal point in the complex plane (`iq_tx`). The codebase uses a clever `reinterpret_cast` to bridge C++'s `std::complex` with `liquid-dsp`'s C-struct `liquid_float_complex`.

**Step 2: Channel Impairment (AWGN)**
```cpp
float noise_power = powf(10.0f, -m_snr / 20.0f);
noisy_i = iq_tx.real() + (noise_power * (((float)arc4random() / 4294967295.0f) - 0.5f));
```
The ideal point is corrupted. As discussed in the Masterclass, the noise variance is a function of the SNR.

**Step 3: Demodulation and Error Vector Magnitude (EVM) Proxy**
```cpp
unsigned int sym_out;
liquid_float_complex iq_rx = {noisy_i, noisy_q};
modemcf_demodulate(m_modem, iq_rx, &sym_out);
bit_errors = countSetBits(sym_in ^ sym_out);
```
The corrupted point (`iq_rx`) is fed back into the demodulator. The demodulator uses minimum distance detection (Euclidean distance in the complex plane) to guess the original symbol. By XORing the input symbol and the output symbol (`sym_in ^ sym_out`), we obtain a bitmask of the errors. The custom `countSetBits` function (Brian Kernighan's algorithm or similar bit-shifting) tallies the exact number of bit errors, providing ground truth for the BER calculation.

### 1.3 Packet Simulation and Jitter Calculus
To make the UI feel "alive", the engine simulates network telemetry and packets.
The `m_packetTimer` simulates a payload containing the string `"ARISTURTLE LINK: TELEMETRY NOMINAL"`. Based on the instantaneous BER calculated from the constellation simulation, a probabilistic model corrupts ASCII characters during the payload construction:

```cpp
if (((float)arc4random() / 4294967295.0f) < (current_ber * 5.0f)) {
    int asciiShift = (arc4random() % 15) + 1;
    c = QChar(c.unicode() + (arc4random() % 2 == 0 ? asciiShift : -asciiShift));
    hasError = true;
}
```
*Note: The `current_ber * 5.0f` multiplier is an intentional pedagogical exaggeration. In reality, a BER of $10^{-3}$ would rarely corrupt a short string, but for visual feedback, the error rate is artificially inflated.*

---

## 2. The `SerialHandler`: Deterministic Asynchrony

While `DspEngine` invents data, `SerialHandler` consumes reality. It is an asynchronous wrapper around `QSerialPort`.

### 2.1 The Parsing Automaton
Serial ports are stream-based, not message-based. A single `readyRead()` signal might deliver half a line, a full line, or multiple lines. The `readData()` slot implements a robust buffering automaton:

```cpp
m_serialBuffer.append(m_serialPort->readAll());
while (m_serialBuffer.contains('\n')) {
    int newlineIndex = m_serialBuffer.indexOf('\n');
    QByteArray rawLine = m_serialBuffer.left(newlineIndex);
    m_serialBuffer.remove(0, newlineIndex + 1);
    // ... process cleanLine
}
```
This loop ensures that only complete, newline-terminated strings are passed to the parser, leaving partial messages safely in the buffer for the next interrupt.

### 2.2 Sequence Tracking and Packet Loss Ratio (PLR)
The custom comma-separated protocol includes a `SEQ` (sequence) number. By tracking the delta between the expected sequence number and the received sequence number, the software deterministically calculates packet loss:

```cpp
if (m_lastSequence != -1 && seq != (m_lastSequence + 1)) {
    m_packetsLost += (seq > m_lastSequence) ? (seq - m_lastSequence - 1) : 0;
}
```
This is a standard network engineering technique to monitor link quality independent of bit-level errors, accounting for entire frames dropped due to synchronization failure or CRC rejections at the hardware level.

## Conclusion
The C++ backend is a study in contrasting computing paradigms. The `DspEngine` is a CPU-bound, continuous mathematical loop relying on highly optimized C-libraries, while the `SerialHandler` is an I/O-bound, event-driven state machine managing string buffers and hardware interrupts. Together, they form a robust foundation capable of both advanced simulation and physical hardware integration.