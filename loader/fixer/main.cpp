#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <filesystem>
#include <algorithm>
#include <dwmapi.h>
#include <urlmon.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(linker, "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")

static ULONG_PTR g_GdiToken = 0;
static HWND g_hWnd = NULL;
static std::vector<std::wstring> g_Logs;
static std::wstring g_CurrentStatus = L"initializing...";
static float g_Progress = 0.0f;
static bool g_Finished = false;
static bool g_HoverCopyBtn = false;
static RECT g_CopyBtnRect = { 310, 10, 390, 28 };
static std::wstring g_PassedLoaderPath = L"";

static void AddLog(const std::wstring& msg) {
    g_Logs.push_back(msg);
    if (g_hWnd) {
        InvalidateRect(g_hWnd, NULL, FALSE);
    }
}

static std::string ToLowerStringA(const std::string& s) {
    std::string r = s;
    for (auto& c : r) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return r;
}

static void CopyLogsToClipboard(HWND hWnd) {
    std::wstring fullLog;
    fullLog += L"=== PRIMORDIAL FIXER LOG ===\r\n";
    for (const auto& line : g_Logs) {
        fullLog += line + L"\r\n";
    }
    fullLog += L"=============================\r\n";

    if (OpenClipboard(hWnd)) {
        EmptyClipboard();
        size_t bytes = (fullLog.length() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hMem) {
            void* pMem = GlobalLock(hMem);
            if (pMem) {
                memcpy(pMem, fullLog.c_str(), bytes);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            }
        }
        CloseClipboard();
    }
}

static void KillProcessByName(const std::wstring& procName) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = { sizeof(pe) };
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, procName.c_str()) == 0 && pe.th32ProcessID != GetCurrentProcessId()) {
                    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (hProc) {
                        TerminateProcess(hProc, 0);
                        CloseHandle(hProc);
                    }
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
}

static std::string DetectSteamPath() {
    std::vector<std::string> candidates = {
        R"(C:\Program Files (x86)\Steam)",
        R"(C:\Program Files\Steam)"
    };
    for (auto& c : candidates) {
        if (std::filesystem::is_directory(c)) return c;
    }
    return "";
}

static std::vector<std::string> FindLocalConfigs(const std::string& steamPath) {
    std::vector<std::string> result;
    std::filesystem::path userdata = std::filesystem::path(steamPath) / "userdata";
    if (!std::filesystem::exists(userdata)) return result;
    try {
        for (auto& profileDir : std::filesystem::directory_iterator(userdata)) {
            if (!profileDir.is_directory()) continue;
            std::filesystem::path cfg = profileDir.path() / "config" / "localconfig.vdf";
            if (std::filesystem::exists(cfg)) result.push_back(cfg.string());
        }
    } catch (...) {}
    return result;
}

static size_t FindQuotedKeyCI(const std::string& s, const std::string& key, size_t from, size_t rangeEnd) {
    std::string lowerKey = ToLowerStringA(key);
    size_t i = from;
    while (i < rangeEnd && i < s.size()) {
        if (s[i] == '"') {
            size_t j = s.find('"', i + 1);
            if (j == std::string::npos || j > rangeEnd) break;
            std::string token = ToLowerStringA(s.substr(i + 1, j - i - 1));
            if (token == lowerKey) return i;
            i = j + 1;
        } else {
            ++i;
        }
    }
    return std::string::npos;
}

static size_t FindMatchingBrace(const std::string& s, size_t openPos) {
    int depth = 0;
    bool inQuotes = false;
    for (size_t i = openPos; i < s.size(); ++i) {
        char c = s[i];
        if (c == '"' && (i == 0 || s[i - 1] != '\\')) {
            inQuotes = !inQuotes;
        } else if (!inQuotes) {
            if (c == '{') {
                depth++;
            } else if (c == '}') {
                depth--;
                if (depth == 0) return i;
            }
        }
    }
    return std::string::npos;
}

