// SPDX-License-Identifier: LGPL-2.1-or-later
#include "detail.hpp"
#include <chrono>
#include <vector>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif
namespace pd::io {
#ifdef _WIN32
std::wstring quote(const std::wstring& s) {
    std::wstring cmd = L"\"";
    unsigned slashes = 0;
    for (wchar_t c : s) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        cmd.append(c == L'"' ? 2 * slashes + 1 : slashes, L'\\');
        cmd += c;
        slashes = 0;
    }
    cmd.append(2 * slashes, L'\\');
    return cmd + L'"';
}
#endif
std::string runWorker(const fs::path& tool, const fs::path& in, const fs::path& out, const fs::path& work,
                      const std::string& version) {
    if (!tool.is_absolute() || !fs::is_regular_file(tool))
        throw Failure(IoStatus::BackendUnavailable, "DWG converter not installed/configured");
    if (!fs::is_regular_file(in))
        throw Failure(IoStatus::FileError, "input file does not exist");
    if (fs::file_size(in) > maxFileBytes)
        throw Failure(IoStatus::InvalidData, "converter input exceeds size limit");
    auto log = work / "stderr.txt";
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    struct Handle {
        HANDLE h;
        ~Handle() {
            if (h != INVALID_HANDLE_VALUE)
                CloseHandle(h);
        }
    };
    Handle err{CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, nullptr)},
        nul{CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                        OPEN_EXISTING, 0, nullptr)};
    if (err.h == INVALID_HANDLE_VALUE || nul.h == INVALID_HANDLE_VALUE)
        throw Failure(IoStatus::BackendError, "cannot redirect converter output");
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof si;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdError = err.h;
    si.StartupInfo.hStdInput = si.StartupInfo.hStdOutput = nul.h;
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<unsigned char> buf(bytes);
    si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buf.data());
    if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &bytes))
        throw Failure(IoStatus::BackendError, "cannot initialize worker attributes");
    struct Attributes {
        LPPROC_THREAD_ATTRIBUTE_LIST p;
        ~Attributes() { DeleteProcThreadAttributeList(p); }
    } attrs{si.lpAttributeList};
    HANDLE allowed[] = {err.h, nul.h};
    if (!UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, allowed,
                                   sizeof allowed, nullptr, nullptr))
        throw Failure(IoStatus::BackendError, "cannot restrict inherited handles");
    std::wstring args = quote(tool.wstring());
    if (!version.empty())
        args += L" -a " + std::wstring(version.begin(), version.end());
    args += L" -o " + quote(out.wstring()) + L" " + quote(in.wstring());
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(tool.c_str(), args.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, work.c_str(),
                        &si.StartupInfo, &pi))
        throw Failure(IoStatus::BackendUnavailable, "cannot start DWG converter");
    CloseHandle(pi.hThread);
    auto w = WaitForSingleObject(pi.hProcess, 60000);
    DWORD code = 1;
    if (w != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, INFINITE);
    } else
        GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    if (w != WAIT_OBJECT_0)
        throw Failure(IoStatus::BackendError, "DWG converter timed out");
#else
    std::vector<std::string> args{tool.string()};
    if (!version.empty()) {
        args.push_back("-a");
        args.push_back(version);
    }
    args.push_back("-o");
    args.push_back(out.string());
    args.push_back(in.string());
    std::vector<char*> argv;
    for (auto& a : args)
        argv.push_back(a.data());
    argv.push_back(nullptr);
    pid_t pid;
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions))
        throw Failure(IoStatus::BackendError, "cannot initialize converter IO");
    struct Actions {
        posix_spawn_file_actions_t* p;
        ~Actions() { posix_spawn_file_actions_destroy(p); }
    } cleanup{&actions};
    if (posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0) ||
        posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0) ||
        posix_spawn_file_actions_addopen(&actions, 2, log.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600))
        throw Failure(IoStatus::BackendError, "cannot redirect converter IO");
    int err = posix_spawn(&pid, tool.c_str(), &actions, nullptr, argv.data(), environ);
    if (err)
        throw Failure(IoStatus::BackendUnavailable, "cannot start DWG converter");
    int status = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    for (;;) {
        auto n = waitpid(pid, &status, WNOHANG);
        if (n == pid)
            break;
        if (n < 0 && errno != EINTR)
            throw Failure(IoStatus::BackendError, "cannot wait for converter");
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
            throw Failure(IoStatus::BackendError, "DWG converter timed out");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
    std::ifstream errors(log, std::ios::binary);
    std::string msg(4096, '\0');
    errors.read(msg.data(), static_cast<std::streamsize>(msg.size()));
    msg.resize(static_cast<size_t>(errors.gcount()));
    if (code != 0)
        throw Failure(IoStatus::BackendError, "DWG converter exit code " + std::to_string(code) + ": " + msg);
    if (!fs::is_regular_file(out) || !fs::file_size(out) || fs::file_size(out) > maxFileBytes)
        throw Failure(IoStatus::BackendError, "missing, empty or oversized converter output");
    return msg;
}
} // namespace pd::io
