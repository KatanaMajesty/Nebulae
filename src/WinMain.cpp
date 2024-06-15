#include <filesystem>

#include "core/Math.h"
#include "common/Log.h"
#include "input/InputManager.h"
#include "Nebulae.h"

#include "Win.h"
#include <windowsx.h> // For GET_X_LPARAM/GET_Y_LPARAM 

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
    Neb::Nebulae& nebulae = Neb::Nebulae::Get();
    switch (uMsg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_SIZE:
        if (nebulae.IsInitialized())
        {
            UINT width = LOWORD(lParam);
            UINT height = HIWORD(lParam);
            nebulae.Resize(width, height);
        }
        break;
    // Input events for InputManager
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP: {
        WORD flags = LOWORD(wParam);
        Neb::EMouseButton button = Neb::eMouseButton_Left;
        Neb::EMouseButtonStates states = (flags & MK_LBUTTON) ? Neb::eMouseButtonState_ClickedOnce : Neb::eMouseButtonState_Released;
        if (states == Neb::eMouseButtonState_ClickedOnce && uMsg == WM_LBUTTONDBLCLK)
            states = Neb::eMouseButtonState_ClickedTwice;

        Neb::InputManager::Get().GetMouse().SetMouseButtonStates(button, states);
    }; break;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK: {
        WORD flags = LOWORD(wParam);
        Neb::EMouseButton button = Neb::eMouseButton_Right;
        Neb::EMouseButtonStates states = (flags & MK_RBUTTON) ? Neb::eMouseButtonState_ClickedOnce : Neb::eMouseButtonState_Released;
        if (states == Neb::eMouseButtonState_ClickedOnce && uMsg == WM_RBUTTONDBLCLK)
            states = Neb::eMouseButtonState_ClickedTwice;

        Neb::InputManager::Get().GetMouse().SetMouseButtonStates(button, states);
    }; break;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK: {
        WORD flags = LOWORD(wParam);
        Neb::EMouseButton button = Neb::eMouseButton_Middle;
        Neb::EMouseButtonStates states = (flags & MK_MBUTTON) ? Neb::eMouseButtonState_ClickedOnce : Neb::eMouseButtonState_Released;
        if (states == Neb::eMouseButtonState_ClickedOnce && uMsg == WM_MBUTTONDBLCLK)
        {
            states = Neb::eMouseButtonState_ClickedTwice;
        }

        Neb::InputManager::Get().GetMouse().SetMouseButtonStates(button, states);
    }; break;
    case WM_MOUSEWHEEL:
    {
        SHORT delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int32_t val = Neb::Signum(delta);

        Neb::InputManager::Get().GetMouse().NotifyWheelScroll(val);
    }; break;
    case WM_MOUSEMOVE:
    {
        INT x = GET_X_LPARAM(lParam);
        INT y = GET_Y_LPARAM(lParam);

        Neb::InputManager::Get().GetMouse().SetCursorHotspot(Neb::MouseCursorHotspot{ .X = x, .Y = y });
    }; break;
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
    wndClass.style = CS_HREDRAW | CS_VREDRAW;
    wndClass.lpfnWndProc = WindowProc;
    wndClass.hInstance = hInstance;
    wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    wndClass.lpszClassName = lpClassName;
    RegisterClass(&wndClass);

    // This code will be moved to Nebulae soon
    static const std::filesystem::path AssetsDir = GetModuleDirectory().parent_path().parent_path().parent_path() / "assets";

    Neb::nri::Manager& nriManager = Neb::nri::Manager::Get();
    nriManager.Init();

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

    Neb::Nebulae& nebulae = Neb::Nebulae::Get();
    Neb::nri::ThrowIfFalse(nebulae.Init(Neb::AppSpec{ .Handle = hwnd, .AssetsDirectory = AssetsDir }));

    Neb::GLTFSceneImporter& importer = nebulae.GetSceneImporter();
    Neb::nri::ThrowIfFalse(importer.ImportScenesFromFile(AssetsDir / "DamagedHelmet" / "DamagedHelmet.gltf"));

    // TODO: This is currently hardcoded as we know that very first scene will be used for rendering, thus we register its callbacks
    Neb::Scene* scene = importer.ImportedScenes.front().get();
    Neb::Mouse& mouse = Neb::InputManager::Get().GetMouse();
    {
        mouse.RegisterCallback<Neb::MouseEvent_Scrolled>(&Neb::Scene::OnMouseScroll, scene);
        mouse.RegisterCallback<Neb::MouseEvent_CursorHotspotChanged>(&Neb::Scene::OnMouseCursorMoved, scene);
        mouse.RegisterCallback<Neb::MouseEvent_ButtonInteraction>(&Neb::Scene::OnMouseButtonInteract, scene);
    }

    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // Tick application here
        nebulae.Render();
    }

    UnregisterClass(lpClassName, hInstance);
    return (INT)msg.wParam;
}