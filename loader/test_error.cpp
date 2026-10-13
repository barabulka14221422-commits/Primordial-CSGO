#include <windows.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <string>
#include <filesystem>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

static const int WND_EXP_WIDTH = 400;
static const int WND_EXP_HEIGHT = 420;

static Gdiplus::Image* g_pErrorImage = nullptr;
static std::wstring g_ErrorMessage = L"Error: Failed to allocate memory in target process!\nError code: 8";

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        DWM_WINDOW_CORNER_PREFERENCE cornerPref = DWMWCP_DONOTROUND;
        DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));

        auto currentPath = std::filesystem::current_path();
        auto errPath = currentPath.wstring() + L"\\error.png";
        if (!std::filesystem::exists(errPath)) errPath = currentPath.wstring() + L"\\primka\\error.png";
        g_pErrorImage = Gdiplus::Image::FromFile(errPath.c_str());
        return 0;
    }
    case WM_NCHITTEST:
    {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        ScreenToClient(hWnd, &pt);
        if (pt.x >= WND_EXP_WIDTH - 28 && pt.x <= WND_EXP_WIDTH - 6 && pt.y >= 6 && pt.y <= 28) {
            return HTCLIENT;
        }
        LRESULT hit = DefWindowProcW(hWnd, message, wParam, lParam);
        if (hit == HTCLIENT) return HTCAPTION;
        return hit;
    }
    case WM_LBUTTONDOWN:
    {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        if (pt.x >= WND_EXP_WIDTH - 28 && pt.x <= WND_EXP_WIDTH - 6 && pt.y >= 6 && pt.y <= 28) {
            PostQuitMessage(0);
        }
        return 0;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, WND_EXP_WIDTH, WND_EXP_HEIGHT);
        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

        Gdiplus::Graphics graphics(memDC);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // Black background
        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 0, 0, 0));
        graphics.FillRectangle(&bgBrush, 0, 0, WND_EXP_WIDTH, WND_EXP_HEIGHT);

        // Sleek ice-blue white border
        Gdiplus::Pen borderPen(Gdiplus::Color(255, 180, 215, 255), 1.5f);
        graphics.DrawRectangle(&borderPen, 0, 0, WND_EXP_WIDTH - 1, WND_EXP_HEIGHT - 1);

        Gdiplus::FontFamily fontFamily(L"Segoe UI");
        Gdiplus::Font closeFont(&fontFamily, 12.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush closeBrush(Gdiplus::Color(255, 220, 240, 255));
        graphics.DrawString(L"X", -1, &closeFont, Gdiplus::PointF((float)(WND_EXP_WIDTH - 20), 6.0f), &closeBrush);

        int imgSize = 220;
        int imgX = (WND_EXP_WIDTH - imgSize) / 2;
        int imgY = 55;
        if (g_pErrorImage && g_pErrorImage->GetLastStatus() == Gdiplus::Ok) {
            graphics.DrawImage(g_pErrorImage, imgX, imgY, imgSize, imgSize);
        }

        // Status Card
        Gdiplus::Font statusFont(&fontFamily, 13.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::StringFormat format;
        format.SetAlignment(Gdiplus::StringAlignmentCenter);
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

        Gdiplus::RectF statusRectF(20.0f, 305.0f, (float)(WND_EXP_WIDTH - 40), 80.0f);
        Gdiplus::SolidBrush cardBg(Gdiplus::Color(255, 12, 16, 22));
        graphics.FillRectangle(&cardBg, 25, 305, WND_EXP_WIDTH - 50, 80);

        Gdiplus::Pen cardBorder(Gdiplus::Color(255, 55, 75, 105), 1.0f);
        graphics.DrawRectangle(&cardBorder, 25, 305, WND_EXP_WIDTH - 50, 80);

        Gdiplus::SolidBrush errBrush(Gdiplus::Color(255, 230, 242, 255));
        graphics.DrawString(g_ErrorMessage.c_str(), -1, &statusFont, statusRectF, &format, &errBrush);

        BitBlt(hdc, 0, 0, WND_EXP_WIDTH, WND_EXP_HEIGHT, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        if (g_pErrorImage) delete g_pErrorImage;
        PostQuitMessage(0);
        return 0;
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
    wcex.lpszClassName = L"PrimordialTestErrorClass";
    RegisterClassExW(&wcex);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int posX = (screenW - WND_EXP_WIDTH) / 2;
    int posY = (screenH - WND_EXP_HEIGHT) / 2;

    HWND hWnd = CreateWindowExW(
        WS_EX_LAYERED,
        L"PrimordialTestErrorClass",
        L"primordial error test",
        WS_POPUP | WS_VISIBLE,
        posX, posY, WND_EXP_WIDTH, WND_EXP_HEIGHT,
        NULL, NULL, hInstance, NULL
    );

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
