#include "dspengine.h"
#include <cmath>
#include <complex>

DspEngine::DspEngine(QObject *parent) : QObject(parent), m_timer(new QTimer(this))
{
    rebuildModem();
    connect(m_timer, &QTimer::timeout, this, &DspEngine::processDspMath);
}

DspEngine::~DspEngine() { if (m_modem) modemcf_destroy(m_modem); }

void DspEngine::rebuildModem()
{
    if (m_modem) modemcf_destroy(m_modem);
    m_modem = nullptr; // Safety clear
    m_errorBits = 0; m_totalBits = 0; // Reset errors on modem change

    modulation_scheme scheme = LIQUID_MODEM_QAM16;
    if (m_modType == 0) { scheme = LIQUID_MODEM_QPSK;   m_mSize = 4; m_bitsPerSymbol = 2; m_modName = "QPSK"; }
    if (m_modType == 1) { scheme = LIQUID_MODEM_QAM16;  m_mSize = 16; m_bitsPerSymbol = 4; m_modName = "16-QAM"; }
    if (m_modType == 2) { scheme = LIQUID_MODEM_QAM64;  m_mSize = 64; m_bitsPerSymbol = 6; m_modName = "64-QAM"; }
    if (m_modType == 3) { scheme = LIQUID_MODEM_QAM256; m_mSize = 256; m_bitsPerSymbol = 8; m_modName = "256-QAM"; }
    if (m_modType == 4) { m_bitsPerSymbol = 1; m_modName = "FSK"; }

    // --- THE CRITICAL MISSING LINE ---
    // Rebuild the DSP math engine unless we are simulating basic FSK
    if (m_modType != 4) {
        m_modem = modemcf_create(scheme);
    }
}

void DspEngine::setSnr(float snr) { if (m_snr != snr) { m_snr = snr; emit snrChanged(); } }
void DspEngine::setModType(int type) { if (m_modType != type) { m_modType = type; rebuildModem(); emit modTypeChanged(); } }
void DspEngine::startSimulation() { m_timer->start(15); } // Run at ~60fps
void DspEngine::stopSimulation() { m_timer->stop(); }

// Helper function to count exact bit errors
unsigned int DspEngine::countSetBits(unsigned int n) {
    unsigned int count = 0;
    while (n) { count += n & 1; n >>= 1; }
    return count;
}

void DspEngine::processDspMath()
{
    float perfect_i = 0.0f, perfect_q = 0.0f;
    float noisy_i = 0.0f, noisy_q = 0.0f;
    unsigned int bit_errors = 0;
    float noise_power = powf(10.0f, -m_snr / 20.0f);

    if (m_modType == 4) {
        float random_angle = ((float)arc4random() / 4294967295.0f) * 2.0f * M_PI;
        perfect_i = cosf(random_angle) * 0.707f; perfect_q = sinf(random_angle) * 0.707f;
        noisy_i = perfect_i + (noise_power * (((float)arc4random() / 4294967295.0f) - 0.5f));
        noisy_q = perfect_q + (noise_power * (((float)arc4random() / 4294967295.0f) - 0.5f));
        if (m_snr < 15.0f && (arc4random() % 100 < (15.0f - m_snr)*5)) bit_errors = 1;
        m_totalBits += 1;
    }
    else if (m_modem) {
        unsigned int sym_in = arc4random_uniform(m_mSize);
        std::complex<float> iq_tx;
        modemcf_modulate(m_modem, sym_in, reinterpret_cast<liquid_float_complex*>(&iq_tx));
        noisy_i = iq_tx.real() + (noise_power * (((float)arc4random() / 4294967295.0f) - 0.5f));
        noisy_q = iq_tx.imag() + (noise_power * (((float)arc4random() / 4294967295.0f) - 0.5f));

        unsigned int sym_out;
        liquid_float_complex iq_rx = {noisy_i, noisy_q};
        modemcf_demodulate(m_modem, iq_rx, &sym_out);
        bit_errors = countSetBits(sym_in ^ sym_out);
        m_totalBits += m_bitsPerSymbol;
    } else { return; }

    m_errorBits += bit_errors;
    emit newConstellationData(noisy_i, noisy_q, bit_errors > 0);

    // --- DECELERATE THE TELEMETRY ENGINE ---
    m_frameCounter++;
    if (m_frameCounter >= 6) {
        m_frameCounter = 0;
        // Jitter math to make the lines look alive
        float simulated_rssi = -100.0f + m_snr + (((float)arc4random() / 4294967295.0f) * 1.5f - 0.75f);
        float simulated_snr = m_snr + (((float)arc4random() / 4294967295.0f) * 0.8f - 0.4f);
        float calculated_ber = m_totalBits > 0 ? ((float)m_errorBits / m_totalBits) * 100.0f : 0.0f;
        float simulated_plr = calculated_ber > 5.0f ? calculated_ber * 1.5f : 0.0f;

        emit simulatedTelemetry(simulated_rssi, calculated_ber, simulated_snr, simulated_plr);
        if (m_totalBits > 5000) { m_totalBits = 0; m_errorBits = 0; }
    }

    // --- NEW FULL-PACKET LOGGER (Runs every ~1 second) ---
    m_packetTimer++;
    if (m_packetTimer >= 60) {
        m_packetTimer = 0;
        QString txStr = "ARISTURTLE LINK: TELEMETRY NOMINAL";
        QString rxStr = "";
        bool hasError = false;

        // PHYSICS: 34 characters * 8 bits = 272 bits total.
        // Time taken = Total Bits / Bits Per Symbol.
        int timeMs = (272 / m_bitsPerSymbol) * 2;

        float current_ber = m_totalBits > 0 ? ((float)m_errorBits / m_totalBits) : 0.0f;

        for (int i = 0; i < txStr.length(); i++) {
            QChar c = txStr[i];
            // Multiply BER probability so visual errors are obvious on screen
            if (((float)arc4random() / 4294967295.0f) < (current_ber * 5.0f)) {
                int asciiShift = (arc4random() % 15) + 1;
                c = QChar(c.unicode() + (arc4random() % 2 == 0 ? asciiShift : -asciiShift));
                hasError = true;
            }
            rxStr += c;
        }
        emit packetReceived(m_modName, m_bitsPerSymbol, timeMs, txStr, rxStr, hasError);
    }
}