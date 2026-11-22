#include "UserInterface.hpp"
#include "core/TrainerManager.hpp"
#include <d3d11.h>
#include <wrl/client.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
namespace trainer {
namespace {
using Microsoft::WRL::ComPtr;
struct Graphics {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swap;
    ComPtr<ID3D11RenderTargetView> target;
    bool Target() {
        ComPtr<ID3D11Texture2D> buffer;
        return SUCCEEDED(swap->GetBuffer(0, IID_PPV_ARGS(&buffer))) &&
            SUCCEEDED(device->CreateRenderTargetView(buffer.Get(), nullptr, &target));
    }
    bool Create(HWND window) {
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferCount = 2; desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.OutputWindow = window;
        desc.SampleDesc.Count = 1; desc.Windowed = TRUE; desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
        HRESULT result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            levels, 2, D3D11_SDK_VERSION, &desc, &swap, &device, nullptr, &context);
        if (FAILED(result)) result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            levels, 2, D3D11_SDK_VERSION, &desc, &swap, &device, nullptr, &context);
        return SUCCEEDED(result) && Target();
    }
};
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam)) return TRUE;
    if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    if (message == WM_GETMINMAXINFO) {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lparam); limits->ptMinTrackSize = {850, 580}; return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
struct Window {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    HWND handle{};
    Window() {
        WNDCLASSEXW wc{sizeof(WNDCLASSEXW)};
        wc.lpfnWndProc = WindowProc; wc.hInstance = instance; wc.lpszClassName = L"SlutopiaTrainerWindow";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        if (!RegisterClassExW(&wc)) throw std::runtime_error(WinError("Window class registration failed"));
        handle = CreateWindowExW(0, wc.lpszClassName, L"SLUTOPIA TRAINER", WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 1100, 760, nullptr, nullptr, instance, nullptr);
        if (!handle) { UnregisterClassW(wc.lpszClassName, instance); throw std::runtime_error(WinError("Window creation failed")); }
    }
    ~Window() { if (IsWindow(handle)) DestroyWindow(handle); UnregisterClassW(L"SlutopiaTrainerWindow", instance); }
};
struct ImGuiSession {
    bool win = false, dx = false;
    ImGuiSession() { IMGUI_CHECKVERSION(); ImGui::CreateContext(); }
    ~ImGuiSession() { if (dx) ImGui_ImplDX11_Shutdown(); if (win) ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext(); }
};
void Theme() {
    ImGui::StyleColorsDark();
    auto& s = ImGui::GetStyle();
    s.WindowRounding = 12; s.ChildRounding = 12; s.FrameRounding = 7; s.GrabRounding = 7;
    s.WindowPadding = {24, 22}; s.FramePadding = {12, 9}; s.ItemSpacing = {12, 14};
    s.Colors[ImGuiCol_WindowBg] = {0.045f,0.055f,0.08f,1};
    s.Colors[ImGuiCol_ChildBg] = {0.075f,0.09f,0.125f,1};
    s.Colors[ImGuiCol_FrameBg] = {0.12f,0.14f,0.19f,1};
    s.Colors[ImGuiCol_Button] = {0.20f,0.25f,0.40f,1};
    s.Colors[ImGuiCol_ButtonHovered] = {0.32f,0.39f,0.61f,1};
    s.Colors[ImGuiCol_ButtonActive] = {0.40f,0.46f,0.74f,1};
    s.Colors[ImGuiCol_Header] = {0.24f,0.29f,0.48f,1};
    s.Colors[ImGuiCol_CheckMark] = {0.65f,0.71f,1,1};
}
bool Toggle(const char* label, bool& value, bool reduced) {
    ImGui::PushID(label);
    const bool clicked = ImGui::InvisibleButton("switch", {42,24});
    if (clicked) value = !value;
    const ImVec2 p = ImGui::GetItemRectMin();
    auto* storage = ImGui::GetStateStorage();
    const auto id = ImGui::GetID("animation");
    float t = storage->GetFloat(id, value ? 1.f : 0.f);
    const float goal = value ? 1.f : 0.f;
    t = reduced ? goal : t + (goal - t) * (1.f - std::exp(-ImGui::GetIO().DeltaTime * 18.f));
    storage->SetFloat(id,t);
    ImGui::GetWindowDrawList()->AddRectFilled(p, {p.x+42,p.y+24},
        ImGui::GetColorU32(value ? ImVec4{0.43f,0.49f,0.86f,1} : ImVec4{0.23f,0.26f,0.33f,1}),12);
    ImGui::GetWindowDrawList()->AddCircleFilled({p.x+12+18*t,p.y+12},8,IM_COL32(240,243,255,255));
    ImGui::SameLine(); ImGui::TextUnformatted(label); ImGui::PopID(); return clicked;
}
void DisabledFeatures(const char* category) {
    ImGui::TextDisabled("Game data requires validation against your installed build.");
    for (const auto& f : FeatureManager::Unsupported()) {
        if (f.category != category) continue;
        ImGui::PushID(f.name.data());
        ImGui::BeginChild("feature", {0,110}, ImGuiChildFlags_Borders);
        ImGui::BeginDisabled(); bool enabled = false; Toggle(f.name.data(), enabled, true); ImGui::EndDisabled();
        ImGui::TextWrapped("%s", f.reason.data());
        ImGui::EndChild(); ImGui::PopID();
    }
}
void Diagnostics(TrainerManager& app, const ProcessStatus& status) {
    ImGui::Text("PID: %lu    Architecture: %s", static_cast<unsigned long>(status.process.pid), status.architecture.c_str());
    ImGui::TextWrapped("Image: %s", Utf8(status.image).c_str());
    ImGui::TextWrapped("Engine evidence: %s", status.engine.c_str());
    ImGui::TextDisabled("Module snapshot at connection; Refresh updates it. F5: refresh, F6: connect/disconnect.");
    if (ImGui::Button("Refresh diagnostics")) app.processes.Refresh();
    if (ImGui::BeginTable("modules", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, {0,170})) {
        ImGui::TableSetupColumn("Module"); ImGui::TableSetupColumn("Base"); ImGui::TableSetupColumn("Bytes"); ImGui::TableHeadersRow();
        for (const auto& m : status.modules) {
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(Utf8(m.name).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Utf8(m.path).c_str());
            ImGui::TableNextColumn(); ImGui::Text("0x%llX", static_cast<unsigned long long>(m.base));
            ImGui::TableNextColumn(); ImGui::Text("%lu", static_cast<unsigned long>(m.size));
        }
        ImGui::EndTable();
    }
    ImGui::TextUnformatted("Diagnostic log (UTC epoch seconds)");
    ImGui::BeginChild("log", {0,170}, ImGuiChildFlags_Borders);
    for (const auto& entry : app.logger.Entries()) ImGui::TextWrapped("%s",entry.c_str());
    ImGui::EndChild();
}
}
int RunUserInterface(TrainerManager& app, bool smoke) {
    ImGui_ImplWin32_EnableDpiAwareness();
    Window window;
    Graphics graphics;
    if (!graphics.Create(window.handle)) throw std::runtime_error("DirectX 11 initialization failed (hardware and WARP)");
    ImGuiSession session;
    ImGui::GetIO().IniFilename = nullptr;
    Theme();
    session.win = ImGui_ImplWin32_Init(window.handle);
    session.dx = ImGui_ImplDX11_Init(graphics.device.Get(), graphics.context.Get());
    if (!session.win || !session.dx) throw std::runtime_error("ImGui backend initialization failed");
    ShowWindow(window.handle, SW_SHOWDEFAULT); UpdateWindow(window.handle);
    std::array<char, 256> executable{};
    std::snprintf(executable.data(), executable.size(), "%s",app.settings.executable.c_str());
    int tab = 0, frames = 0; bool running = true;
    const char* tabs[]{"RESOURCES", "PROGRESSION", "MISC", "SETTINGS"};
    while (running) {
        MSG message{};
        while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
            if (message.message == WM_QUIT) running = false;
        }
        if (!running) break;
        if (IsIconic(window.handle)) { MsgWaitForMultipleObjects(0,nullptr,FALSE,50,QS_ALLINPUT); continue; }
        RECT rect{}; GetClientRect(window.handle,&rect);
        DXGI_SWAP_CHAIN_DESC desc{};
        if (FAILED(graphics.swap->GetDesc(&desc))) throw std::runtime_error("Swap chain description failed");
        if (rect.right > 0 && rect.bottom > 0 && (desc.BufferDesc.Width != static_cast<UINT>(rect.right) || desc.BufferDesc.Height != static_cast<UINT>(rect.bottom))) {
            graphics.context->OMSetRenderTargets(0,nullptr,nullptr); graphics.target.Reset();
            if (FAILED(graphics.swap->ResizeBuffers(0,rect.right,rect.bottom,DXGI_FORMAT_UNKNOWN,0)) || !graphics.Target())
                throw std::runtime_error("Swap chain resize failed");
        }
        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
        auto status = app.processes.Status();
        if (app.settings.hotkeys && !ImGui::GetIO().WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_F5,false)) app.processes.Refresh();
            if (ImGui::IsKeyPressed(ImGuiKey_F6,false)) { if (status.connected) app.Disconnect(); else app.Connect(); }
        }
        ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("SLUTOPIA TRAINER", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextUnformatted("SLUTOPIA / TRAINER");
        ImGui::SameLine(ImGui::GetWindowWidth()-270);
        ImGui::TextColored(status.connected ? ImVec4{0.4f,0.9f,0.7f,1} : ImVec4{0.95f,0.7f,0.4f,1},
            "%s", status.connected ? "CONNECTED / DIAGNOSTICS" : "DISCONNECTED");
        ImGui::TextDisabled("Local state workspace   /   v0.2"); ImGui::Separator();
        ImGui::BeginChild("navigation", {185,0});
        for (int i=0;i<4;++i) if (ImGui::Selectable(tabs[i], tab==i, 0, {0,46})) tab=i;
        ImGui::Spacing(); ImGui::TextWrapped("Game features await local verification.");
        ImGui::TextDisabled("External application\nQuery-only connection");
        ImGui::EndChild(); ImGui::SameLine();
        ImGui::BeginChild("content", {0,0});
        ImGui::TextUnformatted(tabs[tab]); ImGui::TextWrapped("%s",status.state.c_str());
        if (!status.error.empty()) ImGui::TextWrapped("Diagnostic error: %s",status.error.c_str());
        if (tab < 2) DisabledFeatures(tabs[tab]);
        if (tab == 2) Diagnostics(app,status);
        if (tab == 3) {
            ImGui::TextWrapped("Enter the exact filename from Task Manager. A filename match does not validate the game build.");
            ImGui::SetNextItemWidth(-1); ImGui::InputText("##executable",executable.data(),executable.size());
            Toggle("Auto-connect on startup",app.settings.autoConnect,app.settings.reducedMotion);
            Toggle("Keyboard shortcuts (focused window)",app.settings.hotkeys,app.settings.reducedMotion);
            Toggle("Reduced motion",app.settings.reducedMotion,app.settings.reducedMotion);
            if (ImGui::Button("Save settings")) { app.settings.executable = executable.data(); app.Save(); }
            ImGui::SameLine();
            if (ImGui::Button(status.connected ? "Disconnect" : "Connect")) {
                if (status.connected) app.Disconnect();
                else { app.settings.executable = executable.data(); app.Connect(); }
            }
            ImGui::TextWrapped("%s",app.notice.c_str());
            ImGui::TextWrapped("JSON: %s", Utf8(app.config.Path().wstring()).c_str());
            ImGui::TextWrapped("F5 refreshes diagnostics. F6 connects or disconnects. No global keyboard hooks are installed.");
        }
        ImGui::EndChild(); ImGui::End(); ImGui::Render();
        const float clear[]{0.045f,0.055f,0.08f,1};
        auto* target = graphics.target.Get(); graphics.context->OMSetRenderTargets(1,&target,nullptr);
        graphics.context->ClearRenderTargetView(target,clear); ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        const HRESULT present = graphics.swap->Present(1,0);
        if (FAILED(present)) throw std::runtime_error("DirectX presentation failed (device removed or reset)");
        if (present == DXGI_STATUS_OCCLUDED) MsgWaitForMultipleObjects(0,nullptr,FALSE,50,QS_ALLINPUT);
        if (smoke && ++frames >= 45) running = false;
    }
    app.logger.Write("UI closed cleanly");
    return 0;
}
}
