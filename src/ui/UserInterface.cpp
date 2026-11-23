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
#include <unordered_map>

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
        auto* limits = reinterpret_cast<MINMAXINFO*>(lparam); limits->ptMinTrackSize = {880, 600}; return 0;
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
            CW_USEDEFAULT, CW_USEDEFAULT, 1120, 780, nullptr, nullptr, instance, nullptr);
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
    s.Colors[ImGuiCol_WindowBg] = {0.045f, 0.055f, 0.08f, 1};
    s.Colors[ImGuiCol_ChildBg] = {0.075f, 0.09f, 0.125f, 1};
    s.Colors[ImGuiCol_FrameBg] = {0.12f, 0.14f, 0.19f, 1};
    s.Colors[ImGuiCol_Button] = {0.20f, 0.25f, 0.40f, 1};
    s.Colors[ImGuiCol_ButtonHovered] = {0.32f, 0.39f, 0.61f, 1};
    s.Colors[ImGuiCol_ButtonActive] = {0.40f, 0.46f, 0.74f, 1};
    s.Colors[ImGuiCol_Header] = {0.24f, 0.29f, 0.48f, 1};
    s.Colors[ImGuiCol_CheckMark] = {0.65f, 0.71f, 1, 1};
}

bool Toggle(const char* label, bool& value, bool reduced) {
    ImGui::PushID(label);
    const bool clicked = ImGui::InvisibleButton("switch", {42, 24});
    if (clicked) value = !value;
    const ImVec2 p = ImGui::GetItemRectMin();
    auto* storage = ImGui::GetStateStorage();
    const auto id = ImGui::GetID("animation");
    float t = storage->GetFloat(id, value ? 1.f : 0.f);
    const float goal = value ? 1.f : 0.f;
    t = reduced ? goal : t + (goal - t) * (1.f - std::exp(-ImGui::GetIO().DeltaTime * 18.f));
    storage->SetFloat(id, t);
    ImGui::GetWindowDrawList()->AddRectFilled(p, {p.x + 42, p.y + 24},
        ImGui::GetColorU32(value ? ImVec4{0.43f, 0.49f, 0.86f, 1} : ImVec4{0.23f, 0.26f, 0.33f, 1}), 12);
    ImGui::GetWindowDrawList()->AddCircleFilled({p.x + 12 + 18 * t, p.y + 12}, 8, IM_COL32(240, 243, 255, 255));
    ImGui::SameLine(); ImGui::TextUnformatted(label); ImGui::PopID(); return clicked;
}

void DisabledFeatures(const char* category) {
    ImGui::TextDisabled("Unverified features are kept disabled to prevent state desynchronization:");
    for (const auto& f : FeatureManager::Unsupported()) {
        if (f.category != category) continue;
        ImGui::PushID(f.name.data());
        ImGui::BeginChild(f.name.data(), {0, 90}, ImGuiChildFlags_Borders);
        ImGui::BeginDisabled(); bool enabled = false; Toggle(f.name.data(), enabled, true); ImGui::EndDisabled();
        ImGui::TextWrapped("%s", f.reason.data());
        ImGui::EndChild(); ImGui::PopID();
    }
}

