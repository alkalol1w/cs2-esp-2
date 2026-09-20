#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include <vector>

// ============================================================================
// VANTA CS2 Modern Menu & Streamproof ESP Overlay
// Decompiled & Disassembled directly from cs2_esp.exe PE Binary
// ============================================================================

// CS2 Client Memory Offsets
namespace Offsets {
    constexpr uintptr_t dwEntityList = 0x2577BE0;
    constexpr uintptr_t dwLocalPlayerPawn = 0x23CCC08;
    constexpr uintptr_t dwViewMatrix = 0x23D21F0;
    
    // Pawn / Entity Offsets
    constexpr uintptr_t m_iHealth = 0x344; // GÜNCELLENDİ: 0x34C yerine 0x344
    constexpr uintptr_t m_iTeamNum = 0x3E7;
    constexpr uintptr_t m_vOldOrigin = 0x13B8;
}

// Global Menu & Render States
struct Settings {
    bool boxESP = true;
    bool teamESP = false;
    bool streamProof = false;
    bool menuVisible = true;
} g_Settings;

DWORD g_ProcessId = 0;
HANDLE g_hProcess = NULL;
uintptr_t g_ClientDllBase = 0;

// Math Structures
struct Vector3 {
    float x, y, z;
};

struct Matrix4x4 {
    float matrix[4][4];
};

// World to Screen Projection Matrix Matrix Transformation
bool WorldToScreen(const Vector3& pos, Vector3& screen, const Matrix4x4& vm, int width, int height) {
    float w = vm.matrix[3][0] * pos.x + vm.matrix[3][1] * pos.y + vm.matrix[3][2] * pos.z + vm.matrix[3][3];
    if (w < 0.01f) return false;

    float invw = 1.0f / w;
    float x = (vm.matrix[0][0] * pos.x + vm.matrix[0][1] * pos.y + vm.matrix[0][2] * pos.z + vm.matrix[0][3]) * invw;
    float y = (vm.matrix[1][0] * pos.x + vm.matrix[1][1] * pos.y + vm.matrix[1][2] * pos.z + vm.matrix[1][3]) * invw;

    screen.x = (width / 2.0f) + (x * width / 2.0f);
    screen.y = (height / 2.0f) - (y * height / 2.0f);
    return true;
}

// Get Process ID by Executable Name
DWORD GetProcessIdByName(const wchar_t* procName) {
    DWORD pid = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, procName) == 0) {
                    pid = pe.th32ProcessID;
                    break;
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    return pid;
}

// Get Module Base Address (e.g. client.dll)
uintptr_t GetModuleBase(DWORD pid, const wchar_t* modName) {
    uintptr_t base = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (hSnap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me;
        me.dwSize = sizeof(me);
        if (Module32FirstW(hSnap, &me)) {
            do {
                if (_wcsicmp(me.szModule, modName) == 0) {
                    base = (uintptr_t)me.modBaseAddr;
                    break;
                }
            } while (Module32NextW(hSnap, &me));
        }
        CloseHandle(hSnap);
    }
    return base;
}

// Window Procedure for Overlay Window (Class Name: VantaModernOverlay)
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

