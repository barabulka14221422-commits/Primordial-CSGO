#include <windows.h>
#include <tlhelp32.h>
#include <gdiplus.h>
#include <mfapi.h>
#include <mfplay.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <thread>
#include <chrono>
#include <atomic>
#include <cmath>
#include "crypt.h"
#include "resource.h"
#include "discord_rpc.hpp"
#include <urlmon.h>
#include <wininet.h>
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "mfplay.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "urlmon.lib")

static std::wstring g_UnpackedFilesDir = L"";
static std::atomic<bool> g_UnpackingDone{ false };

static bool UnpackResourceToFile(int resId, const std::wstring& destPath) {
    try {
        if (std::filesystem::exists(destPath) && std::filesystem::file_size(destPath) > 0) {
            return true; // Already exists, skip re-extracting!
        }
        std::filesystem::create_directories(std::filesystem::path(destPath).parent_path());

        HRSRC hRes = FindResourceW(GetModuleHandle(NULL), MAKEINTRESOURCEW(resId), RT_RCDATA);
        if (!hRes) return false;

        DWORD dwSize = SizeofResource(GetModuleHandle(NULL), hRes);
        if (dwSize == 0) return false;

        HGLOBAL hGlob = LoadResource(GetModuleHandle(NULL), hRes);
        if (!hGlob) return false;

        void* pData = LockResource(hGlob);
        if (!pData) return false;

        HANDLE hFile = CreateFileW(destPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            hFile = CreateFileW(destPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile == INVALID_HANDLE_VALUE) return false;
        }

        DWORD dwWritten = 0;
        WriteFile(hFile, pData, dwSize, &dwWritten, NULL);
        CloseHandle(hFile);
        return (dwWritten == dwSize);
    } catch (...) {
        return false;
    }
}

enum CsgoDetectionMode {
    MODE_NONE = 0,
    MODE_LEGACY_AUTO,      // csgo legacy path found -> auto-launch steam://run/4465480
    MODE_LEGACY_BETA_MANUAL // C:\Program Files (x86)\...\Counter-Strike Global Offensive\csgo.exe found
};

static CsgoDetectionMode g_DetectionMode = MODE_NONE;

#include <shobjidl.h>
#include <objbase.h>
#include <shlguid.h>

static void CreateDesktopShortcut() {
    CoInitialize(NULL);
    IShellLinkW* psl;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (LPVOID*)&psl))) {
        IPersistFile* ppf;
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        psl->SetPath(exePath);
        psl->SetIconLocation(exePath, 0); // Use our own icon
        if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf))) {
            wchar_t desktop[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktop))) {
                std::wstring linkPath = std::wstring(desktop) + L"\\Primordial.lnk";
                // Only create if it doesn't exist
                if (!std::filesystem::exists(linkPath)) {
                    ppf->Save(linkPath.c_str(), TRUE);
                }
            }
            ppf->Release();
        }
        psl->Release();
    }
    CoUninitialize();
}

static void InitializeFolderAndUnpackResources() {
    g_DetectionMode = MODE_NONE;
    bool targetFound = false;

    std::vector<std::wstring> drives = { L"C:", L"D:", L"E:", L"F:", L"G:", L"H:", L"I:", L"J:", L"K:" };

    // 1. Check for 'csgo legacy' folder across all drive candidates
    for (const auto& drive : drives) {
        std::vector<std::wstring> candidates = {
            drive + L"\\Program Files (x86)\\Steam\\steamapps\\common",
            drive + L"\\SteamLibrary\\steamapps\\common",
            drive + L"\\Program Files\\Steam\\steamapps\\common",
            drive + L"\\Steam\\steamapps\\common",
            drive + L"\\Games\\SteamLibrary\\steamapps\\common",
            drive + L"\\Games\\Steam\\steamapps\\common"
        };
        for (const auto& commonDir : candidates) {
            if (std::filesystem::exists(commonDir + L"\\csgo legacy")) {
                g_DetectionMode = MODE_LEGACY_AUTO;
                targetFound = true;
                break;
            }
            if (std::filesystem::exists(commonDir + L"\\Counter-Strike Global Offensive\\csgo.exe")) {
                g_DetectionMode = MODE_LEGACY_BETA_MANUAL;
                targetFound = true;
                break;
            }
        }
        if (targetFound) break;
    }

    if (!targetFound) g_DetectionMode = MODE_NONE;

    // Always unpack to %APPDATA%\primordial\files for a true "installer" feel
    wchar_t appData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) {
        g_UnpackedFilesDir = std::wstring(appData) + L"\\primordial\\files";
    } else {
        // Fallback if appdata fails
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
        g_UnpackedFilesDir = (exeDir / L"primordial\\files").wstring();
    }
    
    std::filesystem::create_directories(g_UnpackedFilesDir);

    // NOTE: prim.dll is NO LONGER unpacked to disk!
    // It is loaded from embedded resource directly into memory during injection.
    // Only unpack non-DLL assets that need to be on disk:
    UnpackResourceToFile(IDR_LOADING_GIF, g_UnpackedFilesDir + L"\\loading.gif");
    UnpackResourceToFile(IDR_ERROR_PNG, g_UnpackedFilesDir + L"\\error.png");
    UnpackResourceToFile(IDR_LOADING_MP4, g_UnpackedFilesDir + L"\\loading.mp4");
    
}

static void PlayClickSound() {
    HRSRC hRes = FindResource(NULL, MAKEINTRESOURCE(IDR_CLICK_WAV), RT_RCDATA);
    if (hRes) {
        HGLOBAL hMem = LoadResource(NULL, hRes);
        if (hMem) {
            void* pData = LockResource(hMem);
            if (pData) {
                PlaySoundW((LPCWSTR)pData, NULL, SND_MEMORY | SND_ASYNC);
            }
        }
    }
}

enum LoaderState {
    STATE_INSTALLING,
    STATE_IDLE,
    STATE_EXPANDING,
    STATE_WAITING_MANUAL,
    STATE_LOADING,
    STATE_ERROR,
    STATE_UPDATE_AVAILABLE
};

static std::atomic<LoaderState> g_State(STATE_INSTALLING);
static std::wstring g_StatusMessage = L"";
static std::wstring g_ErrorMessage = L"";
static std::wstring g_ConflictingProcessName = L"";
static bool g_IsConflictError = false;
static bool g_BypassConflictCheck = false;
static const int WND_INIT_WIDTH = 290;
static const int WND_INIT_HEIGHT = 168;
static const int WND_EXP_WIDTH = 400;
static const int WND_EXP_HEIGHT = 420;

static int g_CurrentWidth = WND_INIT_WIDTH;
static int g_CurrentHeight = WND_INIT_HEIGHT;

static HWND g_hMainWnd = NULL;
static std::wstring GITHUB_RAW_URL_VERSION = L"https://raw.githubusercontent.com/barabulka14221422-commits/Primordial-CSGO/main/version.txt";

static std::wstring g_NewsText = L"";
static float g_NewsScrollX = 0.0f;
static POINT g_MousePos = { -100, -100 };
static bool g_IsMouseInWindow = false;

static bool CheckVCRedistInstalled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\X86", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD installed = 0;
        DWORD size = sizeof(installed);
        if (RegQueryValueExW(hKey, L"Installed", NULL, NULL, (LPBYTE)&installed, &size) == ERROR_SUCCESS && installed == 1) {
            RegCloseKey(hKey);
            return true;
        }
        RegCloseKey(hKey);
    }
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\X86", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD installed = 0;
        DWORD size = sizeof(installed);
        if (RegQueryValueExW(hKey, L"Installed", NULL, NULL, (LPBYTE)&installed, &size) == ERROR_SUCCESS && installed == 1) {
            RegCloseKey(hKey);
            return true;
        }
        RegCloseKey(hKey);
    }
    return false;
}

static void InstallVCRedistSilently() {
    if (CheckVCRedistInstalled()) return;
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring redistExe = std::wstring(tempPath) + L"vc_redist.x86.exe";
    HRESULT hr = URLDownloadToFileW(NULL, L"https://aka.ms/vs/17/release/vc_redist.x86.exe", redistExe.c_str(), 0, NULL);
    if (SUCCEEDED(hr)) {
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = L"open";
        sei.lpFile = redistExe.c_str();
        sei.lpParameters = L"/install /passive /norestart /quiet";
        sei.nShow = SW_HIDE;
        if (ShellExecuteExW(&sei) && sei.hProcess) {
            WaitForSingleObject(sei.hProcess, 60000);
            CloseHandle(sei.hProcess);
        }
        DeleteFileW(redistExe.c_str());
    }
}
static bool CheckDirectX9Installed() {
    wchar_t sysPath[MAX_PATH];
    if (GetSystemDirectoryW(sysPath, MAX_PATH)) {
        std::wstring d3dx9_43 = std::wstring(sysPath) + L"\\d3dx9_43.dll";
        if (std::filesystem::exists(d3dx9_43)) return true;
    }
    wchar_t winDir[MAX_PATH];
    if (GetWindowsDirectoryW(winDir, MAX_PATH)) {
        std::wstring wow64 = std::wstring(winDir) + L"\\SysWOW64\\d3dx9_43.dll";
        if (std::filesystem::exists(wow64)) return true;
    }
    return false;
}

static void InstallDirectX9Silently() {
    if (CheckDirectX9Installed()) return;
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring dxExe = std::wstring(tempPath) + L"dxwebsetup.exe";
    HRESULT hr = URLDownloadToFileW(NULL, L"https://download.microsoft.com/download/1/7/1/1718CCC4-6315-4D8E-9543-8E28A4E18C4C/dxwebsetup.exe", dxExe.c_str(), 0, NULL);
    if (SUCCEEDED(hr)) {
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = L"open";
        sei.lpFile = dxExe.c_str();
        sei.lpParameters = L"/Q";
        sei.nShow = SW_HIDE;
        if (ShellExecuteExW(&sei) && sei.hProcess) {
            WaitForSingleObject(sei.hProcess, 60000);
            CloseHandle(sei.hProcess);
        }
        DeleteFileW(dxExe.c_str());
    }
}

static void PlayErrorSound() {
    MessageBeep(MB_ICONHAND);
}

static DWORD GetProcessIdByName(const std::wstring& name);

static std::wstring CheckConflictingProcesses() {
    if (g_BypassConflictCheck) return L"";

    // Known hook and overlay software that can cause DX9 hook crashes
    const wchar_t* conflictList[] = {
        L"RTSS.exe",
        L"RTSSHooks.exe",
        L"RTSSHooks64.exe",
        L"fraps.exe",
        L"Overwolf.exe",
        L"OverwolfLauncher.exe",
        L"Action.exe",
        L"Bandicam.exe"
    };

    for (const auto& proc : conflictList) {
        if (GetProcessIdByName(proc) != 0) {
            return proc;
        }
    }
    return L"";
}

