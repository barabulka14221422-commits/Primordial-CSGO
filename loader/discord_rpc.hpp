#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>

class DiscordRPC {
private:
    std::atomic<bool> m_running{ false };
    std::thread m_workerThread;
    uint64_t m_startTimestamp{ 0 };

    bool SendFrame(HANDLE hPipe, uint32_t opcode, const std::string& jsonPayload) {
        if (hPipe == INVALID_HANDLE_VALUE) return false;

        uint32_t header[2];
        header[0] = opcode;
        header[1] = static_cast<uint32_t>(jsonPayload.length());

        DWORD written = 0;
        if (!WriteFile(hPipe, header, sizeof(header), &written, NULL)) return false;
        if (!WriteFile(hPipe, jsonPayload.c_str(), static_cast<DWORD>(jsonPayload.length()), &written, NULL)) return false;

        // Read response header & payload to keep pipe clear
        uint32_t respHeader[2] = { 0 };
        DWORD readBytes = 0;
        if (ReadFile(hPipe, respHeader, sizeof(respHeader), &readBytes, NULL) && readBytes == sizeof(respHeader)) {
            if (respHeader[1] > 0) {
                std::string buf(respHeader[1], '\0');
                ReadFile(hPipe, &buf[0], respHeader[1], &readBytes, NULL);
            }
        }
        return true;
    }

    HANDLE ConnectPipe() {
        for (int i = 0; i < 10; ++i) {
            wchar_t pipeName[64];
            swprintf_s(pipeName, L"\\\\.\\pipe\\discord-ipc-%d", i);
            HANDLE hPipe = CreateFileW(pipeName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
            if (hPipe != INVALID_HANDLE_VALUE) {
                // Handshake (Opcode 0)
                std::string handshake = "{\"v\":1,\"client_id\":\"1535990681460408361\"}";
                if (SendFrame(hPipe, 0, handshake)) {
                    return hPipe;
                }
                CloseHandle(hPipe);
            }
        }
        return INVALID_HANDLE_VALUE;
    }

    void WorkerLoop() {
        HANDLE hPipe = INVALID_HANDLE_VALUE;
        m_startTimestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();

        while (m_running) {
            if (hPipe == INVALID_HANDLE_VALUE) {
                hPipe = ConnectPipe();
            }

            if (hPipe != INVALID_HANDLE_VALUE) {
                DWORD targetPid = 0;
                HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
                if (snapshot != INVALID_HANDLE_VALUE) {
                    PROCESSENTRY32 pe = { sizeof(pe) };
                    if (Process32First(snapshot, &pe)) {
                        do {
                            if (_wcsicmp(pe.szExeFile, L"csgo.exe") == 0) {
                                targetPid = pe.th32ProcessID;
                                break;
                            }
                        } while (Process32Next(snapshot, &pe));
                    }
                    CloseHandle(snapshot);
                }
                if (targetPid == 0) targetPid = GetCurrentProcessId();

                char activityJson[1024];
                snprintf(activityJson, sizeof(activityJson),
                    "{"
                    "\"cmd\":\"SET_ACTIVITY\","
                    "\"args\":{"
                        "\"pid\":%lu,"
                        "\"activity\":{"
                            "\"state\":\"@saved_messenges\","
                            "\"timestamps\":{\"start\":%llu},"
                            "\"assets\":{"
                                "\"large_image\":\"https://files.catbox.moe/1lvygu.gif\","
                                "\"large_text\":\"Primordial\""
                            "}"
                        "}"
                    "},"
                    "\"nonce\":\"primordial_rpc\""
                    "}",
                    targetPid,
                    m_startTimestamp
                );

                if (!SendFrame(hPipe, 1, std::string(activityJson))) {
                    CloseHandle(hPipe);
                    hPipe = INVALID_HANDLE_VALUE;
                }
            }

            // Sleep 500ms between RPC updates for instant override
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        if (hPipe != INVALID_HANDLE_VALUE) {
            CloseHandle(hPipe);
        }
    }

public:
    DiscordRPC() = default;
    ~DiscordRPC() { Stop(); }

    void Start() {
        if (!m_running) {
            m_running = true;
            m_workerThread = std::thread(&DiscordRPC::WorkerLoop, this);
        }
    }

    void Stop() {
        if (m_running) {
            m_running = false;
            if (m_workerThread.joinable()) {
                m_workerThread.join();
            }
        }
    }
};

inline DiscordRPC g_DiscordRPC;