// Render Menu & ESP Graphics using GDI BitBlt/TextOut
void RenderOverlayFrame(HWND hwnd, HDC hdc) {
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    int screenWidth = clientRect.right;
    int screenHeight = clientRect.bottom;

    // Clear Screen (Chroma key RGB 0,0,0 transparent)
    HBRUSH hBackground = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdc, &clientRect, hBackground);
    DeleteObject(hBackground);

    SetBkMode(hdc, TRANSPARENT);

    // Draw On-Screen Menu
    if (g_Settings.menuVisible) {
        SetTextColor(hdc, RGB(255, 0, 0));
        TextOutA(hdc, 20, 20, "VANTA // CS2 MENU", 17);

        SetTextColor(hdc, RGB(255, 255, 255));
        std::string boxText = "Box ESP: " + std::string(g_Settings.boxESP ? "[ON]" : "[OFF]");
        TextOutA(hdc, 20, 45, boxText.c_str(), (int)boxText.length());

        std::string teamText = "Team ESP: " + std::string(g_Settings.teamESP ? "[ON]" : "[OFF]");
        TextOutA(hdc, 20, 65, teamText.c_str(), (int)teamText.length());

        std::string streamText = "Streamproof: " + std::string(g_Settings.streamProof ? "[ON]" : "[OFF]");
        TextOutA(hdc, 20, 85, streamText.c_str(), (int)streamText.length());

        SetTextColor(hdc, RGB(180, 180, 180));
        TextOutA(hdc, 20, 115, "[INSERT] Menuyu Kapatir", 23);
    }

    // Read ViewMatrix and Entity List if CS2 Process Handle is Active
    if (g_hProcess && g_ClientDllBase && g_Settings.boxESP) {
        Matrix4x4 viewMatrix;
        ReadProcessMemory(g_hProcess, (LPCVOID)(g_ClientDllBase + Offsets::dwViewMatrix), &viewMatrix, sizeof(viewMatrix), NULL);

        // Read Local Player Pawn & Team
        uintptr_t localPawn = 0;
        ReadProcessMemory(g_hProcess, (LPCVOID)(g_ClientDllBase + Offsets::dwLocalPlayerPawn), &localPawn, sizeof(localPawn), NULL);

        int localTeam = 0;
        if (localPawn) {
            ReadProcessMemory(g_hProcess, (LPCVOID)(localPawn + Offsets::m_iTeamNum), &localTeam, sizeof(localTeam), NULL);
        }

        // Entity List Loop (First 64 Entities)
        uintptr_t entityList = 0;
        ReadProcessMemory(g_hProcess, (LPCVOID)(g_ClientDllBase + Offsets::dwEntityList), &entityList, sizeof(entityList), NULL);

        if (entityList) {
            HPEN hRedPen = CreatePen(PS_SOLID, 2, RGB(255, 50, 50));
            HPEN hGreenPen = CreatePen(PS_SOLID, 2, RGB(50, 255, 50));
            HGDIOBJ hOldPen = SelectObject(hdc, hRedPen);

            for (int i = 1; i < 64; ++i) {
                uintptr_t listEntry = 0;
                ReadProcessMemory(g_hProcess, (LPCVOID)(entityList + (8 * (i & 0x7FFF) >> 9) + 16), &listEntry, sizeof(listEntry), NULL);
                if (!listEntry) continue;

                uintptr_t playerController = 0;
                ReadProcessMemory(g_hProcess, (LPCVOID)(listEntry + 120 * (i & 0x1FF)), &playerController, sizeof(playerController), NULL);
                if (!playerController) continue;

                uintptr_t playerPawn = 0;
                ReadProcessMemory(g_hProcess, (LPCVOID)(playerController + 0x7FC), &playerPawn, sizeof(playerPawn), NULL);
                if (!playerPawn || playerPawn == localPawn) continue;

                int health = 0;
                ReadProcessMemory(g_hProcess, (LPCVOID)(playerPawn + Offsets::m_iHealth), &health, sizeof(health), NULL);
                if (health <= 0 || health > 100) continue;

                int team = 0;
                ReadProcessMemory(g_hProcess, (LPCVOID)(playerPawn + Offsets::m_iTeamNum), &team, sizeof(team), NULL);

                // Team ESP check
                if (!g_Settings.teamESP && team == localTeam) continue;

                Vector3 pos;
                ReadProcessMemory(g_hProcess, (LPCVOID)(playerPawn + Offsets::m_vOldOrigin), &pos, sizeof(pos), NULL);

                Vector3 screenHead, screenFeet;
                Vector3 headPos = { pos.x, pos.y, pos.z + 72.0f };

                if (WorldToScreen(pos, screenFeet, viewMatrix, screenWidth, screenHeight) &&
                    WorldToScreen(headPos, screenHead, viewMatrix, screenWidth, screenHeight)) {
                    
                    float height = screenFeet.y - screenHead.y;
                    float width = height / 2.0f;

                    SelectObject(hdc, (team == localTeam) ? hGreenPen : hRedPen);

                    // Draw 2D Box
                    int left = (int)(screenHead.x - width / 2.0f);
                    int top = (int)screenHead.y;
                    int right = (int)(screenHead.x + width / 2.0f);
                    int bottom = (int)screenFeet.y;

                    HGDIOBJ hOldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
                    Rectangle(hdc, left, top, right, bottom);
                    SelectObject(hdc, hOldBrush);
                }
            }

            SelectObject(hdc, hOldPen);
            DeleteObject(hRedPen);
            DeleteObject(hGreenPen);
        }
    }
}

int main() {
    std::cout << "[*] VANTA CS2 Modern Menu & Streamproof ESP Baslatiliyor...\n";

    g_ProcessId = GetProcessIdByName(L"cs2.exe");
    if (!g_ProcessId) {
        std::cout << "[!] OpenProcess basarisiz!\n";
    } else {
        g_hProcess = OpenProcess(PROCESS_VM_READ, FALSE, g_ProcessId);
        g_ClientDllBase = GetModuleBase(g_ProcessId, L"client.dll");
        std::cout << "[+] CS2 Baglandi! Client Base: 0x" << std::hex << g_ClientDllBase << std::dec << "\n";
    }

    // Register Overlay Window Class
    WNDCLASSEXA wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "VantaModernOverlay";
    RegisterClassExA(&wc);

    // Create Transparent Topmost Overlay
    HWND hwndOverlay = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED,
        "VantaModernOverlay",
        "Vanta Overlay",
        WS_POPUP,
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        NULL, NULL, wc.hInstance, NULL
    );

    // Make window background key RGB(0,0,0) transparent
    SetLayeredWindowAttributes(hwndOverlay, RGB(0, 0, 0), 0, LWA_COLORKEY);
    ShowWindow(hwndOverlay, SW_SHOW);

    std::cout << "[+] Overlay hazir. Menu acmak/kapatmak icin [INSERT] tusunu kullanin.\n";

    MSG msg;
    bool keyWasDown = false;

    while (true) {
        if (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        // Handle [INSERT] menu toggle key
        SHORT keyState = GetAsyncKeyState(VK_INSERT);
        if ((keyState & 0x8000) && !keyWasDown) {
            g_Settings.menuVisible = !g_Settings.menuVisible;
            keyWasDown = true;
        } else if (!(keyState & 0x8000)) {
            keyWasDown = false;
        }

        // Streamproof Display Affinity Toggle
        if (g_Settings.streamProof) {
            SetWindowDisplayAffinity(hwndOverlay, WDA_EXCLUDEFROMCAPTURE);
        } else {
            SetWindowDisplayAffinity(hwndOverlay, WDA_NONE);
        }

        // Redraw Overlay
        HDC hdc = GetDC(hwndOverlay);
        RenderOverlayFrame(hwndOverlay, hdc);
        ReleaseDC(hwndOverlay, hdc);

        Sleep(16); // ~60 FPS Loop
    }

    if (g_hProcess) CloseHandle(g_hProcess);
    return 0;
}
