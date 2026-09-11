#pragma once

// Purpose: Process bootstrap — logging, theme, single-instance, crash markers.
// Spec:    docs/DECISIONS.md DEC-0040 / ISS-0004 Phase 8 (mechanical extract from main.cpp)
// Invariants: no hop/TTL/CADENCE/feed-gate behavior changes — move-only.

#include <QApplication>
#include <QtGlobal>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

QByteArray readFileTailCapped(const QString& path, qint64 maxBytes);
QString diagnosticsBytesHash(const QByteArray& bytes, int chars = 16);
QJsonObject diagnosticsAttachmentSummary(const QString& path, qint64 maxContentBytes = 8192);
QJsonArray diagnosticsDeviceInventory(bool includeRuntimeState);
QJsonObject diagnosticsSystemHealthPayload();
QJsonArray diagnosticsSystemHealthPressureReasons(const QJsonObject& payload);
QString diagnosticsSystemHealthPressureSummary(const QJsonArray& reasons);
bool diagnosticsSystemHealthLooksBad(const QJsonObject& payload);


#include <initializer_list>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

void setupLogging();
void writeEarlyCrashLog(const std::string& stage, const char* extra = nullptr);
void submitPreviousLaunchCrashIfAny();
void printStartupUsage();
bool startupHasArg(int argc, char* argv[], std::initializer_list<const char*> names,
                   bool ignoreCommandPayload = false);
void applyDarkTheme(QApplication& app);

#ifdef _WIN32
class ProcessInstanceGuard
{
public:
    ProcessInstanceGuard() = default;
    ~ProcessInstanceGuard()
    {
        if (m_handle) {
            ReleaseMutex(m_handle);
            CloseHandle(m_handle);
        }
    }

    ProcessInstanceGuard(const ProcessInstanceGuard&) = delete;
    ProcessInstanceGuard& operator=(const ProcessInstanceGuard&) = delete;

    bool acquire()
    {
        if (m_handle) return !m_alreadyRunning;
        m_handle = CreateMutexA(nullptr, TRUE, "Local\\SDR_Town_Single_Instance_Device_Lock");
        if (!m_handle) {
            m_error = GetLastError();
            return true; // Fail open: a mutex failure should not brick startup.
        }
        m_error = GetLastError();
        m_alreadyRunning = (m_error == ERROR_ALREADY_EXISTS || m_error == ERROR_ACCESS_DENIED);
        return !m_alreadyRunning;
    }

    DWORD error() const noexcept { return m_error; }

private:
    HANDLE m_handle = nullptr;
    DWORD m_error = ERROR_SUCCESS;
    bool m_alreadyRunning = false;
};

LONG WINAPI sehTopLevelFilter(EXCEPTION_POINTERS* info);
#endif
