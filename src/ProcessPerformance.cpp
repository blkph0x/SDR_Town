#include "ProcessPerformance.h"
#include <QThread>
#include <algorithm>
#include <cmath>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#else
#include <sys/resource.h>
#endif

double ProcessPerformance::cpuPercent(double cpu,double wall,int cpus) {
    if(!std::isfinite(cpu)||!std::isfinite(wall)||cpu<0||wall<=0||cpus<1)return 0;
    return std::clamp(100*cpu/(wall*cpus),0.0,100.0);
}
QJsonObject ProcessPerformance::sample() {
    QJsonObject result;
    const int cpus=std::max(1,QThread::idealThreadCount());result["logicalCpus"]=cpus;
    double cpu=0;bool measured=false;
#ifdef _WIN32
    FILETIME created{},exited{},kernel{},user{};
    if(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)) {
        const auto seconds=[](FILETIME t){return double((uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime)*1e-7;};
        cpu=seconds(kernel)+seconds(user);measured=true;
    }
    PROCESS_MEMORY_COUNTERS_EX memory{};
    if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory))) {
        result["workingSetBytes"]=double(memory.WorkingSetSize);
        result["privateBytes"]=double(memory.PrivateUsage);
        result["pageFaultCount"]=double(memory.PageFaultCount);
    }
    DWORD handles=0;if(GetProcessHandleCount(GetCurrentProcess(),&handles))result["handleCount"]=double(handles);
    // Sample only every 30 s on the diagnostics thread, never the GUI or DSP thread.
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    if(snapshot!=INVALID_HANDLE_VALUE) {
        THREADENTRY32 entry{};entry.dwSize=sizeof(entry);int threads=0;
        if(Thread32First(snapshot,&entry))do {if(entry.th32OwnerProcessID==GetCurrentProcessId())++threads;}while(Thread32Next(snapshot,&entry));
        CloseHandle(snapshot);result["threadCount"]=threads;
    }
#else
    rusage usage{};
    if(getrusage(RUSAGE_SELF,&usage)==0){cpu=usage.ru_utime.tv_sec+usage.ru_stime.tv_sec+
        (usage.ru_utime.tv_usec+usage.ru_stime.tv_usec)/1e6;measured=true;}
#endif
    const auto now=std::chrono::steady_clock::now();
    if(measured) {
        result["processCpuSeconds"]=cpu;
        if(previous_!=std::chrono::steady_clock::time_point{} && cpu>=previousCpu_) {
            const double seconds=std::chrono::duration<double>(now-previous_).count();
            result["intervalSeconds"]=seconds;
            result["cpuCapacityPercent"]=cpuPercent(cpu-previousCpu_,seconds,cpus);
        }
        previous_=now;previousCpu_=cpu;
    }
    return result;
}
