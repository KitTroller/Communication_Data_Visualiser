#include "dspengine.h"
#include <cmath>
#include <complex>
#include <QTimer>

// --- WORKER IMPLEMENTATION ---

DspWorker::DspWorker(QObject *parent) : QObject(parent) {
    rebuildModem();
}

DspWorker::~DspWorker() {
    if (m_modem) modemcf_destroy(m_modem);
    if (m_interp) firinterp_crcf_destroy(m_interp);
}

void DspWorker::updateParameters(float snr, int modType, bool useRrc, float rollOff) {
    m_snr = snr;
    m_useRrc = useRrc;
    bool rebuild = false;

    if (m_modType != modType) { m_modType = modType; rebuild = true; }
    if (m_rollOff != rollOff) { m_rollOff = rollOff; rebuild = true; }

    if (rebuild) rebuildModem();
}

void DspWorker::rebuildModem() {
    if (m_modem) modemcf_destroy(m_modem);
    m_modem = nullptr;
    m_errorBits = 0; m_totalBits = 0;

    modulation_scheme scheme = LIQUID_MODEM_QAM16;
    if (m_modType == 0) { scheme = LIQUID_MODEM_QPSK;   m_mSize = 4; m_bitsPerSymbol = 2; m_modName = "QPSK"; }
    else if (m_modType == 1) { scheme = LIQUID_MODEM_QAM16;  m_mSize = 16; m_bitsPerSymbol = 4; m_modName = "16-QAM"; }
    else if (m_modType == 2) { scheme = LIQUID_MODEM_QAM64;  m_mSize = 64; m_bitsPerSymbol = 6; m_modName = "64-QAM"; }
    else if (m_modType == 3) { scheme = LIQUID_MODEM_QAM256; m_mSize = 256; m_bitsPerSymbol = 8; m_modName = "256-QAM"; }
    else if (m_modType == 4) { m_bitsPerSymbol = 1; m_modName = "FSK"; }

    if (m_modType != 4) {
        m_modem = modemcf_create(scheme);
        if (m_interp) firinterp_crcf_destroy(m_interp);
        m_interp = firinterp_crcf_create_prototype(LIQUID_FIRFILT_RRC, 4, 3, m_rollOff, 0.0f);
    }
}

unsigned int DspWorker::countSetBits(unsigned int n) {
    unsigned int count = 0;
    while (n) { count += n & 1; n >>= 1; }
    return count;
}