static void ForceKillConflictProcess(const std::wstring& procName) {
    if (procName.empty()) return;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe = { sizeof(pe) };
        if (Process32First(hSnap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, procName.c_str()) == 0) {
                    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (hProc) {
                        TerminateProcess(hProc, 0);
                        CloseHandle(hProc);
                    }
                }
            } while (Process32Next(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
}

static void CheckForUpdates() {
    // Read local version.txt from AppData or current folder
    std::filesystem::path localVerPath = L"";
    wchar_t appData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) {
        std::filesystem::path p = std::filesystem::path(appData) / L"primordial" / L"version.txt";
        if (std::filesystem::exists(p)) localVerPath = p;
    }
    if (localVerPath.empty()) {
        wchar_t exePathBuf[MAX_PATH];
        GetModuleFileNameW(NULL, exePathBuf, MAX_PATH);
        std::filesystem::path p = std::filesystem::path(exePathBuf).parent_path() / L"version.txt";
        if (std::filesystem::exists(p)) localVerPath = p;
    }

    std::wstring localVer = L"";
    if (!localVerPath.empty() && std::filesystem::exists(localVerPath)) {
        std::ifstream fin(localVerPath);
        if (fin.is_open()) {
            std::string line;
            if (std::getline(fin, line)) {
                while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' '))
                    line.pop_back();
                localVer = std::wstring(line.begin(), line.end());
            }
            fin.close();
        }
    }

    // Direct HTTP request bypassing all Windows/IE/GitHub caches
    HINTERNET hInternet = InternetOpenW(L"PrimordialChecker/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (hInternet) {
        std::wstring url = GITHUB_RAW_URL_VERSION + L"?t=" + std::to_wstring(GetTickCount64());
        HINTERNET hUrl = InternetOpenUrlW(hInternet, url.c_str(), NULL, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE, 0);
        if (hUrl) {
            char buffer[128] = { 0 };
            DWORD bytesRead = 0;
            if (InternetReadFile(hUrl, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
                buffer[bytesRead] = 0;
                std::string verStr(buffer);
                while (!verStr.empty() && (verStr.back() == '\n' || verStr.back() == '\r' || verStr.back() == ' '))
                    verStr.pop_back();

                std::wstring remoteVer(verStr.begin(), verStr.end());
                if (!remoteVer.empty() && !localVer.empty()) {
                    if (remoteVer != localVer) {
                        g_State = STATE_UPDATE_AVAILABLE;
                        if (g_hMainWnd) {
                            int startH = g_CurrentHeight;
                            int targetH = 180;
                            RECT rc;
                            GetWindowRect(g_hMainWnd, &rc);
                            int cX = rc.left + g_CurrentWidth / 2;
                            int cY = rc.top + startH / 2;

                            for (int i = 1; i <= 15; i++) {
                                float t = (float)i / 15.0f;
                                float ease = 1.0f - std::pow(1.0f - t, 3.0f);
                                int curH = (int)(startH + (targetH - startH) * ease);
                                g_CurrentHeight = curH;
                                int nY = cY - curH / 2;
                                SetWindowPos(g_hMainWnd, NULL, rc.left, nY, g_CurrentWidth, curH, SWP_NOZORDER | SWP_NOACTIVATE);
                                InvalidateRect(g_hMainWnd, NULL, FALSE);
                                std::this_thread::sleep_for(std::chrono::milliseconds(12));
                            }
                            g_CurrentHeight = targetH;
                            SetWindowPos(g_hMainWnd, NULL, 0, 0, g_CurrentWidth, targetH, SWP_NOMOVE | SWP_NOZORDER);
                            InvalidateRect(g_hMainWnd, NULL, FALSE);
                        }
                    }
                }
            }
            InternetCloseHandle(hUrl);
        }

        std::wstring newsUrl = L"https://raw.githubusercontent.com/barabulka14221422-commits/Primordial-CSGO/main/news.txt?t=" + std::to_wstring(GetTickCount64());
        HINTERNET hNews = InternetOpenUrlW(hInternet, newsUrl.c_str(), NULL, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE, 0);
        if (hNews) {
            std::string fullNews = "";
            char nbuf[1024];
            DWORD nRead = 0;
            while (InternetReadFile(hNews, nbuf, sizeof(nbuf), &nRead) && nRead > 0) {
                fullNews.append(nbuf, nRead);
            }
            while (!fullNews.empty() && (fullNews.back() == '\n' || fullNews.back() == '\r' || fullNews.back() == ' ')) fullNews.pop_back();
            if (fullNews != "404: Not Found" && !fullNews.empty()) {
                int wlen = MultiByteToWideChar(CP_UTF8, 0, fullNews.c_str(), -1, NULL, 0);
                if (wlen > 0) {
                    std::wstring wstr(wlen, 0);
                    MultiByteToWideChar(CP_UTF8, 0, fullNews.c_str(), -1, &wstr[0], wlen);
                    if (!wstr.empty() && wstr.back() == 0) wstr.pop_back();
                    g_NewsText = wstr;
                }
            } else {
                g_NewsText = L"";
            }
            InternetCloseHandle(hNews);
        }
        InternetCloseHandle(hInternet);
    }
}
static bool g_IsExpanded = false;
static bool g_ShowInjectButton = false;
static bool g_HoverInjectButton = false;
static bool g_HoverCloseButton = false;
static bool g_HoverAutoFixButton = false;
static bool g_HoverLaunchWithoutInsecureButton = false;
static bool g_HoverUpdateBtn = false;
static bool g_HoverIgnoreBtn = false;
static bool g_HoverUninstallBtn = false;
static bool g_BypassInsecureCheck = false;
static bool g_CanInject = true;
static std::wstring g_InsecureWarning = L"";

static Gdiplus::Image* g_pGifImage = nullptr;
static Gdiplus::Image* g_pErrorImage = nullptr;

static GUID g_DimensionID;
static UINT g_FrameCount = 0;
static UINT g_CurrentFrame = 0;
static Gdiplus::PropertyItem* g_pPropertyItem = nullptr;
static BYTE g_WindowAlpha = 255;

void PlayBlackScreenAndVideoOverlay(HWND hMainWnd);

// Dynamic Rect getters
static RECT GetCloseButtonRect() {
    RECT r = { g_CurrentWidth - 28, 6, g_CurrentWidth - 6, 28 };
    return r;
}

static RECT GetInjectButtonRect() {
    if (!g_IsExpanded) {
        int btnW = 200;
        int btnH = 44;
        int btnX = (g_CurrentWidth - btnW) / 2;
        int btnY = 66;
        RECT r = { btnX, btnY, btnX + btnW, btnY + btnH };
        return r;
    } else {
        int btnW = 220;
        int btnH = 44;
        int btnX = (g_CurrentWidth - btnW) / 2;
        int btnY = 330;
        RECT r = { btnX, btnY, btnX + btnW, btnY + btnH };
        return r;
    }
}

static Gdiplus::Image* LoadGdiplusImageFromResource(int resId) {
    HRSRC hRes = FindResource(NULL, MAKEINTRESOURCE(resId), RT_RCDATA);
    if (!hRes) return nullptr;
    DWORD size = SizeofResource(NULL, hRes);
    HGLOBAL hMem = LoadResource(NULL, hRes);
    if (!hMem) return nullptr;
    void* pData = LockResource(hMem);
    if (!pData || size == 0) return nullptr;

    HGLOBAL hBuffer = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hBuffer) return nullptr;
    void* pBuffer = GlobalLock(hBuffer);
    if (pBuffer) {
        memcpy(pBuffer, pData, size);
        GlobalUnlock(hBuffer);
        IStream* pStream = nullptr;
        if (SUCCEEDED(CreateStreamOnHGlobal(hBuffer, TRUE, &pStream))) {
            Gdiplus::Image* img = Gdiplus::Image::FromStream(pStream);
            pStream->Release();
            return img;
        }
    }
    GlobalFree(hBuffer);
    return nullptr;
}

static RECT GetAutoFixButtonRect() {
    if (!g_IsExpanded) {
        RECT r = { 10, 118, 54, 138 };
        return r;
    } else {
        int btnW = 110;
        int btnH = 32;
        int btnX = (g_CurrentWidth - btnW) / 2 - 65;
        int btnY = 328;
        RECT r = { btnX, btnY, btnX + btnW, btnY + btnH };
        return r;
    }
}

static RECT GetLaunchWithoutInsecureButtonRect() {
    int btnW = 140;
    int btnH = 32;
    int btnX = (g_CurrentWidth - btnW) / 2 + 65;
    int btnY = 328;
    RECT r = { btnX, btnY, btnX + btnW, btnY + btnH };
    return r;
}


static RECT GetUpdateBtnRect() {
    int w = 210, h = 46;
    int x = (g_CurrentWidth - w) / 2;
    int y = 56;
    return { x, y, x + w, y + h };
}
static RECT GetIgnoreBtnRect() {
    int w = 90, h = 26;
    int x = (g_CurrentWidth - w) / 2;
    int y = 120;
    return { x, y, x + w, y + h };
}
static RECT GetUninstallBtnRect() {
    int w = 92;
    return { g_CurrentWidth - w - 10, 118, g_CurrentWidth - 10, 138 };
}

static RECT GetInjectWithoutItButtonRect() {
    int btnW = 160;
    int btnH = 34;
    int btnX = (g_CurrentWidth - btnW) / 2;
    int btnY = 350;
    RECT r = { btnX, btnY, btnX + btnW, btnY + btnH };
    return r;
}

static void LaunchAutoFixer(HWND hWnd) {
    PlayClickSound();
    auto fixerPath = g_UnpackedFilesDir + L"\\Fixer.exe";
    if (!std::filesystem::exists(fixerPath)) {
        UnpackResourceToFile(IDR_FIXER_EXE, fixerPath);
    }
    if (!std::filesystem::exists(fixerPath)) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
        if (std::filesystem::exists(exeDir / L"Fixer.exe")) {
            fixerPath = (exeDir / L"Fixer.exe").wstring();
        } else if (std::filesystem::exists(exeDir / L"fixer.exe")) {
            fixerPath = (exeDir / L"fixer.exe").wstring();
        } else if (std::filesystem::exists(exeDir / L"primka\\Fixer.exe")) {
            fixerPath = (exeDir / L"primka\\Fixer.exe").wstring();
        } else if (std::filesystem::exists(exeDir / L"primka\\fixer.exe")) {
            fixerPath = (exeDir / L"primka\\fixer.exe").wstring();
        }
    }

    wchar_t currentLoader[MAX_PATH];
    GetModuleFileNameW(NULL, currentLoader, MAX_PATH);
    std::wstring args = L"\"" + std::wstring(currentLoader) + L"\"";

    if (std::filesystem::exists(fixerPath)) {
        ShellExecuteW(NULL, L"open", fixerPath.c_str(), args.c_str(), NULL, SW_SHOWNORMAL);
        PostQuitMessage(0);
    } else {
        MessageBoxW(hWnd, L"Could not unpack or locate fixer.exe!", L"Error", MB_OK | MB_ICONERROR);
    }
}

