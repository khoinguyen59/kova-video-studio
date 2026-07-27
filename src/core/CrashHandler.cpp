#include "CrashHandler.h"

#ifdef _WIN32

#include "core/PathUtils.h"

#include <QDir>
#include <QString>

#include <windows.h>
#include <dbghelp.h>

#include <atomic>
#include <cstdlib>
#include <exception>
#include <string>

namespace LAStudio::CrashHandler {
namespace {

std::wstring s_crashDirectory;
std::atomic_flag s_writingDump = ATOMIC_FLAG_INIT;

std::wstring dumpPath()
{
    SYSTEMTIME now = {};
    GetSystemTime(&now);
    wchar_t filename[128] = {};
    swprintf_s(filename, L"lastudio-%04u%02u%02uT%02u%02u%02uZ-%lu.dmp",
               now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
               GetCurrentProcessId());
    return s_crashDirectory + L"\\" + filename;
}

void writeMiniDump(EXCEPTION_POINTERS *exceptionPointers)
{
    if (s_crashDirectory.empty() || s_writingDump.test_and_set()) {
        return;
    }

    const std::wstring path = dumpPath();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    MINIDUMP_EXCEPTION_INFORMATION exceptionInfo = {};
    MINIDUMP_EXCEPTION_INFORMATION *exceptionInfoPointer = nullptr;
    if (exceptionPointers) {
        exceptionInfo.ThreadId = GetCurrentThreadId();
        exceptionInfo.ExceptionPointers = exceptionPointers;
        exceptionInfo.ClientPointers = FALSE;
        exceptionInfoPointer = &exceptionInfo;
    }

    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                      MiniDumpWithIndirectlyReferencedMemory,
                      exceptionInfoPointer, nullptr, nullptr);
    CloseHandle(file);
}

LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS *exceptionPointers)
{
    writeMiniDump(exceptionPointers);
    return EXCEPTION_CONTINUE_SEARCH;
}

[[noreturn]] void terminateHandler()
{
    writeMiniDump(nullptr);
    std::abort();
}

} // namespace

void initialize()
{
    const QString crashDir = PathUtils::logsDir() + QStringLiteral("/crashes");
    if (!QDir().mkpath(crashDir)) {
        return;
    }
    s_crashDirectory = QDir::toNativeSeparators(crashDir).toStdWString();
    SetUnhandledExceptionFilter(unhandledExceptionFilter);
    std::set_terminate(terminateHandler);
}

} // namespace LAStudio::CrashHandler

#else

namespace LAStudio::CrashHandler {
void initialize() {}
} // namespace LAStudio::CrashHandler

#endif
