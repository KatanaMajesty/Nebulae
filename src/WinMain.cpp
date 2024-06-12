#include <stdio.h>

#include <filesystem>
#include "common/Log.h"
#include "Nebulae.h"
#include "Win.h"

// TODO: Temp -> to be moved to nebulae
#include "core/GLTFScene.h"
#include "core/GLTFSceneImporter.h"

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

std::filesystem::path GetModuleDirectory()
{
    char rawPath[MAX_PATH];

    // this will always return null-termination character
    if (GetModuleFileNameA(nullptr, rawPath, MAX_PATH) == MAX_PATH &&
        GetLastError() == ERROR_INSUFFICIENT_BUFFER)
    {
        // Handle insufficient buffer
        throw std::runtime_error("Insufficient buffer space for path to be retrieved");
        return {};
    }

    // This std::string constructor will seek for the null-termination character;
    std::filesystem::path path = std::filesystem::path(std::string(rawPath));

    // get directory now
    return path.parent_path();
}

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

    // This code will be moved to Nebulae soon
    static const std::filesystem::path AssetsDir = GetModuleDirectory().parent_path().parent_path().parent_path() / "assets";

    Neb::Nebulae nebulae;
    if (!nebulae.Init(Neb::AppSpec{ .Handle = hwnd, .AssetsDirectory = AssetsDir }))
    {
        NEB_LOG_ERROR("Failed to initialize nebulae\n");
        return -1;
    }

    /*Neb::GLTFSceneImporter& importer = nebulae.GetSceneImporter();
    if (!importer.ImportScenesFromFile(AssetsDir / "DamagedHelmet" / "DamagedHelmet.gltf"))
    {
        NEB_LOG_WARN("Failed to load scenes\n");
        return -1;
    }*/
    
    // we only render first scene currently, dont care about other scenes
    // Neb::GLTFScene* scene = importer.ImportedScenes.front().get();

    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // Tick application here
        nebulae.Render(nullptr);
    }

    UnregisterClass(lpClassName, hInstance);
    return (INT)msg.wParam;
}