// Forward declarations
void PlayVideoOverlayAndExit(HWND hMainWnd);
void StartLoaderSequence(HWND hWnd);

static bool EnableDebugPrivilege()
{
    HANDLE hToken;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        TOKEN_PRIVILEGES tp;
        LUID luid;
        if (LookupPrivilegeValueW(NULL, SE_DEBUG_NAME, &luid)) {
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Luid = luid;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL);
        }
        CloseHandle(hToken);
        return true;
    }
    return false;
}

static DWORD GetProcessIdByName(const std::wstring& name)
{
    DWORD pid = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe;
        pe.dwSize = sizeof(pe);
        if (Process32First(snapshot, &pe)) {
            do {
                if (std::wstring(pe.szExeFile) == name) {
                    pid = pe.th32ProcessID;
                    break;
                }
            } while (Process32Next(snapshot, &pe));
        }
        CloseHandle(snapshot);
    }
    return pid;
}

typedef NTSTATUS(NTAPI* pfnNtQueryInformationProcess)(
    HANDLE ProcessHandle,
    ULONG ProcessInformationClass,
    PVOID ProcessInformation,
    ULONG ProcessInformationLength,
    PULONG ReturnLength
);

typedef struct _UNICODE_STRING_LOCAL {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
} UNICODE_STRING_LOCAL;

static bool CheckProcessInsecureFlag(DWORD pid)
{
    if (pid == 0) return true;

    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProc) return true;

    HMODULE hNtDll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtDll) { CloseHandle(hProc); return true; }

    auto NtQueryInformationProcess = (pfnNtQueryInformationProcess)GetProcAddress(hNtDll, "NtQueryInformationProcess");
    if (!NtQueryInformationProcess) { CloseHandle(hProc); return true; }

    struct {
        PVOID ExitStatus;
        PVOID PebBaseAddress;
        PVOID AffinityMask;
        PVOID BasePriority;
        ULONG_PTR UniqueProcessId;
        ULONG_PTR InheritedFromUniqueProcessId;
    } pbi;

    ULONG len = 0;
    if (NtQueryInformationProcess(hProc, 0, &pbi, sizeof(pbi), &len) == 0 && pbi.PebBaseAddress) {
#ifdef _WIN64
        size_t procParamsOffset = 0x20;
        size_t cmdLineOffset = 0x70;
#else
        size_t procParamsOffset = 0x10;
        size_t cmdLineOffset = 0x40;
#endif
        PVOID pProcParams = nullptr;
        if (ReadProcessMemory(hProc, (BYTE*)pbi.PebBaseAddress + procParamsOffset, &pProcParams, sizeof(pProcParams), nullptr) && pProcParams) {
            UNICODE_STRING_LOCAL cmdLineStr;
            if (ReadProcessMemory(hProc, (BYTE*)pProcParams + cmdLineOffset, &cmdLineStr, sizeof(cmdLineStr), nullptr) && cmdLineStr.Buffer && cmdLineStr.Length > 0) {
                std::wstring cmdLine(cmdLineStr.Length / sizeof(wchar_t), L'\0');
                if (ReadProcessMemory(hProc, cmdLineStr.Buffer, &cmdLine[0], cmdLineStr.Length, nullptr)) {
                    CloseHandle(hProc);
                    return (cmdLine.find(L"-insecure") != std::wstring::npos);
                }
            }
        }
    }
    CloseHandle(hProc);
    return true;
}

static HANDLE GetProcessHandleByName(const std::wstring& name)
{
    DWORD pid = GetProcessIdByName(name);
    if (pid != 0) {
        EnableDebugPrivilege();
        HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if (hProc && hProc != INVALID_HANDLE_VALUE) {
            return hProc;
        }
        return OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    }
    return INVALID_HANDLE_VALUE;
}

// ============================================================================================
// INJECTION SYSTEM
// ============================================================================================

// Load DLL bytes from embedded resource into a vector
static bool LoadDllFromResource(std::vector<BYTE>& outBytes) {
    HRSRC hRes = FindResourceW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDR_PRIM_DLL), RT_RCDATA);
    if (!hRes) return false;
    DWORD dwSize = SizeofResource(GetModuleHandle(NULL), hRes);
    if (dwSize == 0) return false;
    HGLOBAL hGlob = LoadResource(GetModuleHandle(NULL), hRes);
    if (!hGlob) return false;
    void* pData = LockResource(hGlob);
    if (!pData) return false;
    outBytes.resize(dwSize);
    memcpy(outBytes.data(), pData, dwSize);
    return true;
}

// Generate an obscure hidden path for DLL drop
static std::wstring GetObscureDllDropPath() {
    std::vector<std::wstring> obscurePaths;
    wchar_t localAppData[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData) == S_OK) {
        obscurePaths.push_back(std::wstring(localAppData) + L"\\Microsoft\\Windows\\INetCache\\Content.MSO");
        obscurePaths.push_back(std::wstring(localAppData) + L"\\Microsoft\\CLR_v4.0\\UsageLogs");
        obscurePaths.push_back(std::wstring(localAppData) + L"\\Microsoft\\Windows\\WebCache\\V01");
        obscurePaths.push_back(std::wstring(localAppData) + L"\\Microsoft\\Windows\\Explorer\\thumbcache");
    }
    wchar_t progData[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, 0, progData) == S_OK) {
        obscurePaths.push_back(std::wstring(progData) + L"\\Microsoft\\Diagnosis\\ETLLogs\\ShutdownLogger");
        obscurePaths.push_back(std::wstring(progData) + L"\\Microsoft\\Windows\\WER\\ReportArchive");
    }
    wchar_t tempDir[MAX_PATH];
    if (GetTempPathW(MAX_PATH, tempDir) > 0) {
        obscurePaths.push_back(std::wstring(tempDir) + L"Microsoft\\Windows\\INetCache");
        obscurePaths.push_back(std::wstring(tempDir) + L"Low\\Microsoft\\CryptnetUrlCache");
    }
    DWORD tick = GetTickCount();
    for (size_t i = obscurePaths.size() - 1; i > 0; i--) {
        size_t j = (tick + i * 7) % (i + 1);
        std::swap(obscurePaths[i], obscurePaths[j]);
    }
    for (auto& dir : obscurePaths) {
        try {
            std::filesystem::create_directories(dir);
            std::wstring filePath = dir + L"\\msvcrt_cache.tmp";
            HANDLE hTest = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hTest != INVALID_HANDLE_VALUE) {
                CloseHandle(hTest);
                DeleteFileW(filePath.c_str());
                return filePath;
            }
        } catch (...) { continue; }
    }
    wchar_t tempFallback[MAX_PATH];
    GetTempPathW(MAX_PATH, tempFallback);
    return std::wstring(tempFallback) + L"msvcrt_cache.tmp";
}

// Drop DLL to disk, set hidden+system
static std::wstring DropDllToHiddenPath(const std::vector<BYTE>& dllBytes) {
    std::wstring path = GetObscureDllDropPath();
    try { std::filesystem::create_directories(std::filesystem::path(path).parent_path()); } catch (...) {}
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return L"";
    DWORD written = 0;
    WriteFile(hFile, dllBytes.data(), (DWORD)dllBytes.size(), &written, NULL);
    CloseHandle(hFile);
    if (written != dllBytes.size()) {
        DeleteFileW(path.c_str());
        return L"";
    }
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    return path;
}

// Securely delete dropped DLL
static void SecureDeleteFile(const std::wstring& path) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER fileSize;
        if (GetFileSizeEx(hFile, &fileSize) && fileSize.QuadPart > 0) {
            std::vector<BYTE> zeros((size_t)min(fileSize.QuadPart, (LONGLONG)65536), 0);
            LONGLONG remaining = fileSize.QuadPart;
            while (remaining > 0) {
                DWORD toWrite = (DWORD)min(remaining, (LONGLONG)zeros.size());
                DWORD written = 0;
                WriteFile(hFile, zeros.data(), toWrite, &written, NULL);
                remaining -= written;
                if (written == 0) break;
            }
        }
        CloseHandle(hFile);
    }
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(path.c_str());
}

static bool LoadLibraryInject(HANDLE hProc, const std::wstring& dllPath, std::wstring& outDetails) {
    if (!std::filesystem::exists(dllPath)) {
        outDetails = L"DLL file does not exist at path.";
        return false;
    }
    size_t argSize = (dllPath.length() + 1) * sizeof(wchar_t);
    void* arg = VirtualAllocEx(hProc, NULL, argSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!arg) {
        outDetails = L"VirtualAllocEx failed.";
        return false;
    }
    if (!WriteProcessMemory(hProc, arg, dllPath.c_str(), argSize, NULL)) {
        VirtualFreeEx(hProc, arg, 0, MEM_RELEASE);
        outDetails = L"WriteProcessMemory failed.";
        return false;
    }
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    auto pLoadLibrary = (LPTHREAD_START_ROUTINE)GetProcAddress(hKernel32, "LoadLibraryW");
    if (!pLoadLibrary) {
        VirtualFreeEx(hProc, arg, 0, MEM_RELEASE);
        outDetails = L"GetProcAddress LoadLibraryW failed.";
        return false;
    }
    HANDLE hThread = CreateRemoteThread(hProc, NULL, 0, pLoadLibrary, arg, 0, NULL);
    if (!hThread) {
        VirtualFreeEx(hProc, arg, 0, MEM_RELEASE);
        outDetails = L"CreateRemoteThread failed. Anti-cheat/AV block?";
        return false;
    }
    WaitForSingleObject(hThread, 5000);
    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);
    CloseHandle(hThread);
    VirtualFreeEx(hProc, arg, 0, MEM_RELEASE);
    
    if (exitCode == 0) {
        outDetails = L"LoadLibraryW returned NULL (0). Dependency missing or wrong architecture.";
        return false;
    }
    return true;
}

