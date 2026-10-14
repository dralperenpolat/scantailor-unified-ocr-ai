// SPDX-License-Identifier: GPL-3.0-or-later
#include "UnifiedI18n.h"
#include "UnifiedResources.h"
#include <QThread>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
static HANDLE job = nullptr;
bool unifiedProcessRunning(qint64 pid) {
    if(pid<=0)return false;
    HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,DWORD(pid));
    if(!p)return false;
    DWORD code=0;bool running=GetExitCodeProcess(p,&code)&&code==STILL_ACTIVE;CloseHandle(p);return running;
}
QString unifiedApplyCpuLimit(int percent) {
    if (!job) {
        HANDLE candidate = CreateJobObjectW(nullptr, nullptr);
        if (!candidate || !AssignProcessToJobObject(candidate, GetCurrentProcess())) {
            const auto error = GetLastError();
            if (candidate) CloseHandle(candidate);
            return unified_i18n::text("CPU sınırı uygulanamadı (Windows %1)").arg(error);
        }
        job = candidate;
    }
    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION info{};
    if (percent < 100) {
        info.ControlFlags = JOB_OBJECT_CPU_RATE_CONTROL_ENABLE | JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;
        info.CpuRate = qBound(5, percent, 100) * 100;
    }
    if (!SetInformationJobObject(job, JobObjectCpuRateControlInformation, &info, sizeof(info)))
        return unified_i18n::text("CPU sınırı uygulanamadı (Windows %1)").arg(GetLastError());
    return percent == 100 ? unified_i18n::text("CPU üst sınırı kapalı")
                          : unified_i18n::text("CPU üst sınırı: toplam kapasitenin %%%1'i").arg(percent);
}
QString unifiedUsage() {
    static ULONGLONG lastWall=0, lastCpu=0;
    FILETIME creation, exit, kernel, user;
    GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user);
    ULARGE_INTEGER k{}, u{};k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;
    u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;
    const ULONGLONG wall=GetTickCount64(), cpu=k.QuadPart+u.QuadPart;
    double usage=lastWall && wall>lastWall ? double(cpu-lastCpu)/10000.0/(wall-lastWall)*100/qMax(1,QThread::idealThreadCount()):0;
    lastWall=wall;lastCpu=cpu;
    PROCESS_MEMORY_COUNTERS memory{};memory.cb=sizeof(memory);
    GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory));
    return unified_i18n::text("Ana uygulama CPU: %%%1 · RAM: %2 MB").arg(usage,0,'f',1).arg(memory.WorkingSetSize/1024/1024);
}
#else
#include <signal.h>
bool unifiedProcessRunning(qint64 pid) {return pid>0 && kill(pid,0)==0;}
QString unifiedApplyCpuLimit(int) {return unified_i18n::text("Bu sistemde CPU iş parçacığı sınırı kullanılabilir.");}
QString unifiedUsage() {return {};}
#endif