static std::pair<size_t, size_t> FindChildBlock(const std::string& s, const std::string& key, size_t rangeStart, size_t rangeEnd) {
    size_t keyPos = FindQuotedKeyCI(s, key, rangeStart, rangeEnd);
    if (keyPos == std::string::npos) return {std::string::npos, std::string::npos};
    size_t openBrace = s.find('{', keyPos);
    if (openBrace == std::string::npos || openBrace >= rangeEnd) return {std::string::npos, std::string::npos};
    size_t closeBrace = FindMatchingBrace(s, openBrace);
    return {openBrace, closeBrace};
}

static std::pair<size_t, size_t> FindAppsBlock(std::string& content) {
    size_t docStart = 0, docEnd = content.size();
    auto [s1, e1] = FindChildBlock(content, "UserLocalConfigStore", docStart, docEnd);
    if (s1 == std::string::npos) return {std::string::npos, std::string::npos};
    auto [s2, e2] = FindChildBlock(content, "Software", s1, e1);
    if (s2 == std::string::npos) return {std::string::npos, std::string::npos};
    auto [s3, e3] = FindChildBlock(content, "Valve", s2, e2);
    if (s3 == std::string::npos) return {std::string::npos, std::string::npos};
    auto [s4, e4] = FindChildBlock(content, "Steam", s3, e3);
    if (s4 == std::string::npos) return {std::string::npos, std::string::npos};
    auto [s5, e5] = FindChildBlock(content, "apps", s4, e4);
    if (s5 == std::string::npos) {
        std::string insertion = "\n\t\t\t\"apps\"\n\t\t\t{\n\t\t\t}\n";
        content.insert(s4 + 1, insertion);
        return FindAppsBlock(content);
    }
    return {s5, e5};
}

static bool FixSteamVdf(const std::string& steamPath, const std::string& appId, const std::string& targetOpts) {
    auto configs = FindLocalConfigs(steamPath);
    if (configs.empty()) return false;
    bool anyOk = false;
    for (auto& path : configs) {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) continue;
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        f.close();

        if (content.empty()) continue;

        auto [appsOpen, appsClose] = FindAppsBlock(content);
        if (appsOpen == std::string::npos) continue;

        auto [aOpen, aClose] = FindChildBlock(content, appId, appsOpen, appsClose);
        if (aOpen == std::string::npos) {
            std::string insertion = "\n\t\t\t\t\"" + appId + "\"\n\t\t\t\t{\n" +
                                     "\t\t\t\t\t\"LaunchOptions\"\t\t\"" + targetOpts + "\"\n" +
                                     "\t\t\t\t}\n";
            content.insert(appsOpen + 1, insertion);
        } else {
            size_t loKeyPos = FindQuotedKeyCI(content, "LaunchOptions", aOpen, aClose);
            if (loKeyPos == std::string::npos || loKeyPos >= aClose) {
                std::string insertion = "\n\t\t\t\t\t\"LaunchOptions\"\t\t\"" + targetOpts + "\"\n";
                content.insert(aOpen + 1, insertion);
            } else {
                size_t keyCloseQuote = content.find('"', loKeyPos + 1);
                if (keyCloseQuote != std::string::npos) {
                    size_t valOpenQuote = content.find('"', keyCloseQuote + 1);
                    if (valOpenQuote != std::string::npos && valOpenQuote < aClose) {
                        size_t valCloseQuote = content.find('"', valOpenQuote + 1);
                        if (valCloseQuote != std::string::npos && valCloseQuote <= aClose) {
                            std::string curVal = content.substr(valOpenQuote + 1, valCloseQuote - valOpenQuote - 1);
                            if (curVal.find("-insecure") == std::string::npos) {
                                std::string newVal = curVal.empty() ? targetOpts : (curVal + " " + targetOpts);
                                content.replace(valOpenQuote + 1, valCloseQuote - valOpenQuote - 1, newVal);
                            }
                        }
                    }
                }
            }
        }

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (out.is_open()) {
            out << content;
            out.close();
            anyOk = true;
        }
    }
    return anyOk;
}