void ResourcesView(TrainerManager& app) {
    if (app.saves.IsValid()) {
        const auto& info = app.saves.Info();
        ImGui::BeginChild("save_meta", {0, 100}, ImGuiChildFlags_Borders);
        ImGui::Text("Active Save Player ID: %s", info.playerId.c_str());
        ImGui::Text("File Size: %llu bytes    Last Modified (UTC): %s",
            static_cast<unsigned long long>(info.fileSize), info.lastModifiedUtc.c_str());
        ImGui::Text("Backup (.bak): %s", info.hasBackup ? "Present (Restorable)" : "None created yet");
        if (ImGui::Button("Reload Save File")) {
            app.RefreshSave();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!info.hasBackup);
        if (ImGui::Button("Restore Backup (.bak)")) {
            std::string err;
            if (app.saves.RestoreBackup(err)) {
                app.notice = "Backup successfully restored!";
                app.logger.Write(app.notice);
            } else {
                app.notice = "Restore failed: " + err;
                app.logger.Write(app.notice);
            }
        }
        ImGui::EndDisabled();
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::TextColored({0.4f, 0.9f, 0.7f, 1}, "Confirmed Local Currencies (SavedData / Memento)");
        ImGui::TextDisabled("Changes are applied to the local save file. Restart game or trigger save reload to see changes in-game.");

        static std::unordered_map<std::string, int> editValues;

        for (const auto& p : info.parameters) {
            if (p.category != "RESOURCES") continue;
            ImGui::PushID(p.id.c_str());
            ImGui::BeginChild("currency_row", {0, 110}, ImGuiChildFlags_Borders);
            ImGui::Text("%s", p.displayName.c_str());
            ImGui::SameLine(ImGui::GetWindowWidth() - 220);
            ImGui::TextColored({0.6f, 0.8f, 1.f, 1}, "Current: %d", p.value);

            if (editValues.find(p.id) == editValues.end()) {
                editValues[p.id] = p.value;
            }

            ImGui::SetNextItemWidth(160);
            ImGui::InputInt("##val", &editValues[p.id]);
            ImGui::SameLine();
            if (ImGui::Button("Set")) {
                std::string err;
                if (app.saves.ModifyParameter(p.id, editValues[p.id], err)) {
                    app.notice = "Updated " + p.displayName + " to " + std::to_string(editValues[p.id]);
                    app.logger.Write(app.notice);
                } else {
                    app.notice = "Failed to update " + p.displayName + ": " + err;
                    app.logger.Write(app.notice);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("+100")) {
                std::string err;
                editValues[p.id] = p.value + 100;
                app.saves.ModifyParameter(p.id, editValues[p.id], err);
            }
            ImGui::SameLine();
            if (ImGui::Button("+1,000")) {
                std::string err;
                editValues[p.id] = p.value + 1000;
                app.saves.ModifyParameter(p.id, editValues[p.id], err);
            }
            ImGui::SameLine();
            if (ImGui::Button("+10,000")) {
                std::string err;
                editValues[p.id] = p.value + 10000;
                app.saves.ModifyParameter(p.id, editValues[p.id], err);
            }

            ImGui::TextDisabled("Key: %s | Note: %s", p.id.c_str(), p.note.c_str());
            ImGui::EndChild();
            ImGui::PopID();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored({0.95f, 0.7f, 0.4f, 1}, "Architecture Boundary Notice: Energy vs Currencies");
        ImGui::TextWrapped("Reverse engineering of Assembly-CSharp confirms that Energy is managed server-side via ClientServerCore.Energy.EnergyServerService and is discarded by SavedData.ClearDummies(). Wallet currencies (Soft, Hard, Star, Tickets, Crystals) are stored in local Mementos and merged with server state.");
    } else {
        ImGui::TextWrapped("No valid Slutopia save file found in default location.");
        ImGui::TextDisabled("Expected path: %%USERPROFILE%%\\AppData\\LocalLow\\MobiStar Games\\Slutopia\\<PlayerID>");
        if (ImGui::Button("Search for Save File")) {
            app.RefreshSave();
        }
        ImGui::Spacing();
        DisabledFeatures("RESOURCES");
    }
}

void ProgressionView(TrainerManager& app) {
    if (app.saves.IsValid()) {
        const auto& info = app.saves.Info();
        bool hasProgression = false;
        static std::unordered_map<std::string, int> progEdits;

        for (const auto& p : info.parameters) {
            if (p.category != "PROGRESSION") continue;
            hasProgression = true;
            ImGui::PushID(p.id.c_str());
            ImGui::BeginChild("prog_row", {0, 110}, ImGuiChildFlags_Borders);
            ImGui::Text("%s", p.displayName.c_str());
            ImGui::SameLine(ImGui::GetWindowWidth() - 220);
            ImGui::TextColored({0.6f, 0.8f, 1.f, 1}, "Current: %d", p.value);

            if (progEdits.find(p.id) == progEdits.end()) {
                progEdits[p.id] = p.value;
            }

            ImGui::SetNextItemWidth(160);
            ImGui::InputInt("##prog_val", &progEdits[p.id]);
            ImGui::SameLine();
            if (ImGui::Button("Set Level")) {
                std::string err;
                if (app.saves.ModifyParameter(p.id, progEdits[p.id], err)) {
                    app.notice = "Updated " + p.displayName + " to " + std::to_string(progEdits[p.id]);
                    app.logger.Write(app.notice);
                } else {
                    app.notice = "Failed to update " + p.displayName + ": " + err;
                    app.logger.Write(app.notice);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("+1 Level")) {
                std::string err;
                progEdits[p.id] = p.value + 1;
                app.saves.ModifyParameter(p.id, progEdits[p.id], err);
            }
            ImGui::SameLine();
            if (ImGui::Button("+5 Levels")) {
                std::string err;
                progEdits[p.id] = p.value + 5;
                app.saves.ModifyParameter(p.id, progEdits[p.id], err);
            }

            ImGui::TextDisabled("Key: %s | Note: %s", p.id.c_str(), p.note.c_str());
            ImGui::EndChild();
            ImGui::PopID();
        }
        if (!hasProgression) {
            ImGui::TextDisabled("No progression parameters found in active save.");
        }
    }
    ImGui::Spacing();
    DisabledFeatures("PROGRESSION");
}

void Diagnostics(TrainerManager& app, const ProcessStatus& status) {
    if (app.gameInfo.installed) {
        ImGui::BeginChild("install_info", {0, 85}, ImGuiChildFlags_Borders);
        ImGui::TextColored({0.4f, 0.9f, 0.7f, 1}, "Detected Game Installation:");
        ImGui::TextWrapped("Path: %s", app.gameInfo.executablePath.string().c_str());
        ImGui::Text("Steam AppID: %s    BuildID: %s    Engine: %s",
            app.gameInfo.appId.c_str(), app.gameInfo.buildId.c_str(), app.gameInfo.engine.c_str());
        ImGui::EndChild();
        ImGui::Spacing();
    }

    ImGui::Text("PID: %lu    Architecture: %s", static_cast<unsigned long>(status.process.pid), status.architecture.c_str());
    ImGui::TextWrapped("Image: %s", Utf8(status.image).c_str());
    ImGui::TextWrapped("Engine evidence: %s", status.engine.c_str());
    ImGui::TextDisabled("Module snapshot at connection; Refresh updates it. F5: refresh, F6: connect/disconnect.");
    if (ImGui::Button("Refresh diagnostics")) app.processes.Refresh();

    if (ImGui::BeginTable("modules", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, {0, 160})) {
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
    ImGui::BeginChild("log", {0, 140}, ImGuiChildFlags_Borders);
    for (const auto& entry : app.logger.Entries()) ImGui::TextWrapped("%s", entry.c_str());
    ImGui::EndChild();
}

} // namespace

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
    std::snprintf(executable.data(), executable.size(), "%s", app.settings.executable.c_str());

    int tab = 0, frames = 0; bool running = true;
    const char* tabs[]{"RESOURCES", "PROGRESSION", "MISC", "SETTINGS"};

    while (running) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
            if (message.message == WM_QUIT) running = false;
        }
        if (!running) break;
        if (IsIconic(window.handle)) { MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT); continue; }
        RECT rect{}; GetClientRect(window.handle, &rect);
        DXGI_SWAP_CHAIN_DESC desc{};
        if (FAILED(graphics.swap->GetDesc(&desc))) throw std::runtime_error("Swap chain description failed");
        if (rect.right > 0 && rect.bottom > 0 && (desc.BufferDesc.Width != static_cast<UINT>(rect.right) || desc.BufferDesc.Height != static_cast<UINT>(rect.bottom))) {
            graphics.context->OMSetRenderTargets(0, nullptr, nullptr); graphics.target.Reset();
            if (FAILED(graphics.swap->ResizeBuffers(0, rect.right, rect.bottom, DXGI_FORMAT_UNKNOWN, 0)) || !graphics.Target())
                throw std::runtime_error("Swap chain resize failed");
        }

        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
        if (smoke) tab = (frames / 15) % 4;
        auto status = app.processes.Status();

        if (app.settings.hotkeys && !ImGui::GetIO().WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) app.processes.Refresh();
            if (ImGui::IsKeyPressed(ImGuiKey_F6, false)) { if (status.connected) app.Disconnect(); else app.Connect(); }
        }

        ImGui::SetNextWindowPos({0, 0}); ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("SLUTOPIA TRAINER", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

        ImGui::TextUnformatted("SLUTOPIA / TRAINER");
        ImGui::SameLine(ImGui::GetWindowWidth() - 320);
        if (status.connected) {
            ImGui::TextColored({0.4f, 0.9f, 0.7f, 1}, "● CONNECTED (PID: %lu)", static_cast<unsigned long>(status.process.pid));
        } else if (app.gameInfo.installed) {
            ImGui::TextColored({0.95f, 0.8f, 0.3f, 1}, "○ SLUTOPIA DETECTED");
        } else {
            ImGui::TextColored({0.95f, 0.7f, 0.4f, 1}, "○ DISCONNECTED");
        }

        ImGui::TextDisabled("C++20 External Trainer   /   Unity 6 Mono x64"); ImGui::Separator();

        ImGui::BeginChild("navigation", {185, 0});
        for (int i = 0; i < 4; ++i) {
            if (ImGui::Selectable(tabs[i], tab == i, 0, {0, 46})) tab = i;
        }
        ImGui::Spacing();
        if (app.gameInfo.installed) {
            ImGui::TextColored({0.4f, 0.9f, 0.7f, 1}, "Build: %s", app.gameInfo.buildId.c_str());
            ImGui::TextDisabled("Engine: Unity 6000.0\nBackend: Mono\nArch: x64");
        }
        ImGui::EndChild(); ImGui::SameLine();

        ImGui::BeginChild("content", {0, 0});
        ImGui::TextUnformatted(tabs[tab]);
        ImGui::TextWrapped("%s", status.state.c_str());
        if (!status.error.empty()) ImGui::TextWrapped("Diagnostic error: %s", status.error.c_str());
        ImGui::Separator();

        if (tab == 0) ResourcesView(app);
        if (tab == 1) ProgressionView(app);
        if (tab == 2) Diagnostics(app, status);
        if (tab == 3) {
            ImGui::TextWrapped("Executable Target:");
            ImGui::SetNextItemWidth(300);
            ImGui::InputText("##executable", executable.data(), executable.size());
            ImGui::SameLine();
            if (ImGui::Button("Auto-detect Game")) {
                app.DetectGame();
                if (app.gameInfo.installed) {
                    std::snprintf(executable.data(), executable.size(), "%s", Utf8(app.gameInfo.executableName).c_str());
                    app.settings.executable = executable.data();
                    app.Save();
                }
            }

            Toggle("Auto-connect on startup", app.settings.autoConnect, app.settings.reducedMotion);
            Toggle("Keyboard shortcuts (focused window: F5/F6)", app.settings.hotkeys, app.settings.reducedMotion);
            Toggle("Reduced motion", app.settings.reducedMotion, app.settings.reducedMotion);

            if (ImGui::Button("Save Settings")) {
                app.settings.executable = executable.data();
                app.Save();
            }
            ImGui::SameLine();
            if (ImGui::Button(status.connected ? "Disconnect" : "Connect")) {
                if (status.connected) app.Disconnect();
                else { app.settings.executable = executable.data(); app.Connect(); }
            }

            ImGui::Spacing();
            if (!app.notice.empty()) ImGui::TextColored({0.4f, 0.9f, 0.7f, 1}, "%s", app.notice.c_str());
            ImGui::TextWrapped("Config file: %s", Utf8(app.config.Path().wstring()).c_str());
            ImGui::TextWrapped("Save directory: %s", Utf8(app.saves.SaveDirectory().wstring()).c_str());
            ImGui::TextDisabled("Hotkeys: F5 refreshes diagnostics; F6 toggles connection.");
        }

        ImGui::EndChild(); ImGui::End(); ImGui::Render();

        const float clear[]{0.045f, 0.055f, 0.08f, 1};
        auto* target = graphics.target.Get(); graphics.context->OMSetRenderTargets(1, &target, nullptr);
        graphics.context->ClearRenderTargetView(target, clear); ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        const HRESULT present = graphics.swap->Present(1, 0);
        if (FAILED(present)) throw std::runtime_error("DirectX presentation failed (device removed or reset)");
        if (present == DXGI_STATUS_OCCLUDED) MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
        if (smoke && ++frames >= 60) running = false;
    }

    app.logger.Write("UI closed cleanly");
    return 0;
}

} // namespace trainer
