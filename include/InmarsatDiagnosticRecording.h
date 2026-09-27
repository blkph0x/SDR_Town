#pragma once
#include <QByteArray>
#include <QString>
#include <cstdint>
#include <span>
#include <complex>
struct InmarsatAeroStats;

// DEC-0155: one explicitly armed recording, never a background rolling buffer.
namespace InmarsatDiagnosticRecording {
bool arm(double channelHz);
void cancel();
QString status();
QByteArray bundle();
void begin(const void* source, double channelHz, int mode, bool reset,
           std::span<const int16_t> modemInput);
void pcm(const void* source, std::span<const int16_t> samples);
void end(const void* source);
void release(const void* source);
void counters(const void* source,const InmarsatAeroStats& stats);
void iq(const void* source,std::span<const std::complex<float>> samples,
        double rate,double center,uint64_t start);
}
class QWidget;
void showInmarsatDiagnosticRecording(QWidget* parent, double channelHz);