static void RunFixerPipeline() {
    AddLog(L"[+] Starting primordial fixer pipeline...");
    g_Progress = 0.1f;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // 1. Terminate running processes
    g_CurrentStatus = L"closing processes...";
    AddLog(L"[+] Closing csgo.exe & loader.exe...");
    KillProcessByName(L"csgo.exe");
    KillProcessByName(L"loader.exe");
    KillProcessByName(L"test_error.exe");
    g_Progress = 0.3f;
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    // 2. Search CS:GO path & log
    g_CurrentStatus = L"locating csgo...";
    std::vector<std::wstring> drives = { L"C:", L"D:", L"E:", L"F:", L"G:", L"H:", L"I:", L"J:", L"K:" };
    std::wstring foundCsgoPath = L"";
    for (const auto& drive : drives) {
        std::vector<std::wstring> candidates = {
            drive + L"\\Program Files (x86)\\Steam\\steamapps\\common\\csgo legacy",
            drive + L"\\SteamLibrary\\steamapps\\common\\csgo legacy",
            drive + L"\\Program Files\\Steam\\steamapps\\common\\csgo legacy",
            drive + L"\\Steam\\steamapps\\common\\csgo legacy",
            drive + L"\\Program Files (x86)\\Steam\\steamapps\\common\\Counter-Strike Global Offensive",
            drive + L"\\SteamLibrary\\steamapps\\common\\Counter-Strike Global Offensive",
            drive + L"\\Program Files\\Steam\\steamapps\\common\\Counter-Strike Global Offensive",
            drive + L"\\Steam\\steamapps\\common\\Counter-Strike Global Offensive"
        };
        for (const auto& p : candidates) {
            if (std::filesystem::exists(p + L"\\csgo.exe")) {
                foundCsgoPath = p;
                break;
            }
        }
        if (!foundCsgoPath.empty()) break;
    }

    if (!foundCsgoPath.empty()) {
        AddLog(L"[+] CS:GO path: " + foundCsgoPath);
    } else {
        AddLog(L"[-] CS:GO not found automatically!");
        AddLog(L"[*] Please launch CS:GO manually now...");
        g_CurrentStatus = L"waiting for user to launch csgo...";
        
        // Wait up to 60 seconds for user to launch it
        bool manualFound = false;
        for (int i = 0; i < 60; i++) {
            HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (hSnap != INVALID_HANDLE_VALUE) {
                PROCESSENTRY32W pe = { sizeof(pe) };
                if (Process32FirstW(hSnap, &pe)) {
                    do {
                        if (_wcsicmp(pe.szExeFile, L"csgo.exe") == 0) {
                            HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                            if (hProc) {
                                wchar_t exeP[MAX_PATH];
                                DWORD size = MAX_PATH;
                                if (QueryFullProcessImageNameW(hProc, 0, exeP, &size)) {
                                    foundCsgoPath = std::filesystem::path(exeP).parent_path().wstring();
                                    manualFound = true;
                                }
                                CloseHandle(hProc);
                            }
                            break; // found the process
                        }
                    } while (Process32NextW(hSnap, &pe));
                }
                CloseHandle(hSnap);
            }
            if (manualFound) break;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        if (manualFound) {
            AddLog(L"[+] Detected manual launch!");
            AddLog(L"[+] CS:GO path: " + foundCsgoPath);
            KillProcessByName(L"csgo.exe"); // Kill it so we can fix it
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        } else {
            AddLog(L"[-] Timeout waiting for CS:GO. Falling back to default.");
        }
    }
    g_Progress = 0.5f;
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    // 3. Selective Cache Wipe (Strictly primordial/files folder only! Preserves configs and scripts!)
    g_CurrentStatus = L"wiping files cache...";
    AddLog(L"[+] Purging primordial/files folder (configs & scripts preserved)...");
    if (!foundCsgoPath.empty()) {
        try {
            std::filesystem::path primFilesDir = std::filesystem::path(foundCsgoPath) / L"primordial" / L"files";
            if (std::filesystem::exists(primFilesDir)) {
                std::filesystem::remove_all(primFilesDir);
            }
        } catch (...) {}
    }

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
    try {
        std::filesystem::path localFilesDir = exeDir / L"primordial" / L"files";
        if (std::filesystem::exists(localFilesDir)) {
            std::filesystem::remove_all(localFilesDir);
        }
    } catch (...) {}

    // Clean TEMP primordial/files cache only (avoid touching configs/scripts)
    wchar_t tempPath[MAX_PATH];
    if (GetTempPathW(MAX_PATH, tempPath) > 0) {
        try {
            std::filesystem::path tempFilesDir = std::filesystem::path(tempPath) / L"primordial" / L"files";
            if (std::filesystem::exists(tempFilesDir)) {
                std::filesystem::remove_all(tempFilesDir);
            }
        } catch (...) {}
    }
    g_Progress = 0.7f;
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    // 4. Fix Steam VDF
    g_CurrentStatus = L"patching steam vdf...";
    AddLog(L"[+] Patching Steam localconfig.vdf (-insecure)...");
    std::string steamPath = DetectSteamPath();
    if (!steamPath.empty()) {
        FixSteamVdf(steamPath, "4465480", "-insecure");
        FixSteamVdf(steamPath, "730", "-insecure");
    }
    g_Progress = 0.85f;
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    // 5. Check Admin Privileges & System Security Diagnostics
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuth, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    AddLog(L"[+] Admin privileges: " + std::wstring(isAdmin ? L"OK" : L"WARNING: Limited Rights"));
    
    // Check retry counter stored in TEMP
    wchar_t tempDirBuf[MAX_PATH];
    int fixAttempts = 1;
    std::wstring countFilePath = L"";
    if (GetTempPathW(MAX_PATH, tempDirBuf) > 0) {
        countFilePath = std::wstring(tempDirBuf) + L"primordial_fix_attempts.txt";
        std::ifstream countIn(countFilePath);
        if (countIn.is_open()) {
            countIn >> fixAttempts;
            fixAttempts++;
            countIn.close();
        }
        std::ofstream countOut(countFilePath);
        if (countOut.is_open()) {
            countOut << fixAttempts;
            countOut.close();
        }
    }

    if (fixAttempts >= 2) {
        AddLog(L"[!] Unresolved issue detected (Attempt #" + std::to_wstring(fixAttempts) + L")");
        AddLog(L"[!] Possible causes:");
        AddLog(L"    1. Antivirus / Windows Defender blocking DLL injection");
        AddLog(L"    2. Missing Visual C++ Redistributable (2015-2022 x64/x86)");
        AddLog(L"    3. CS:GO path permission restrictions");
        AddLog(L"[!] For assistance, contact: @saved_messenges (Telegram)");
    } else {
        AddLog(L"[+] System diagnostic check completed.");
    }
    // 5.5 Check and restore missing core DLL modules from GitHub
    g_CurrentStatus = L"verifying core modules...";
    AddLog(L"[+] Checking core DLL modules...");
    std::vector<std::wstring> requiredDlls = { L"primordial_discord.dll", L"primordial_core.dll", L"primordial_graphics.dll" };
    for (const auto& dllName : requiredDlls) {
        std::filesystem::path dllPath = exeDir / dllName;
        if (!std::filesystem::exists(dllPath)) {
            AddLog(L"[*] Missing module detected: " + dllName + L", downloading...");
            std::wstring rawUrl = L"https://raw.githubusercontent.com/barabulka14221422-commits/Primordial-CSGO/main/" + dllName;
            HRESULT hr = URLDownloadToFileW(NULL, rawUrl.c_str(), dllPath.c_str(), 0, NULL);
            if (SUCCEEDED(hr) && std::filesystem::exists(dllPath) && std::filesystem::file_size(dllPath) > 0) {
                AddLog(L"[+] Successfully restored " + dllName);
            } else {
                AddLog(L"[-] Failed to download " + dllName);
            }
        }
    }

    g_Progress = 0.95f;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // 6. Locate & Restart Loader.exe
    g_CurrentStatus = L"restarting loader...";
    AddLog(L"[+] Restarting loader.exe...");
    g_Progress = 1.0f;
    g_Finished = true;

    std::wstring loaderToRun = L"";
    if (!g_PassedLoaderPath.empty() && std::filesystem::exists(g_PassedLoaderPath)) {
        loaderToRun = g_PassedLoaderPath;
    } else {
        std::vector<std::wstring> searchPaths = {
            (exeDir / L"Primordial.exe").wstring(),
            (exeDir / L"loader.exe").wstring(),
            (exeDir.parent_path() / L"Primordial.exe").wstring(),
            (exeDir.parent_path() / L"loader.exe").wstring(),
            (exeDir / L"primka\\Primordial.exe").wstring(),
            (exeDir / L"primka\\loader.exe").wstring(),
            LR"(C:\Users\rafa\Desktop\Projects\primordial\primka\Primordial.exe)",
            LR"(C:\Users\rafa\Desktop\Projects\primordial\primka\loader.exe)",
            LR"(C:\Users\kolos\Desktop\Projects\primordial\primka\Primordial.exe)",
            LR"(C:\Users\kolos\Desktop\Projects\primordial\primka\loader.exe)"
        };
        for (const auto& p : searchPaths) {
            if (std::filesystem::exists(p)) {
                loaderToRun = p;
                break;
            }
        }
    }

    if (!loaderToRun.empty()) {
        AddLog(L"[+] Launching: " + loaderToRun);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        ShellExecuteW(NULL, L"open", loaderToRun.c_str(), NULL, NULL, SW_SHOWNORMAL);
    } else {
        AddLog(L"[-] Error: Could not locate loader.exe!");
        AddLog(L"[!] Contact @saved_messenges (Telegram) for support.");
        return;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    PostMessage(g_hWnd, WM_CLOSE, 0, 0);
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_hWnd = hWnd;
        SetTimer(hWnd, 1, 30, NULL);
        std::thread(RunFixerPipeline).detach();
        return 0;

    case WM_TIMER:
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rc;
        GetClientRect(hWnd, &rc);

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP hBM = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP hOld = (HBITMAP)SelectObject(memDC, hBM);

        Gdiplus::Graphics g(memDC);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // 1. Pure Black Background
        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 0, 0, 0));
        g.FillRectangle(&bgBrush, 0, 0, rc.right, rc.bottom);

        // 2. Ice-blue outer border
        Gdiplus::Pen borderPen(Gdiplus::Color(255, 180, 215, 255), 1.5f);
        g.DrawRectangle(&borderPen, 0, 0, rc.right - 1, rc.bottom - 1);

        Gdiplus::FontFamily fontFamily(L"Segoe UI");
        Gdiplus::Font titleFont(&fontFamily, 11, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font btnFont(&fontFamily, 10, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::SolidBrush dimBrush(Gdiplus::Color(255, 140, 180, 220));

        // Title
        g.DrawString(L"PRIMORDIAL FIXER", -1, &titleFont, Gdiplus::PointF(12.0f, 10.0f), &whiteBrush);

        // Logs Output List
        Gdiplus::Font logFont(&fontFamily, 10, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush logBrush(Gdiplus::Color(255, 200, 220, 245));

        int startY = 38;
        int maxLines = (rc.bottom - 45) / 18;
        int startIndex = 0;
        if ((int)g_Logs.size() > maxLines) {
            startIndex = (int)g_Logs.size() - maxLines;
        }

        for (int i = startIndex; i < (int)g_Logs.size(); i++) {
            g.DrawString(g_Logs[i].c_str(), -1, &logFont, Gdiplus::PointF(14.0f, (float)startY), &logBrush);
            startY += 18;
        }

        // Bottom 2px progress bar
        int barW = (int)(rc.right * g_Progress);
        if (barW > 0) {
            Gdiplus::SolidBrush progressBrush(Gdiplus::Color(255, 255, 255, 255));
            g.FillRectangle(&progressBrush, 0, rc.bottom - 2, barW, 2);
        }

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, hOld);
        DeleteObject(hBM);
        DeleteDC(memDC);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR lpCmdLine, int nCmdShow) {
    if (__wargv && __argc > 1) {
        g_PassedLoaderPath = __wargv[1];
    }

    Gdiplus::GdiplusStartupInput gdiInput;
    Gdiplus::GdiplusStartup(&g_GdiToken, &gdiInput, NULL);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"PrimordialFixerWndClass";
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExW(&wc);

    int w = 400;
    int h = 240;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

    HWND hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"PrimordialFixerWndClass",
        L"Primordial Fixer",
        WS_POPUP | WS_VISIBLE,
        x, y, w, h,
        NULL, NULL, hInstance, NULL
    );

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Gdiplus::GdiplusShutdown(g_GdiToken);
    return 0;
}