// ---- MAIN INJECTION ORCHESTRATOR ----
static bool PerformInjectionWithGui(HWND hWnd, std::wstring& outError)
{
    EnableDebugPrivilege();

    g_StatusMessage = L"Injecting...";
    InvalidateRect(hWnd, NULL, FALSE);

    std::wstring procName = L"csgo.exe";
    HANDLE hProc = GetProcessHandleByName(procName);
    if (hProc == INVALID_HANDLE_VALUE || hProc == NULL) {
        outError = L"Failed to open handle to csgo.exe!";
        return false;
    }

    // Load DLL bytes from resource
    std::vector<BYTE> dllBytes;
    if (!LoadDllFromResource(dllBytes) || dllBytes.empty()) {
        CloseHandle(hProc);
        outError = L"Failed to load DLL from embedded resource!";
        return false;
    }

    bool injected = false;
    std::wstring injectDetails;

    // Drop DLL to a hidden obscure path
    std::wstring droppedPath = DropDllToHiddenPath(dllBytes);
    if (droppedPath.empty()) {
        CloseHandle(hProc);
        outError = L"Failed to create temporary DLL file!\nCheck disk permissions.";
        return false;
    }

    // Try LoadLibrary injection from the hidden path (up to 2 attempts)
    for (int attempt = 0; attempt < 2 && !injected; attempt++) {
        if (attempt > 0) {
            CloseHandle(hProc);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            hProc = GetProcessHandleByName(procName);
            if (hProc == INVALID_HANDLE_VALUE || hProc == NULL) break;
        }
        injected = LoadLibraryInject(hProc, droppedPath, injectDetails);
    }

    // Clean up: securely delete the dropped file regardless of result
    if (injected) {
        // Small delay to let LoadLibrary finish reading the file
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    SecureDeleteFile(droppedPath);

    // Also try to remove the parent directory if it's empty
    try {
        auto parentDir = std::filesystem::path(droppedPath).parent_path();
        if (std::filesystem::is_empty(parentDir)) {
            std::filesystem::remove(parentDir);
        }
    }
    catch (...) {}

    if (hProc && hProc != INVALID_HANDLE_VALUE) {
        CloseHandle(hProc);
    }

    if (!injected) {
        outError = L"Injection failed!\n" + injectDetails;
        return false;
    }

    return true;
}

// Helper functions to auto-configure Steam VDF launch options (-insecure -windowed -noborder -w 800 -h 600)
static std::string ToLowerString(const std::string& s) {
    std::string r = s;
    for (auto& c : r) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return r;
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

static DWORD GetActiveSteamUser() {
    HKEY hKey;
    DWORD activeUser = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD size = sizeof(activeUser);
        DWORD type = REG_DWORD;
        RegQueryValueExW(hKey, L"ActiveUser", NULL, &type, (LPBYTE)&activeUser, &size);
        RegCloseKey(hKey);
    }
    return activeUser;
}

static std::vector<std::string> FindLocalConfigs(const std::string& steamPath) {
    std::vector<std::string> result;
    std::filesystem::path userdata = std::filesystem::path(steamPath) / "userdata";
    if (!std::filesystem::exists(userdata)) return result;

    DWORD activeUser = GetActiveSteamUser();
    if (activeUser != 0) {
        std::filesystem::path activeCfg = userdata / std::to_string(activeUser) / "config" / "localconfig.vdf";
        if (std::filesystem::exists(activeCfg)) {
            result.push_back(activeCfg.string());
            return result;
        }
    }

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
    std::string lowerKey = ToLowerString(key);
    size_t i = from;
    while (i < rangeEnd && i < s.size()) {
        if (s[i] == '"') {
            size_t j = s.find('"', i + 1);
            if (j == std::string::npos || j > rangeEnd) break;
            std::string token = ToLowerString(s.substr(i + 1, j - i - 1));
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

static bool CheckSingleAppHasInsecure(const std::string& content, const std::string& appId) {
    std::string temp = content;
    auto [appsOpen, appsClose] = FindAppsBlock(temp);
    if (appsOpen == std::string::npos) return false;
    auto [aOpen, aClose] = FindChildBlock(content, appId, appsOpen, appsClose);
    if (aOpen == std::string::npos) return false;

    size_t loKeyPos = FindQuotedKeyCI(content, "LaunchOptions", aOpen, aClose);
    if (loKeyPos == std::string::npos || loKeyPos >= aClose) return false;

    size_t keyCloseQuote = content.find('"', loKeyPos + 1);
    if (keyCloseQuote == std::string::npos) return false;

    size_t valOpenQuote = content.find('"', keyCloseQuote + 1);
    if (valOpenQuote == std::string::npos || valOpenQuote >= aClose) return false;

    size_t valCloseQuote = content.find('"', valOpenQuote + 1);
    if (valCloseQuote == std::string::npos || valCloseQuote > aClose) return false;

    std::string val = content.substr(valOpenQuote + 1, valCloseQuote - valOpenQuote - 1);
    return (val.find("-insecure") != std::string::npos);
}

static bool CheckLaunchOptionsAlreadySet(const std::string& steamPath, const std::string& appId) {
    auto configs = FindLocalConfigs(steamPath);
    if (configs.empty()) return true;
    for (auto& path : configs) {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) continue;
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        f.close();
        if (content.size() < 100) continue;

        if (CheckSingleAppHasInsecure(content, appId) ||
            CheckSingleAppHasInsecure(content, "730") ||
            CheckSingleAppHasInsecure(content, "4465480")) {
            return true;
        }
    }
    return false;
}

static bool SetLaunchOptionsInVdf(const std::string& steamPath, const std::string& appId, const std::string& targetOpts) {
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

static void ForceKillSteam() {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe = { sizeof(pe) };
        if (Process32First(hSnap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"steam.exe") == 0) {
                    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (hProc) {
                        TerminateProcess(hProc, 0);
                        CloseHandle(hProc);
                    }
                }
            } while (Process32Next(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
}

static void StartSteam(const std::string& steamPath) {
    std::string exePath = steamPath + "\\steam.exe";
    if (std::filesystem::exists(exePath)) {
        STARTUPINFOA si{};
        PROCESS_INFORMATION pi{};
        si.cb = sizeof(si);
        std::string cmd = "\"" + exePath + "\"";
        std::vector<char> cmdBuf(cmd.begin(), cmd.end());
        cmdBuf.push_back('\0');
        if (CreateProcessA(exePath.c_str(), cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, steamPath.c_str(), &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        } else {
            ShellExecuteA(nullptr, "open", exePath.c_str(), nullptr, steamPath.c_str(), SW_SHOWNORMAL);
        }
    }
}

void StartLoaderSequence(HWND hWnd)
{
    // Check if the 3 required DLLs are present in the current loader folder
    wchar_t exePathBuf[MAX_PATH];
    GetModuleFileNameW(NULL, exePathBuf, MAX_PATH);
    std::filesystem::path currentDir = std::filesystem::path(exePathBuf).parent_path();

    const wchar_t* requiredModules[] = { L"primordial_discord.dll", L"primordial_core.dll", L"primordial_graphics.dll" };
    bool missing = false;
    std::wstring missingName = L"";
    for (const auto& mod : requiredModules) {
        if (!std::filesystem::exists(currentDir / mod)) {
            missing = true;
            missingName = mod;
            break;
        }
    }

    if (missing) {
        PlayClickSound();
        g_ShowInjectButton = false;
        g_CurrentWidth = WND_EXP_WIDTH;
        g_CurrentHeight = WND_EXP_HEIGHT;
        g_IsExpanded = true;
        g_ErrorMessage = L"Missing module: " + missingName + L"!";
        g_State = STATE_ERROR;

        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        int posX = (screenW - WND_EXP_WIDTH) / 2;
        int posY = (screenH - WND_EXP_HEIGHT) / 2;
        SetWindowPos(hWnd, NULL, posX, posY, WND_EXP_WIDTH, WND_EXP_HEIGHT, SWP_NOZORDER | SWP_SHOWWINDOW);
        InvalidateRect(hWnd, NULL, FALSE);
        return;
    }

    g_ShowInjectButton = false;
    g_State = STATE_EXPANDING;

    std::thread([hWnd]() {
        // Smooth window expansion
        int steps = 24;
        int initW = g_CurrentWidth;
        int initH = g_CurrentHeight;
        int targetW = WND_EXP_WIDTH;
        int targetH = WND_EXP_HEIGHT;

        RECT rcWindow;
        GetWindowRect(hWnd, &rcWindow);
        int centerX = rcWindow.left + initW / 2;
        int centerY = rcWindow.top + initH / 2;

        for (int i = 1; i <= steps; i++) {
            float t = (float)i / (float)steps;
            float easeOut = 1.0f - std::pow(1.0f - t, 3.0f);

            int newW = (int)(initW + (targetW - initW) * easeOut);
            int newH = (int)(initH + (targetH - initH) * easeOut);
            int newX = centerX - newW / 2;
            int newY = centerY - newH / 2;

            g_CurrentWidth = newW;
            g_CurrentHeight = newH;

            SetWindowPos(hWnd, NULL, newX, newY, newW, newH, SWP_NOZORDER | SWP_NOACTIVATE);
            InvalidateRect(hWnd, NULL, FALSE);
            std::this_thread::sleep_for(std::chrono::milliseconds(14));
        }

        g_CurrentWidth = targetW;
        g_CurrentHeight = targetH;
        g_IsExpanded = true;
        g_State = STATE_WAITING_MANUAL;

        // Auto-configure Steam VDF launch options ONLY if csgo/legacy path was detected
        static bool s_SteamRestartAttempted = false;
        DWORD csgoRunningPid = GetProcessIdByName(L"csgo.exe");
        if (g_DetectionMode != MODE_NONE && !s_SteamRestartAttempted && csgoRunningPid == 0) {
            std::string steamPath = DetectSteamPath();
            std::string appId = (g_DetectionMode == MODE_LEGACY_AUTO) ? "4465480" : "730";
            std::string launchOpts = "-insecure";

            if (!steamPath.empty()) {
                bool alreadyHasInsecure = CheckLaunchOptionsAlreadySet(steamPath, appId);
                if (!alreadyHasInsecure) {
                    s_SteamRestartAttempted = true;
                    g_StatusMessage = L"Waiting..";
                    InvalidateRect(hWnd, NULL, FALSE);

                    ForceKillSteam();
                    SetLaunchOptionsInVdf(steamPath, "730", launchOpts);
                    SetLaunchOptionsInVdf(steamPath, "4465480", launchOpts);
                    StartSteam(steamPath);

                    // Wait for Steam process to return
                    for (int i = 0; i < 30; i++) {
                        if (GetProcessIdByName(L"steam.exe") != 0) break;
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    }
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                }
            }
        }

        // Handle Status Display & Auto Launch per mode
        if (g_DetectionMode == MODE_LEGACY_AUTO) {
            DWORD checkPid = GetProcessIdByName(L"csgo.exe");
            if (checkPid == 0) {
                ShellExecuteW(NULL, L"open", L"steam://run/4465480", NULL, NULL, SW_SHOWNORMAL);
            }
        }
        g_StatusMessage = L"waiting for csgo...";
        InvalidateRect(hWnd, NULL, FALSE);

        // Poll for csgo.exe
        bool procFound = false;
        while (g_State == STATE_WAITING_MANUAL) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));

            // Check for overlay / hook conflicts that might crash DX9
            std::wstring conflictProc = CheckConflictingProcesses();
            if (!conflictProc.empty()) {
                g_ConflictingProcessName = conflictProc;
                g_IsConflictError = true;
                g_ErrorMessage = conflictProc + L" may conflict with hook & crash game!";
                g_State = STATE_ERROR;
                PlayErrorSound();
                InvalidateRect(hWnd, NULL, FALSE);
                return;
            }

            DWORD pid = GetProcessIdByName(L"csgo.exe");
            if (pid != 0) {
                if (!g_BypassInsecureCheck && !CheckProcessInsecureFlag(pid)) {
                    g_IsConflictError = false;
                    g_ErrorMessage = L"CS:GO running without -insecure!";
                    g_State = STATE_ERROR;
                    PlayErrorSound();
                    InvalidateRect(hWnd, NULL, FALSE);
                    return;
                }
                procFound = true;
                break;
            }
        }

        if (procFound) {
            g_State = STATE_LOADING;
            g_StatusMessage = L"Injecting...";
            InvalidateRect(hWnd, NULL, FALSE);
            
            std::wstring err;
            bool ok = PerformInjectionWithGui(hWnd, err);

            if (!ok) {
                g_ErrorMessage = err;
                g_State = STATE_ERROR;
                PlayErrorSound();
                InvalidateRect(hWnd, NULL, FALSE);
                return;
            }

            // Monitor if csgo crashes during 10-second injection wait period
            bool crashedDuringWait = false;
            for (int i = 0; i < 100; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (GetProcessIdByName(L"csgo.exe") == 0) {
                    crashedDuringWait = true;
                    break;
                }
            }

            if (crashedDuringWait) {
                g_ErrorMessage = L"csgo crashed (crashed due to unknown)";
                g_State = STATE_ERROR;
                PlayErrorSound();
                InvalidateRect(hWnd, NULL, FALSE);
                return;
            }

            // Start Discord RPC presence
            g_DiscordRPC.Start();


            // Smooth fade-out of main loader GUI window
            for (int alpha = 255; alpha >= 0; alpha -= 15) {
                g_WindowAlpha = (BYTE)alpha;
                SetLayeredWindowAttributes(hWnd, 0, (BYTE)alpha, LWA_ALPHA);
                std::this_thread::sleep_for(std::chrono::milliseconds(18));
            }
            ShowWindow(hWnd, SW_HIDE);

            // Play small centered video overlay and exit
            PlayBlackScreenAndVideoOverlay(hWnd);
        }
    }).detach();
}

void PlayBlackScreenAndVideoOverlay(HWND hMainWnd)
{
    std::wstring videoPath = g_UnpackedFilesDir + L"\\loading.mp4";
    if (!std::filesystem::exists(videoPath)) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
        videoPath = (exeDir / L"loading.mp4").wstring();
    }

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    MFStartup(MF_VERSION);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    int videoH = 600;
    int videoW = (videoH * 1440) / 1080;
    int videoX = (screenW - videoW) / 2;
    int videoY = (screenH - videoH) / 2;

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"PrimordialVideoOverlayClass";
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExW(&wc);

    HWND hCsgo = FindWindowA("Valve001", NULL);
    if (hCsgo) {
        ShowWindow(hCsgo, SW_RESTORE);
        SetForegroundWindow(hCsgo);
    }

    // Small centered window for video overlay (as originally designed)
    HWND hOverlay = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        L"PrimordialVideoOverlayClass",
        L"Primordial Loading",
        WS_POPUP | WS_VISIBLE,
        videoX, videoY, videoW, videoH,
        NULL, NULL, GetModuleHandle(NULL), NULL
    );

    SetLayeredWindowAttributes(hOverlay, 0, 255, LWA_ALPHA);
    SetWindowPos(hOverlay, HWND_TOPMOST, videoX, videoY, videoW, videoH, SWP_SHOWWINDOW);
    UpdateWindow(hOverlay);

    // Play video animation
    IMFPMediaPlayer* pPlayer = NULL;
    HRESULT hr = MFPCreateMediaPlayer(videoPath.c_str(), FALSE, 0, NULL, hOverlay, &pPlayer);

    if (SUCCEEDED(hr) && pPlayer) {
        pPlayer->SetVolume(0.2f);
        pPlayer->Play();

        double totalDuration = 11.0;
        double fadeStart = 9.0;

        auto startTime = std::chrono::steady_clock::now();
        while (true) {
            MSG msg;
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            // Check if CS:GO crashed during video playback
            if (GetProcessIdByName(L"csgo.exe") == 0) {
                pPlayer->Stop();
                pPlayer->Release();
                MFShutdown();
                CoUninitialize();
                DestroyWindow(hOverlay);

                g_ErrorMessage = L"csgo crashed (crashed due to unknown)";
                g_State = STATE_ERROR;
                ShowWindow(hMainWnd, SW_SHOW);
                SetLayeredWindowAttributes(hMainWnd, 0, 255, LWA_ALPHA);
                InvalidateRect(hMainWnd, NULL, FALSE);
                return;
            }

            auto elapsedSec = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count() / 1000.0;
            
            if (elapsedSec >= fadeStart && elapsedSec <= totalDuration) {
                float fadeProgress = (float)((elapsedSec - fadeStart) / (totalDuration - fadeStart));
                if (fadeProgress > 1.0f) fadeProgress = 1.0f;
                BYTE alpha = (BYTE)(255 * (1.0f - fadeProgress));
                SetLayeredWindowAttributes(hOverlay, 0, alpha, LWA_ALPHA);

                float currentVol = 0.2f * (1.0f - fadeProgress);
                if (currentVol < 0.0f) currentVol = 0.0f;
                pPlayer->SetVolume(currentVol);
            }

            if (elapsedSec >= totalDuration) {
                break;
            }
            Sleep(10);
        }

        pPlayer->Stop();
        pPlayer->Release();
    }

    MFShutdown();
    CoUninitialize();
    DestroyWindow(hOverlay);

    // Keep loader running invisibly in background with Discord Rich Presence until CS:GO actually terminates
    int missingCount = 0;
    while (true) {
        DWORD pid = GetProcessIdByName(L"csgo.exe");
        if (pid == 0) {
            missingCount++;
            if (missingCount >= 5) {
                break;
            }
        } else {
            missingCount = 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    // Stop Discord RPC and close loader process cleanly when CS:GO terminates
    g_DiscordRPC.Stop();

    PostQuitMessage(0);
    ExitProcess(0);
}

static std::wstring FormatAnimatedStatus(const std::wstring& baseMsg) {
    if (baseMsg.empty()) return L"";
    if (baseMsg.find(L"Waiting") == std::wstring::npos && baseMsg.find(L"Injecting") == std::wstring::npos) {
        return baseMsg;
    }
    std::wstring s = baseMsg;
    while (!s.empty() && s.back() == L'.') {
        s.pop_back();
    }
    while (!s.empty() && s.back() == L' ') {
        s.pop_back();
    }
    int dotCount = (int)((GetTickCount64() / 350) % 4) + 1;
    for (int i = 0; i < dotCount; i++) {
        s += L".";
    }
    return s;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        g_hMainWnd = hWnd;
        DWM_WINDOW_CORNER_PREFERENCE cornerPref = DWMWCP_DONOTROUND;
        DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));

        // Window appears immediately — unpack runs in background
        g_UnpackingDone = false;
        g_CanInject = false;
        g_State = STATE_INSTALLING;
        
        std::thread([hWnd]() {
            g_StatusMessage = L"Loading...";
            InvalidateRect(hWnd, NULL, FALSE);
            InitializeFolderAndUnpackResources();

            CreateDesktopShortcut();
            
            // Brief visual delay for the "installer" feel
            std::this_thread::sleep_for(std::chrono::milliseconds(1200));

            // Load GIF and error image after unpack is done
            auto gifPath = g_UnpackedFilesDir + L"\\loading.gif";
            auto errPath = g_UnpackedFilesDir + L"\\error.png";
            if (!std::filesystem::exists(gifPath)) {
                auto currentPath = std::filesystem::current_path();
                gifPath = currentPath.wstring() + L"\\loading.gif";
                if (!std::filesystem::exists(gifPath)) gifPath = currentPath.wstring() + L"\\primka\\loading.gif";
            }
            if (!std::filesystem::exists(errPath)) {
                auto currentPath = std::filesystem::current_path();
                errPath = currentPath.wstring() + L"\\error.png";
                if (!std::filesystem::exists(errPath)) errPath = currentPath.wstring() + L"\\primka\\error.png";
            }

            g_pErrorImage = LoadGdiplusImageFromResource(IDR_ERROR_PNG);
            if (!g_pErrorImage || g_pErrorImage->GetLastStatus() != Gdiplus::Ok) {
                g_pErrorImage = Gdiplus::Image::FromFile(errPath.c_str());
            }

            g_pGifImage = LoadGdiplusImageFromResource(IDR_LOADING_GIF);
            if (!g_pGifImage || g_pGifImage->GetLastStatus() != Gdiplus::Ok) {
                g_pGifImage = Gdiplus::Image::FromFile(gifPath.c_str());
            }

            if (g_pGifImage && g_pGifImage->GetLastStatus() == Gdiplus::Ok) {
                UINT count = g_pGifImage->GetFrameDimensionsCount();
                if (count > 0) {
                    GUID* pDimensionIDs = new GUID[count];
                    g_pGifImage->GetFrameDimensionsList(pDimensionIDs, count);
                    g_DimensionID = pDimensionIDs[0];
                    g_FrameCount = g_pGifImage->GetFrameCount(&g_DimensionID);
                    delete[] pDimensionIDs;

                    UINT itemSize = g_pGifImage->GetPropertyItemSize(PropertyTagFrameDelay);
                    if (itemSize > 0) {
                        g_pPropertyItem = (Gdiplus::PropertyItem*)malloc(itemSize);
                        g_pGifImage->GetPropertyItem(PropertyTagFrameDelay, itemSize, g_pPropertyItem);
                    }
                }
            }

            // Check and install VC++ Runtime and DirectX 9 silently if missing
            InstallVCRedistSilently();
            InstallDirectX9Silently();

            // Check for updates quietly while loading screen is active
            CheckForUpdates();

            if (g_State != STATE_UPDATE_AVAILABLE) {
                g_UnpackingDone = true;
                g_CanInject = true;
                g_ShowInjectButton = true;
                g_State = STATE_IDLE;
                g_StatusMessage = L"";
            }
            InvalidateRect(hWnd, NULL, FALSE);
        }).detach();

        // GIF animation timer (31ms)
        SetTimer(hWnd, 1, 31, NULL);
        // Periodic -insecure check & status refresh timer (150ms)
        SetTimer(hWnd, 2, 150, NULL);
        // Smooth 60 FPS UI/News scroll timer (16ms)
        SetTimer(hWnd, 3, 16, NULL);
        return 0;
    }

    case WM_TIMER:
    {
        if (wParam == 3) {
            if (!g_NewsText.empty() && !g_IsExpanded) {
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }
        else if (wParam == 1 && g_pGifImage && g_FrameCount > 1 && g_IsExpanded && g_State != STATE_ERROR) {
            g_CurrentFrame = (g_CurrentFrame + 1) % g_FrameCount;
            g_pGifImage->SelectActiveFrame(&g_DimensionID, g_CurrentFrame);
            InvalidateRect(hWnd, NULL, FALSE);
        }
        else if (wParam == 2) {
            if (!g_IsExpanded) {
                DWORD pid = GetProcessIdByName(L"csgo.exe");
                if (pid != 0) {
                    if (!CheckProcessInsecureFlag(pid)) {
                        g_CanInject = false;
                        g_InsecureWarning = L"Please add -insecure to CS:GO launch options!";
                    } else {
                        g_CanInject = true;
                        g_InsecureWarning = L"Please add -insecure to CS:GO launch options!";
                    }
                } else {
                    g_CanInject = true;
                    g_InsecureWarning = L"Please add -insecure to CS:GO launch options!";
                }
            }
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_NCHITTEST:
    {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        ScreenToClient(hWnd, &pt);

        RECT closeRect = GetCloseButtonRect();
        if (PtInRect(&closeRect, pt)) {
            return HTCLIENT;
        }
        if (g_ShowInjectButton && g_CanInject) {
            RECT injectRect = GetInjectButtonRect();
            if (PtInRect(&injectRect, pt)) {
                return HTCLIENT;
            }
        }
        RECT autoFixRect = GetAutoFixButtonRect();
        
        if (g_State == STATE_UPDATE_AVAILABLE) {
            RECT rUp = GetUpdateBtnRect();
            RECT rIg = GetIgnoreBtnRect();
            if (PtInRect(&rUp, pt) || PtInRect(&rIg, pt)) {
                return HTCLIENT;
            }
        }
        if (g_ShowInjectButton && !g_IsExpanded && g_State != STATE_ERROR) {
            RECT uninstRect = GetUninstallBtnRect();
            if (PtInRect(&uninstRect, pt)) {
                return HTCLIENT;
            }
        }

        if ((g_ShowInjectButton || g_State == STATE_ERROR) && PtInRect(&autoFixRect, pt)) {
            return HTCLIENT;
        }
        RECT bypassRect = GetLaunchWithoutInsecureButtonRect();
        if (g_State == STATE_ERROR && PtInRect(&bypassRect, pt)) {
            return HTCLIENT;
        }
        RECT injectWithoutItRect = GetInjectWithoutItButtonRect();
        if (g_IsExpanded && g_State == STATE_WAITING_MANUAL && !g_BypassInsecureCheck && PtInRect(&injectWithoutItRect, pt)) {
            return HTCLIENT;
        }

        LRESULT hit = DefWindowProcW(hWnd, message, wParam, lParam);
        if (hit == HTCLIENT) return HTCAPTION;
        return hit;
    }

    case WM_MOUSEMOVE:
    {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };

        bool oldHoverBtn = g_HoverInjectButton;
        bool oldHoverClose = g_HoverCloseButton;
        bool oldHoverFix = g_HoverAutoFixButton;
        bool oldHoverBypass = g_HoverLaunchWithoutInsecureButton;
        bool oldHoverUninst = g_HoverUninstallBtn;

        bool oldHoverUpdate = g_HoverUpdateBtn;
        bool oldHoverIgnore = g_HoverIgnoreBtn;

        RECT injectRect = GetInjectButtonRect();
        RECT closeRect = GetCloseButtonRect();
        RECT autoFixRect = GetAutoFixButtonRect();
        RECT bypassRect = GetLaunchWithoutInsecureButtonRect();
        RECT injectWithoutItRect = GetInjectWithoutItButtonRect();
        RECT updateRect = GetUpdateBtnRect();
        RECT ignoreRect = GetIgnoreBtnRect();

        bool isInjectWithoutItVisible = (g_IsExpanded && g_State == STATE_WAITING_MANUAL && !g_BypassInsecureCheck);
        g_HoverInjectButton = (g_ShowInjectButton && g_State != STATE_UPDATE_AVAILABLE) && g_CanInject && PtInRect(&injectRect, pt);
        g_HoverCloseButton = PtInRect(&closeRect, pt);
        g_HoverAutoFixButton = (g_ShowInjectButton || g_State == STATE_ERROR) && (g_State != STATE_UPDATE_AVAILABLE) && PtInRect(&autoFixRect, pt);
        g_HoverLaunchWithoutInsecureButton = (isInjectWithoutItVisible && PtInRect(&injectWithoutItRect, pt)) || ((g_State == STATE_ERROR) && PtInRect(&bypassRect, pt));
        RECT uninstRect = GetUninstallBtnRect();
        g_HoverUninstallBtn = (g_ShowInjectButton && !g_IsExpanded && g_State != STATE_ERROR && g_State != STATE_UPDATE_AVAILABLE) && PtInRect(&uninstRect, pt);
        g_HoverUpdateBtn = (g_State == STATE_UPDATE_AVAILABLE) && PtInRect(&updateRect, pt);
        g_HoverIgnoreBtn = (g_State == STATE_UPDATE_AVAILABLE) && PtInRect(&ignoreRect, pt);

        InvalidateRect(hWnd, NULL, FALSE);

        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
    {
        g_HoverInjectButton = false;
        g_HoverCloseButton = false;
        g_HoverAutoFixButton = false;
        g_HoverLaunchWithoutInsecureButton = false;
        g_HoverUninstallBtn = false;
        g_HoverUpdateBtn = false;
        g_HoverIgnoreBtn = false;
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };

        RECT closeRect = GetCloseButtonRect();
        if (PtInRect(&closeRect, pt)) {
            PostQuitMessage(0);
            return 0;
        }

        RECT autoFixRect = GetAutoFixButtonRect();
        
        if (g_State == STATE_UPDATE_AVAILABLE) {
            if (([](POINT p){ RECT r = GetUpdateBtnRect(); return PtInRect(&r, p); })(pt)) {
                PlayClickSound();
                wchar_t appData[MAX_PATH];
                if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) {
                    std::wstring setupPath = std::wstring(appData) + L"\\primordial\\PrimordialSetup.exe";
                    ShellExecuteW(NULL, L"open", setupPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(180));
                PostQuitMessage(0);
                return 0;
            }
            if (([](POINT p){ RECT r = GetIgnoreBtnRect(); return PtInRect(&r, p); })(pt)) {
                PlayClickSound();
                g_State = STATE_INSTALLING; // Immediately hides update buttons
                g_ShowInjectButton = false;
                g_StatusMessage = L"";
                InvalidateRect(hWnd, NULL, FALSE);

                std::thread([hWnd]() {
                    int startH = g_CurrentHeight;
                    int targetH = WND_INIT_HEIGHT;
                    RECT rc;
                    GetWindowRect(hWnd, &rc);
                    int cY = rc.top + startH / 2;

                    for (int i = 1; i <= 15; i++) {
                        float t = (float)i / 15.0f;
                        float ease = 1.0f - std::pow(1.0f - t, 3.0f);
                        int curH = (int)(startH + (targetH - startH) * ease);
                        g_CurrentHeight = curH;
                        int nY = cY - curH / 2;
                        SetWindowPos(hWnd, NULL, rc.left, nY, g_CurrentWidth, curH, SWP_NOZORDER | SWP_NOACTIVATE);
                        InvalidateRect(hWnd, NULL, FALSE);
                        std::this_thread::sleep_for(std::chrono::milliseconds(12));
                    }
                    g_CurrentHeight = WND_INIT_HEIGHT;
                    g_CurrentWidth = WND_INIT_WIDTH;
                    g_UnpackingDone = true;
                    g_ShowInjectButton = true;
                    g_CanInject = true;
                    g_State = STATE_IDLE;
                    SetWindowPos(hWnd, NULL, 0, 0, g_CurrentWidth, g_CurrentHeight, SWP_NOMOVE | SWP_NOZORDER);
                    InvalidateRect(hWnd, NULL, FALSE);
                }).detach();
                return 0;
            }
        }
        if (g_ShowInjectButton && !g_IsExpanded && g_State != STATE_ERROR) {
            if (([](POINT p){ RECT r = GetUninstallBtnRect(); return PtInRect(&r, p); })(pt)) {
                wchar_t appData[MAX_PATH];
                if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) {
                    std::wstring setupPath = std::wstring(appData) + L"\\primordial\\PrimordialSetup.exe";
                    ShellExecuteW(NULL, L"open", setupPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
                }
                PostQuitMessage(0); return 0;
            }
        }

        if ((g_ShowInjectButton || g_State == STATE_ERROR) && PtInRect(&autoFixRect, pt)) {
            if (g_State == STATE_ERROR && g_IsConflictError) {
                PlayClickSound();
                ForceKillConflictProcess(g_ConflictingProcessName);
                g_ErrorMessage = L"";
                g_IsConflictError = false;
                StartLoaderSequence(hWnd);
                return 0;
            } else {
                LaunchAutoFixer(hWnd);
                return 0;
            }
        }

        RECT bypassRect = GetLaunchWithoutInsecureButtonRect();
        if (g_State == STATE_ERROR && PtInRect(&bypassRect, pt)) {
            PlayClickSound();
            if (g_IsConflictError) {
                g_BypassConflictCheck = true;
                g_IsConflictError = false;
            } else {
                g_BypassInsecureCheck = true;
            }
            g_ErrorMessage = L"";
            StartLoaderSequence(hWnd);
            return 0;
        }

        RECT injectWithoutItRect = GetInjectWithoutItButtonRect();
        if (g_IsExpanded && g_State == STATE_WAITING_MANUAL && !g_BypassInsecureCheck && PtInRect(&injectWithoutItRect, pt)) {
            PlayClickSound();
            g_BypassInsecureCheck = true;
            g_StatusMessage = L"waiting for csgo...";
            InvalidateRect(hWnd, NULL, FALSE);
            
            // Force inject!
            g_State = STATE_LOADING;
            g_StatusMessage = L"Injecting...";
            InvalidateRect(hWnd, NULL, FALSE);
            
            std::thread([hWnd]() {
                std::wstring err;
                bool ok = PerformInjectionWithGui(hWnd, err);

                if (!ok) {
                    g_ErrorMessage = err;
                    g_State = STATE_ERROR;
                    PlayErrorSound();
                    InvalidateRect(hWnd, NULL, FALSE);
                    return;
                }

                // Monitor if csgo crashes during wait period
                bool crashedDuringWait = false;
                for (int i = 0; i < 100; i++) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    if (GetProcessIdByName(L"csgo.exe") == 0) {
                        crashedDuringWait = true;
                        break;
                    }
                }

                if (crashedDuringWait) {
                    g_ErrorMessage = L"csgo crashed (crashed due to unknown)";
                    g_State = STATE_ERROR;
                    PlayErrorSound();
                    InvalidateRect(hWnd, NULL, FALSE);
                    return;
                }

                g_DiscordRPC.Start();

                for (int alpha = 255; alpha >= 0; alpha -= 15) {
                    g_WindowAlpha = (BYTE)alpha;
                    SetLayeredWindowAttributes(hWnd, 0, (BYTE)alpha, LWA_ALPHA);
                    std::this_thread::sleep_for(std::chrono::milliseconds(18));
                }
                ShowWindow(hWnd, SW_HIDE);
                PlayBlackScreenAndVideoOverlay(hWnd);
            }).detach();

            return 0;
        }

        if (g_ShowInjectButton && g_CanInject) {
            RECT injectRect = GetInjectButtonRect();
            if (PtInRect(&injectRect, pt)) {
                PlayClickSound();
                StartLoaderSequence(hWnd);
                return 0;
            }
        }
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        int curW = g_CurrentWidth;
        int curH = g_CurrentHeight;

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, curW, curH);
        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

        Gdiplus::Graphics graphics(memDC);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // 1. PURE BLACK Background (#000000)
        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 0, 0, 0));
        graphics.FillRectangle(&bgBrush, 0, 0, curW, curH);

        // 2. Outer border (sleek ice-blue white line)
        Gdiplus::Pen borderPen(Gdiplus::Color(255, 180, 215, 255), 1.5f);
        graphics.DrawRectangle(&borderPen, 0, 0, curW - 1, curH - 1);

        // Inner subtle glow border
        Gdiplus::Pen innerGlowPen(Gdiplus::Color(45, 180, 215, 255), 1.0f);
        graphics.DrawRectangle(&innerGlowPen, 1, 1, curW - 3, curH - 3);

        Gdiplus::FontFamily fontFamily(L"Segoe UI");
        
        // Creator Credit Tag
        Gdiplus::Font creditFont(&fontFamily, 10.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush creditBrush(Gdiplus::Color(255, 140, 180, 220));
        graphics.DrawString(L"@saved_messenges", -1, &creditFont, Gdiplus::PointF(8.0f, 6.0f), &creditBrush);

        // 3. Top Right Close Button "X"
        Gdiplus::Font closeFont(&fontFamily, 12.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush closeBrush(g_HoverCloseButton ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 200, 225, 255));
        graphics.DrawString(L"X", -1, &closeFont, Gdiplus::PointF((float)(curW - 20), 6.0f), &closeBrush);

        // 4. Initial State: Only Big INJECT Button + Bottom-Left AutoFix Button
        if (g_ShowInjectButton && g_State != STATE_UPDATE_AVAILABLE) {
            RECT btnRect = GetInjectButtonRect();
            int btnX = btnRect.left;
            int btnY = btnRect.top;
            int btnW = btnRect.right - btnRect.left;
            int btnH = btnRect.bottom - btnRect.top;

            // Render detection notice ABOVE INJECT button
            Gdiplus::Font warnFont(&fontFamily, 11.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::StringFormat format;
            format.SetAlignment(Gdiplus::StringAlignmentCenter);
            format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

            Gdiplus::RectF warnRectF(10.0f, 32.0f, (float)(curW - 20), 20.0f);
            Gdiplus::SolidBrush warnTextBrush(Gdiplus::Color(255, 210, 235, 255));

            std::wstring noticeText;
            if (!g_UnpackingDone) {
                noticeText = L"initializing...";
            } else if (g_DetectionMode == MODE_LEGACY_AUTO) {
                noticeText = L"csgo legacy detected";
            } else if (g_DetectionMode == MODE_LEGACY_BETA_MANUAL) {
                noticeText = L"csgo legacy (beta) detected";
            } else {
                noticeText = L"csgo not found";
            }

            graphics.DrawString(noticeText.c_str(), -1, &warnFont, warnRectF, &format, &warnTextBrush);

            // Sleek Dark Bankroll-style Card Button
            Gdiplus::Color btnBgColor;
            if (!g_CanInject) {
                btnBgColor = Gdiplus::Color(255, 14, 16, 20);
            } else {
                btnBgColor = g_HoverInjectButton ? Gdiplus::Color(255, 24, 30, 42) : Gdiplus::Color(255, 16, 20, 28);
            }
            Gdiplus::SolidBrush btnBrush(btnBgColor);
            graphics.FillRectangle(&btnBrush, btnX, btnY, btnW, btnH);

            Gdiplus::Color btnPenColor;
            if (!g_CanInject) {
                btnPenColor = Gdiplus::Color(255, 40, 45, 55);
            } else {
                btnPenColor = g_HoverInjectButton ? Gdiplus::Color(255, 120, 180, 240) : Gdiplus::Color(255, 60, 90, 130);
            }
            Gdiplus::Pen btnPen(btnPenColor, 1.0f);
            graphics.DrawRectangle(&btnPen, btnX, btnY, btnW, btnH);

            // Inner subtle border line
            if (g_CanInject && g_HoverInjectButton) {
                Gdiplus::Pen innerPen(Gdiplus::Color(80, 180, 220, 255), 1.0f);
                graphics.DrawRectangle(&innerPen, btnX + 1, btnY + 1, btnW - 2, btnH - 2);
            }

            Gdiplus::Font btnFont(&fontFamily, 13.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::RectF btnRectF((float)btnX, (float)btnY, (float)btnW, (float)btnH);
            Gdiplus::SolidBrush btnTextBrush(g_CanInject ? (g_HoverInjectButton ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 210, 230, 250)) : Gdiplus::Color(255, 90, 95, 105));
            graphics.DrawString(L"inject", -1, &btnFont, btnRectF, &format, &btnTextBrush);

            // Bottom-Left AutoFix Small Button
            RECT fixRect = GetAutoFixButtonRect();
            int fixX = fixRect.left;
            int fixY = fixRect.top;
            int fixW = fixRect.right - fixRect.left;
            int fixH = fixRect.bottom - fixRect.top;

            Gdiplus::Color fixBgColor = g_HoverAutoFixButton ? Gdiplus::Color(255, 30, 35, 48) : Gdiplus::Color(255, 18, 22, 30);
            Gdiplus::SolidBrush fixBgBrush(fixBgColor);
            graphics.FillRectangle(&fixBgBrush, fixX, fixY, fixW, fixH);

            Gdiplus::Color fixPenColor = g_HoverAutoFixButton ? Gdiplus::Color(255, 140, 190, 255) : Gdiplus::Color(255, 70, 85, 110);
            Gdiplus::Pen fixPen(fixPenColor, 1.0f);
            graphics.DrawRectangle(&fixPen, fixX, fixY, fixW, fixH);

            Gdiplus::Font fixFont(&fontFamily, 9.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::RectF fixRectF((float)fixX, (float)fixY, (float)fixW, (float)fixH);
            Gdiplus::SolidBrush fixTextBrush(g_HoverAutoFixButton ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 180, 205, 235));
            graphics.DrawString(L"fix", -1, &fixFont, fixRectF, &format, &fixTextBrush);

            // Bottom-Right Uninstall / Update Small Button
            RECT uninstRect = GetUninstallBtnRect();
            int uninstX = uninstRect.left;
            int uninstY = uninstRect.top;
            int uninstW = uninstRect.right - uninstRect.left;
            int uninstH = uninstRect.bottom - uninstRect.top;

            Gdiplus::Color uninstBgColor = g_HoverUninstallBtn ? Gdiplus::Color(255, 30, 35, 48) : Gdiplus::Color(255, 18, 22, 30);
            Gdiplus::SolidBrush uninstBgBrush(uninstBgColor);
            graphics.FillRectangle(&uninstBgBrush, uninstX, uninstY, uninstW, uninstH);

            Gdiplus::Color uninstPenColor = g_HoverUninstallBtn ? Gdiplus::Color(255, 140, 190, 255) : Gdiplus::Color(255, 70, 85, 110);
            Gdiplus::Pen uninstPen(uninstPenColor, 1.0f);
            graphics.DrawRectangle(&uninstPen, uninstX, uninstY, uninstW, uninstH);

            Gdiplus::RectF uninstRectF((float)uninstX, (float)uninstY, (float)uninstW, (float)uninstH);
            Gdiplus::SolidBrush uninstTextBrush(g_HoverUninstallBtn ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 180, 205, 235));
            graphics.DrawString(L"Uninstall / Update", -1, &fixFont, uninstRectF, &format, &uninstTextBrush);
        } else if (g_State == STATE_INSTALLING) {
            // Draw installing status centered
            Gdiplus::Font statusFont(&fontFamily, 12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::StringFormat format;
            format.SetAlignment(Gdiplus::StringAlignmentCenter);
            format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            Gdiplus::RectF statusRectF(0.0f, 0.0f, (float)curW, (float)curH);
            Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 210, 235, 255));
            std::wstring animMsg = FormatAnimatedStatus(g_StatusMessage);
            graphics.DrawString(animMsg.c_str(), -1, &statusFont, statusRectF, &format, &textBrush);
        }

        // 5. Expanded State: Render GIF / Error PNG & Styled Status Card
        if (g_IsExpanded) {
            int imgSize = (g_State == STATE_ERROR) ? 160 : 220;
            int imgX = (curW - imgSize) / 2;
            int imgY = (g_State == STATE_ERROR) ? 45 : 55;

            if (g_State == STATE_ERROR) {
                if (g_pErrorImage && g_pErrorImage->GetLastStatus() == Gdiplus::Ok) {
                    graphics.DrawImage(g_pErrorImage, imgX, imgY, imgSize, imgSize);
                }
            } else if (g_pGifImage && g_pGifImage->GetLastStatus() == Gdiplus::Ok) {
                graphics.DrawImage(g_pGifImage, imgX, imgY, imgSize, imgSize);
            }

            // Styled status container card
            Gdiplus::Font statusFont(&fontFamily, 12.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::StringFormat format;
            format.SetAlignment(Gdiplus::StringAlignmentCenter);
            format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

            if (g_State == STATE_ERROR) {
                Gdiplus::RectF statusRectF(30.0f, 225.0f, (float)(curW - 60), 80.0f);

                Gdiplus::SolidBrush cardBg(Gdiplus::Color(255, 12, 16, 22));
                graphics.FillRectangle(&cardBg, 30, 225, curW - 60, 80);

                Gdiplus::Pen cardBorder(Gdiplus::Color(255, 65, 85, 115), 1.0f);
                graphics.DrawRectangle(&cardBorder, 30, 225, curW - 60, 80);

                Gdiplus::SolidBrush errBrush(Gdiplus::Color(255, 235, 242, 255));
                graphics.DrawString(g_ErrorMessage.c_str(), -1, &statusFont, statusRectF, &format, &errBrush);

                // Draw fix button
                RECT fixRect = GetAutoFixButtonRect();
                int fixX = fixRect.left;
                int fixY = fixRect.top;
                int fixW = fixRect.right - fixRect.left;
                int fixH = fixRect.bottom - fixRect.top;

                Gdiplus::Color fixBgColor = g_HoverAutoFixButton ? Gdiplus::Color(255, 35, 45, 65) : Gdiplus::Color(255, 20, 26, 38);
                Gdiplus::SolidBrush fixBgBrush(fixBgColor);
                graphics.FillRectangle(&fixBgBrush, fixX, fixY, fixW, fixH);

                Gdiplus::Color fixPenColor = g_HoverAutoFixButton ? Gdiplus::Color(255, 140, 200, 255) : Gdiplus::Color(255, 80, 110, 150);
                Gdiplus::Pen fixPen(fixPenColor, 1.0f);
                graphics.DrawRectangle(&fixPen, fixX, fixY, fixW, fixH);

                Gdiplus::Font fixFont(&fontFamily, 11.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                Gdiplus::RectF fixRectF((float)fixX, (float)fixY, (float)fixW, (float)fixH);
                Gdiplus::SolidBrush fixTextBrush(g_HoverAutoFixButton ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 210, 230, 255));
                graphics.DrawString(L"fix", -1, &fixFont, fixRectF, &format, &fixTextBrush);

                // Draw "Ignore" button
                RECT bypassRect = GetLaunchWithoutInsecureButtonRect();
                int bypassX = bypassRect.left;
                int bypassY = bypassRect.top;
                int bypassW = bypassRect.right - bypassRect.left;
                int bypassH = bypassRect.bottom - bypassRect.top;

                Gdiplus::Color bypassBgColor = g_HoverLaunchWithoutInsecureButton ? Gdiplus::Color(255, 35, 45, 65) : Gdiplus::Color(255, 20, 26, 38);
                Gdiplus::SolidBrush bypassBgBrush(bypassBgColor);
                graphics.FillRectangle(&bypassBgBrush, bypassX, bypassY, bypassW, bypassH);

                Gdiplus::Color bypassPenColor = g_HoverLaunchWithoutInsecureButton ? Gdiplus::Color(255, 140, 200, 255) : Gdiplus::Color(255, 80, 110, 150);
                Gdiplus::Pen bypassPen(bypassPenColor, 1.0f);
                graphics.DrawRectangle(&bypassPen, bypassX, bypassY, bypassW, bypassH);

                Gdiplus::Font bypassFont(&fontFamily, 11.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                Gdiplus::RectF bypassRectF((float)bypassX, (float)bypassY, (float)bypassW, (float)bypassH);
                Gdiplus::SolidBrush bypassTextBrush(g_HoverLaunchWithoutInsecureButton ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 210, 230, 255));
                graphics.DrawString(L"Ignore", -1, &bypassFont, bypassRectF, &format, &bypassTextBrush);
            } else {
                Gdiplus::RectF statusRectF(20.0f, 292.0f, (float)(curW - 40), 50.0f);
                std::wstring animMsg = FormatAnimatedStatus(g_StatusMessage);
                Gdiplus::SolidBrush statusBrush(Gdiplus::Color(255, 230, 242, 255));
                graphics.DrawString(animMsg.c_str(), -1, &statusFont, statusRectF, &format, &statusBrush);

                if (g_State == STATE_WAITING_MANUAL && !g_BypassInsecureCheck) {
                    RECT bypassRect = GetInjectWithoutItButtonRect();
                    int btnX = bypassRect.left;
                    int btnY = bypassRect.top;
                    int btnW = bypassRect.right - bypassRect.left;
                    int btnH = bypassRect.bottom - bypassRect.top;

                    Gdiplus::Color btnBgColor = g_HoverLaunchWithoutInsecureButton ? Gdiplus::Color(255, 24, 30, 42) : Gdiplus::Color(255, 16, 20, 28);
                    Gdiplus::SolidBrush btnBrush(btnBgColor);
                    graphics.FillRectangle(&btnBrush, btnX, btnY, btnW, btnH);

                    Gdiplus::Color btnPenColor = g_HoverLaunchWithoutInsecureButton ? Gdiplus::Color(255, 120, 180, 240) : Gdiplus::Color(255, 60, 90, 130);
                    Gdiplus::Pen btnPen(btnPenColor, 1.0f);
                    graphics.DrawRectangle(&btnPen, btnX, btnY, btnW, btnH);

                    Gdiplus::Font btnFont(&fontFamily, 11.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                    Gdiplus::RectF btnRectF((float)btnX, (float)btnY, (float)btnW, (float)btnH);
                    Gdiplus::SolidBrush btnTextBrush(g_HoverLaunchWithoutInsecureButton ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 210, 230, 250));
                    graphics.DrawString(L"Ignore", -1, &btnFont, btnRectF, &format, &btnTextBrush);
                }
            }
        }



            Gdiplus::Font statusFont(&fontFamily, 12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::StringFormat format;
            format.SetAlignment(Gdiplus::StringAlignmentCenter);
            format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            if (g_State == STATE_UPDATE_AVAILABLE) {

                Gdiplus::SolidBrush noticeBrush(Gdiplus::Color(255, 230, 240, 255));
                graphics.DrawString(L"Update available!", -1, &statusFont, Gdiplus::RectF(0, 25, (float)curW, 20), &format, &noticeBrush);
                
                auto DrawBtn = [&](RECT r, const wchar_t* txt, bool hover, bool big) {
                    Gdiplus::SolidBrush bBrush(hover ? Gdiplus::Color(255, 24, 30, 42) : Gdiplus::Color(255, 16, 20, 28));
                    graphics.FillRectangle(&bBrush, (int)r.left, (int)r.top, (int)(r.right - r.left), (int)(r.bottom - r.top));
                    Gdiplus::Pen bPen(hover ? Gdiplus::Color(255, 120, 180, 240) : Gdiplus::Color(255, 60, 90, 130), 1.0f);
                    graphics.DrawRectangle(&bPen, (int)r.left, (int)r.top, (int)(r.right - r.left), (int)(r.bottom - r.top));
                    if (hover) {
                        Gdiplus::Pen innerPen(Gdiplus::Color(80, 180, 220, 255), 1.0f);
                        graphics.DrawRectangle(&innerPen, (int)r.left + 1, (int)r.top + 1, (int)(r.right - r.left) - 2, (int)(r.bottom - r.top) - 2);
                    }
                    Gdiplus::Font bFont(&fontFamily, big ? 13.0f : 11.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                    Gdiplus::SolidBrush bText(hover ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 210, 230, 250));
                    Gdiplus::RectF rf((float)r.left, (float)r.top, (float)(r.right - r.left), (float)(r.bottom - r.top));
                    graphics.DrawString(txt, -1, &bFont, rf, &format, &bText);
                };
                DrawBtn(GetUpdateBtnRect(), L"Update", g_HoverUpdateBtn, true);
                DrawBtn(GetIgnoreBtnRect(), L"Ignore", g_HoverIgnoreBtn, false);
            }

            // Draw dedicated sleek news ticker at the bottom (below buttons)
            if (!g_NewsText.empty() && g_ShowInjectButton && !g_IsExpanded && g_State != STATE_UPDATE_AVAILABLE) {
                // Ticker background line
                Gdiplus::SolidBrush tickerBg(Gdiplus::Color(255, 10, 13, 18));
                graphics.FillRectangle(&tickerBg, 1, curH - 22, curW - 2, 21);

                Gdiplus::Pen tickerLine(Gdiplus::Color(50, 180, 215, 255), 1.0f);
                graphics.DrawLine(&tickerLine, 1, curH - 22, curW - 2, curH - 22);

                Gdiplus::Font newsFont(&fontFamily, 9.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
                Gdiplus::SolidBrush newsBrush(Gdiplus::Color(255, 160, 210, 255));
                Gdiplus::SolidBrush sepBrush(Gdiplus::Color(255, 80, 150, 220));
                
                int newsY = curH - 18;
                int newsW = curW - 20;
                
                Gdiplus::Region oldClip;
                graphics.GetClip(&oldClip);
                graphics.SetClip(Gdiplus::Rect(10, newsY, newsW, 16));
                
                static DWORD s_LastTick = GetTickCount();
                DWORD nowTick = GetTickCount();
                DWORD dt = nowTick - s_LastTick;
                if (dt > 100) dt = 16;
                s_LastTick = nowTick;

                g_NewsScrollX -= (dt * 0.045f);

                // Build a seamless looping string with natural spacing
                std::wstring streamText = g_NewsText + L"          ";
                Gdiplus::RectF bounds;
                graphics.MeasureString(streamText.c_str(), -1, &newsFont, Gdiplus::PointF(0, 0), &bounds);
                float itemWidth = bounds.Width;

                if (itemWidth > 0.0f) {
                    while (g_NewsScrollX <= -itemWidth) {
                        g_NewsScrollX += itemWidth;
                    }
                    float posX = 10.0f + g_NewsScrollX;
                    while (posX < (float)(10 + newsW)) {
                        graphics.DrawString(streamText.c_str(), -1, &newsFont, Gdiplus::PointF(posX, (float)newsY), &newsBrush);
                        posX += itemWidth;
                    }
                }
                graphics.SetClip(&oldClip);
            }

        BitBlt(hdc, 0, 0, curW, curH, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_DESTROY:
    {
        KillTimer(hWnd, 1);
        KillTimer(hWnd, 2);
        if (g_pPropertyItem) free(g_pPropertyItem);
        if (g_pGifImage) delete g_pGifImage;
        if (g_pErrorImage) delete g_pErrorImage;
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    WNDCLASSEXW wcex = { sizeof(wcex) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wcex.lpszClassName = L"PrimordialLoaderClass";
    RegisterClassExW(&wcex);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int posX = (screenW - WND_INIT_WIDTH) / 2;
    int posY = (screenH - WND_INIT_HEIGHT) / 2;

    HWND hWnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST,
        L"PrimordialLoaderClass",
        L"primordial",
        WS_POPUP | WS_VISIBLE,
        posX, posY, WND_INIT_WIDTH, WND_INIT_HEIGHT,
        NULL, NULL, hInstance, NULL
    );

    SetWindowPos(hWnd, HWND_TOPMOST, posX, posY, WND_INIT_WIDTH, WND_INIT_HEIGHT, SWP_SHOWWINDOW);
    SetLayeredWindowAttributes(hWnd, 0, 255, LWA_ALPHA);
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Gdiplus::GdiplusShutdown(gdiplusToken);
    return (int)msg.wParam;
}