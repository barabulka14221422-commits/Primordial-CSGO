#include <windows.h>
#include <mfapi.h>
#include <mfplay.h>
#include <filesystem>
#include <thread>
#include <chrono>

#pragma comment(lib, "mfplay.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
    
    std::wstring videoPath = (exeDir / L"loading.mp4").wstring();
    if (!std::filesystem::exists(videoPath)) {
        videoPath = (exeDir.parent_path() / L"loading.mp4").wstring();
    }

    if (!std::filesystem::exists(videoPath)) {
        MessageBoxW(NULL, L"loading.mp4 file not found!", L"Error", MB_ICONERROR);
        return 0;
    }

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    MFStartup(MF_VERSION);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    // Native video aspect ratio is 1440x1080 (4:3)
    int videoH = 600;
    int videoW = (videoH * 1440) / 1080; // 800x600 (Exact 4:3 matching video, NO side bars!)
    int videoX = (screenW - videoW) / 2;
    int videoY = (screenH - videoH) / 2;

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"PrimordialTestMFVideoClass";
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExW(&wc);

    HWND hOverlay = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        L"PrimordialTestMFVideoClass",
        L"Primordial Video",
        WS_POPUP | WS_VISIBLE,
        videoX, videoY, videoW, videoH,
        NULL, NULL, hInstance, NULL
    );

    SetLayeredWindowAttributes(hOverlay, 0, 255, LWA_ALPHA);
    SetWindowPos(hOverlay, HWND_TOPMOST, videoX, videoY, videoW, videoH, SWP_SHOWWINDOW);
    UpdateWindow(hOverlay);

    IMFPMediaPlayer* pPlayer = NULL;
    HRESULT hr = MFPCreateMediaPlayer(videoPath.c_str(), FALSE, 0, NULL, hOverlay, &pPlayer);

    if (SUCCEEDED(hr) && pPlayer) {
        // Set quieter audio volume (20% volume - 2x quieter)
        pPlayer->SetVolume(0.2f);
        pPlayer->Play();

        // Exact video duration is 10.633s
        double totalDuration = 10.75; // allow full video to finish cleanly without cutoff
        double fadeStart = 9.5;      // start smooth fade-out during the last 1.25s

        auto startTime = std::chrono::steady_clock::now();
        while (true) {
            MSG msg;
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            auto elapsedSec = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count() / 1000.0;
            
            if (elapsedSec >= fadeStart && elapsedSec <= totalDuration) {
                float fadeProgress = (float)((elapsedSec - fadeStart) / (totalDuration - fadeStart));
                if (fadeProgress > 1.0f) fadeProgress = 1.0f;
                BYTE alpha = (BYTE)(255 * (1.0f - fadeProgress));
                SetLayeredWindowAttributes(hOverlay, 0, alpha, LWA_ALPHA);
            }

            if (elapsedSec >= totalDuration) {
                break;
            }
            Sleep(10);
        }

        pPlayer->Stop();
        pPlayer->Release();
    } else {
        wchar_t buf[256];
        swprintf_s(buf, L"MFPCreateMediaPlayer failed!\nHRESULT: 0x%08X", (unsigned int)hr);
        MessageBoxW(NULL, buf, L"Media Foundation Error", MB_ICONERROR);
    }

    MFShutdown();
    CoUninitialize();
    DestroyWindow(hOverlay);

    return 0;
}