void DspWorker::process() {
    float noisy_i = 0.0f, noisy_q = 0.0f;
    unsigned int bit_errors = 0;
    float noise_power = powf(10.0f, -m_snr / 20.0f);

    if (m_modType == 4) {
        float random_angle = ((float)arc4random() / 4294967295.0f) * 2.0f * M_PI;
        noisy_i = cosf(random_angle) * 0.707f + (noise_power * (((float)arc4random() / 4294967295.0f) - 0.5f));
        noisy_q = sinf(random_angle) * 0.707f + (noise_power * (((float)arc4random() / 4294967295.0f) - 0.5f));
        if (m_snr < 15.0f && (arc4random() % 100 < (15.0f - m_snr)*5)) bit_errors = 1;
        m_totalBits += 1;
    } else if (m_modem) {
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
        bool is_error = (bit_errors > 0);

        if (m_useRrc && m_interp) {
            liquid_float_complex buffer_tx[4];
            firinterp_crcf_execute(m_interp, {iq_tx.real(), iq_tx.imag()}, buffer_tx);
            for (int i = 0; i < 4; i++) {
                float vis_i = buffer_tx[i].real + (noise_power * (((float)arc4random() / 4294967295.0f) - 0.5f));
                float vis_q = buffer_tx[i].imag + (noise_power * (((float)arc4random() / 4294967295.0f) - 0.5f));
                emit newConstellationData(vis_i, vis_q, is_error, (i == 0));
            }
        }
    }

    m_errorBits += bit_errors;

    if (!m_useRrc || m_modType == 4) {
        emit newConstellationData(noisy_i, noisy_q, bit_errors > 0, true);
    }

    m_frameCounter++;
    if (m_frameCounter >= 6) {
        m_frameCounter = 0;
        float sim_rssi = -100.0f + m_snr + (((float)arc4random() / 4294967295.0f) * 1.5f - 0.75f);
        float sim_snr = m_snr + (((float)arc4random() / 4294967295.0f) * 0.8f - 0.4f);
        float calculated_ber = m_totalBits > 0 ? ((float)m_errorBits / m_totalBits) * 100.0f : 0.0f;
        float sim_plr = calculated_ber > 5.0f ? calculated_ber * 1.5f : 0.0f;
        emit simulatedTelemetry(sim_rssi, calculated_ber, sim_snr, sim_plr);
        if (m_totalBits > 5000) { m_totalBits = 0; m_errorBits = 0; }
    }

    m_packetTimer++;
    if (m_packetTimer >= 60) {
        m_packetTimer = 0;
        QString txStr = "ARISTURTLE LINK: TELEMETRY NOMINAL";
        QString rxStr = "";
        bool hasError = false;
        int timeMs = (272 / m_bitsPerSymbol) * 2;
        float current_ber = m_totalBits > 0 ? ((float)m_errorBits / m_totalBits) : 0.0f;

        for (int i = 0; i < txStr.length(); i++) {
            QChar c = txStr[i];
            if (((float)arc4random() / 4294967295.0f) < (current_ber * 5.0f)) {
                c = QChar(c.unicode() + (arc4random() % 5 + 1));
                hasError = true;
            }
            rxStr += c;
        }
        emit packetReceived(m_modName, m_bitsPerSymbol, timeMs, txStr, rxStr, hasError);
    }
}

// --- ENGINE IMPLEMENTATION ---

DspEngine::DspEngine(QObject *parent) : QObject(parent), m_worker(new DspWorker()), m_timer(new QTimer(this)) {
    m_worker->moveToThread(&m_workerThread);

    connect(&m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_timer, &QTimer::timeout, m_worker, &DspWorker::process);
    
    // Relay signals from worker to engine
    connect(m_worker, &DspWorker::newConstellationData, this, &DspEngine::newConstellationData);
    connect(m_worker, &DspWorker::simulatedTelemetry, this, &DspEngine::simulatedTelemetry);
    connect(m_worker, &DspWorker::packetReceived, this, &DspEngine::packetReceived);

    m_workerThread.start();
}

DspEngine::~DspEngine() {
    m_workerThread.quit();
    m_workerThread.wait();
}

void DspEngine::syncParameters() {
    QMetaObject::invokeMethod(m_worker, "updateParameters",
                              Q_ARG(float, m_snr), Q_ARG(int, m_modType),
                              Q_ARG(bool, m_useRrc), Q_ARG(float, m_rollOff));
}

void DspEngine::setSnr(float snr) { if (m_snr != snr) { m_snr = snr; syncParameters(); emit snrChanged(); } }
void DspEngine::setModType(int type) { if (m_modType != type) { m_modType = type; syncParameters(); emit modTypeChanged(); } }
void DspEngine::setUseRrc(bool rrc) { if (m_useRrc != rrc) { m_useRrc = rrc; syncParameters(); emit useRrcChanged(); } }
void DspEngine::setGridOpacity(float opacity) { if (m_gridOpacity != opacity) { m_gridOpacity = opacity; emit gridOpacityChanged(); } }

void DspEngine::startSimulation() { m_timer->start(m_timerInterval); }
void DspEngine::stopSimulation() { m_timer->stop(); }


void DspEngine::setRollOff(float ro) {
    if (m_rollOff != ro) {
        m_rollOff = ro;
        syncParameters();
        emit rollOffChanged();
    }
}

void DspEngine::setTimerInterval(int ti) {
    if (m_timerInterval != ti) {
        m_timerInterval = ti;
        if (m_timer->isActive()) { m_timer->setInterval(m_timerInterval); }
        emit timerIntervalChanged();
    }
}