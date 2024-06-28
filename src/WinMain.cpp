#include <filesystem>

#include "core/Math.h"
#include "common/Log.h"
#include "input/InputManager.h"
#include "nri/Device.h"
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

using KeyMappingContainer = std::unordered_map<WORD, Neb::EKeycode>;
static KeyMappingContainer g_keyMapping;

void InitInputMappings()
{
    using namespace Neb;
    KeyMappingContainer& keyMapping = g_keyMapping;

    // F1 - F12 (we do not support F13 - F24)
    keyMapping[VK_F1] = eKeycode_F1;
    keyMapping[VK_F2] = eKeycode_F2;
    keyMapping[VK_F3] = eKeycode_F3;
    keyMapping[VK_F4] = eKeycode_F4;
    keyMapping[VK_F5] = eKeycode_F5;
    keyMapping[VK_F6] = eKeycode_F6;
    keyMapping[VK_F7] = eKeycode_F7;
    keyMapping[VK_F8] = eKeycode_F8;
    keyMapping[VK_F9] = eKeycode_F9;
    keyMapping[VK_F10] = eKeycode_F10;
    keyMapping[VK_F11] = eKeycode_F11;
    keyMapping[VK_F12] = eKeycode_F12;

    // Keyboard nums 1 - 9 and 0
    keyMapping['0'] = eKeycode_0;
    keyMapping['1'] = eKeycode_1;
    keyMapping['2'] = eKeycode_2;
    keyMapping['3'] = eKeycode_3;
    keyMapping['4'] = eKeycode_4;
    keyMapping['5'] = eKeycode_5;
    keyMapping['6'] = eKeycode_6;
    keyMapping['7'] = eKeycode_7;
    keyMapping['8'] = eKeycode_8;
    keyMapping['9'] = eKeycode_9;

    // Keyboard chars 'A' - 'Z'
    keyMapping['A'] = eKeycode_A;
    keyMapping['B'] = eKeycode_B;
    keyMapping['C'] = eKeycode_C;
    keyMapping['D'] = eKeycode_D;
    keyMapping['E'] = eKeycode_E;
    keyMapping['F'] = eKeycode_F;
    keyMapping['G'] = eKeycode_G;
    keyMapping['H'] = eKeycode_H;
    keyMapping['I'] = eKeycode_I;
    keyMapping['J'] = eKeycode_J;
    keyMapping['K'] = eKeycode_K;
    keyMapping['L'] = eKeycode_L;
    keyMapping['M'] = eKeycode_M;
    keyMapping['N'] = eKeycode_N;
    keyMapping['O'] = eKeycode_O;
    keyMapping['P'] = eKeycode_P;
    keyMapping['Q'] = eKeycode_Q;
    keyMapping['R'] = eKeycode_R;
    keyMapping['S'] = eKeycode_S;
    keyMapping['T'] = eKeycode_T;
    keyMapping['U'] = eKeycode_U;
    keyMapping['V'] = eKeycode_V;
    keyMapping['W'] = eKeycode_W;
    keyMapping['X'] = eKeycode_X;
    keyMapping['Y'] = eKeycode_Y;
    keyMapping['Z'] = eKeycode_Z;

    keyMapping[VK_ESCAPE] = eKeycode_Esc;
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
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_KEYDOWN:
    case WM_KEYUP: {
        KeyMappingContainer& keyMapping = g_keyMapping;

        WORD vkCode = LOWORD(wParam);
        WORD keyFlags = HIWORD(lParam);
        BOOL isUp = (keyFlags & KF_UP);

        auto it = keyMapping.find(vkCode);
        if (it == keyMapping.end())
        {
            break;
        }

        Neb::EKeycode keycode = it->second;
        Neb::EKeycodeState nextState = isUp ? Neb::eKeycodeState_Released : Neb::eKeycodeState_Pressed;
        Neb::InputManager::Get().GetKeyboard().SetKeycodeState(keycode, nextState);
    }; break;
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

    Neb::nri::NRIDevice& device = Neb::nri::NRIDevice::Get();
    device.Init();

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

    // Initialize input-related contexts
    InitInputMappings();

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
    Neb::Keyboard& keyboard = Neb::InputManager::Get().GetKeyboard();
    {
        keyboard.RegisterCallback<Neb::KeyboardEvent_KeyInteraction>(&Neb::Nebulae::OnKeyInteraction, &nebulae);
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