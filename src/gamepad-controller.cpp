#include "gamepad-controller.hpp"
#include <util/platform.h>
#include <cmath>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <xinput.h>

typedef DWORD(WINAPI *PFN_XInputGetState)(DWORD dwUserIndex, XINPUT_STATE *pState);
#endif

GamepadController::GamepadController()
	: enabled(false),
	  active_camera(0),
	  deadzone(0.15f),
	  sensitivity(1.0f),
	  is_manual_override(false),
	  last_manual_activity_ns(0),
	  prev_buttons(0),
	  xinput_dll(nullptr),
	  p_xinput_get_state(nullptr)
{
#ifdef _WIN32
	HMODULE mod = LoadLibraryW(L"xinput1_4.dll");
	if (!mod)
		mod = LoadLibraryW(L"xinput1_3.dll");
	if (!mod)
		mod = LoadLibraryW(L"xinput9_1_0.dll");

	if (mod) {
		xinput_dll = (void *)mod;
		p_xinput_get_state = (void *)GetProcAddress(mod, "XInputGetState");
	}
#endif
}

GamepadController::~GamepadController()
{
#ifdef _WIN32
	if (xinput_dll) {
		FreeLibrary((HMODULE)xinput_dll);
		xinput_dll = nullptr;
		p_xinput_get_state = nullptr;
	}
#endif
}

static inline float apply_axis_curve(float raw, float deadzone, float curve_pow = 2.2f)
{
	float abs_val = std::abs(raw);
	if (abs_val <= deadzone)
		return 0.0f;

	float norm = (abs_val - deadzone) / (1.0f - deadzone);
	float curved = std::pow(norm, curve_pow);
	return (raw < 0.0f) ? -curved : curved;
}

bool GamepadController::tick(float dt, GamepadState &state)
{
	state.connected = false;
	state.pan_axis = 0.0f;
	state.tilt_axis = 0.0f;
	state.zoom_axis = 0.0f;
	state.manual_active = is_manual_override;
	state.active_camera_index = active_camera;

	if (!enabled)
		return false;

#ifdef _WIN32
	if (!p_xinput_get_state)
		return false;

	PFN_XInputGetState fnGetState = (PFN_XInputGetState)p_xinput_get_state;
	XINPUT_STATE xstate;
	ZeroMemory(&xstate, sizeof(XINPUT_STATE));

	// Lê o primeiro controle conectado (usuário 0)
	DWORD res = fnGetState(0, &xstate);
	if (res != ERROR_SUCCESS) {
		return false;
	}

	state.connected = true;

	// 1. Analógico Esquerdo: Pan e Tilt
	float raw_lx = (float)xstate.Gamepad.sThumbLX / 32767.0f;
	float raw_ly = (float)xstate.Gamepad.sThumbLY / 32767.0f;
	raw_lx = std::clamp(raw_lx, -1.0f, 1.0f);
	raw_ly = std::clamp(raw_ly, -1.0f, 1.0f);

	float pan = apply_axis_curve(raw_lx, deadzone) * sensitivity;
	float tilt = apply_axis_curve(raw_ly, deadzone) * sensitivity;

	// 2. Gatilhos Analógicos LT / RT: Zoom Out e Zoom In
	float lt = (float)xstate.Gamepad.bLeftTrigger / 255.0f;
	float rt = (float)xstate.Gamepad.bRightTrigger / 255.0f;
	float zoom = 0.0f;
	if (rt > 0.05f)
		zoom += (rt - 0.05f) / 0.95f; // Zoom in
	if (lt > 0.05f)
		zoom -= (lt - 0.05f) / 0.95f; // Zoom out
	zoom = std::clamp(zoom * sensitivity, -1.0f, 1.0f);

	state.pan_axis = pan;
	state.tilt_axis = tilt;
	state.zoom_axis = zoom;

	uint64_t now_ns = os_gettime_ns();
	bool stick_active = (std::abs(pan) > 0.01f || std::abs(tilt) > 0.01f || std::abs(zoom) > 0.01f);

	if (stick_active) {
		is_manual_override = true;
		last_manual_activity_ns = now_ns;
	}

	// 3. Botões: detecção de cliques (borda de subida)
	uint32_t current_buttons = xstate.Gamepad.wButtons;
	uint32_t pressed_buttons = current_buttons & (~prev_buttons);
	prev_buttons = current_buttons;

	// Botão R3 (THUMB_RIGHT) ou START: Alterna Modo Manual / Auto-Tracking
	if ((pressed_buttons & XINPUT_GAMEPAD_RIGHT_THUMB) || (pressed_buttons & XINPUT_GAMEPAD_START)) {
		is_manual_override = !is_manual_override;
		if (on_mode_toggle)
			on_mode_toggle(is_manual_override);
	}

	// Botões Bumpers LB / RB: Troca de Câmera
	if (pressed_buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) {
		if (active_camera > 0)
			active_camera--;
		if (on_camera_change)
			on_camera_change(active_camera);
	}
	if (pressed_buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) {
		active_camera++;
		if (on_camera_change)
			on_camera_change(active_camera);
	}

	// Botões de Presets: A=Preset 1, B=Preset 2, X=Preset 3, Y=Preset 4
	if (on_preset) {
		if (pressed_buttons & XINPUT_GAMEPAD_A)
			on_preset(active_camera, 1);
		if (pressed_buttons & XINPUT_GAMEPAD_B)
			on_preset(active_camera, 2);
		if (pressed_buttons & XINPUT_GAMEPAD_X)
			on_preset(active_camera, 3);
		if (pressed_buttons & XINPUT_GAMEPAD_Y)
			on_preset(active_camera, 4);
	}

	state.manual_active = is_manual_override;
	state.active_camera_index = active_camera;

	// Se estiver em modo manual e houver callback de velocidade
	if (is_manual_override && on_speed) {
		int speed_pan = (int)std::round(pan * 24.0f);
		int speed_tilt = (int)std::round(tilt * 20.0f);
		int speed_zoom = (int)std::round(zoom * 7.0f);
		on_speed(active_camera, speed_pan, speed_tilt, speed_zoom);
	}

	return true;
#else
	return false;
#endif
}
