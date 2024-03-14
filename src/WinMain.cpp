#include <stdio.h>

#include "Raytracing/RayTracer.h"
#include "Win.h"

struct WIN32Console
{
    WIN32Console()
    {
        AllocConsole();
        freopen_s(&Stream, "CONOUT$", "w", stdout);
    }

    ~WIN32Console()
    {
        FreeConsole();
    }

    FILE* Stream = NULL;
};

LRESULT WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

INT WINAPI WinMain(
    _In_     HINSTANCE hInstance, 
    _In_opt_ HINSTANCE hPrevInstance, 
    _In_     LPSTR lpCmdLine, 
    _In_     INT nShowCmd)
{
    WIN32Console console;

    constexpr const char* lpClassName  = "DXRNebulae";
    constexpr const char* lpWindowName = "DirectX Raytracing Nebulae";

    WNDCLASS wndClass = {};
    wndClass.lpfnWndProc = WindowProc;
    wndClass.hInstance = hInstance;
    wndClass.lpszClassName = lpClassName;
    RegisterClass(&wndClass);

    HWND hwnd = CreateWindowEx(
        0,                              // Optional window styles.
        lpClassName,                    // Window class
        lpWindowName,                   // Window text
        WS_OVERLAPPEDWINDOW,            // Window style

        // Size and position
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,

        NULL,       // Parent window    
        NULL,       // Menu
        hInstance,  // Instance handle
        NULL        // Additional application data
    );

    if (hwnd == NULL)
    {
        return 0;
    }

    ShowWindow(hwnd, nShowCmd);

    // TODO: Tmp here just to check if works
    RayTracer rayTracer(hwnd);

    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // Tick application here
    }

    UnregisterClass(lpClassName, hInstance);
    return (INT)msg.wParam;
}