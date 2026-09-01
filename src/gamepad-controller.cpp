#include "gamepad-controller.hpp"
#include <util/platform.h>
#include <obs-frontend-api.h>
#include <obs.h>
#include <cmath>
#include <algorithm>

#ifdef _WIN32
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <xinput.h>
#include <dinput.h>
#include <vector>

typedef DWORD(WINAPI *PFN_XInputGetState)(DWORD dwUserIndex, XINPUT_STATE *pState);

struct DInputDeviceInfo {
	GUID guid;
	std::string name;
};

static std::vector<DInputDeviceInfo> s_dinput_devices;

static BOOL CALLBACK EnumJoysticksCallback(const DIDEVICEINSTANCEW *pdidInstance, VOID *)
{
	DInputDeviceInfo dev;
	dev.guid = pdidInstance->guidInstance;

	char nameUtf8[256];
	os_wcs_to_utf8(pdidInstance->tszInstanceName, 0, nameUtf8, sizeof(nameUtf8));
	dev.name = nameUtf8;

	s_dinput_devices.push_back(dev);
	return DIENUM_CONTINUE;
}

static IDirectInput8W *s_pDI = nullptr;
static IDirectInputDevice8W *s_pDIDevice = nullptr;
static GUID s_current_guid = {};
static bool s_has_active_guid = false;

static void init_directinput()
{
	if (!s_pDI) {
		HRESULT hr = DirectInput8Create(GetModuleHandle(NULL), DIRECTINPUT_VERSION, IID_IDirectInput8W, (VOID **)&s_pDI, NULL);
		if (FAILED(hr)) {
			blog(LOG_WARNING, "[Gamepad] Falha ao criar DirectInput8: 0x%08X", (unsigned int)hr);
		}
	}
}

static void release_directinput_device()
{
	if (s_pDIDevice) {
		s_pDIDevice->Unacquire();
		s_pDIDevice->Release();
		s_pDIDevice = nullptr;
	}
	s_has_active_guid = false;
}
#endif

GamepadController &GamepadController::get_instance()
{
	static GamepadController instance;
	return instance;
}

GamepadController::GamepadController()
	: enabled(true),
	  active_camera(0),
	  deadzone(0.15f),
	  sensitivity(1.0f),
	  is_manual_override(false),
	  last_manual_activity_ns(0),
	  prev_buttons(0),
	  prev_lt(0.0f),
	  prev_rt(0.0f),
	  xinput_dll(nullptr),
	  p_xinput_get_state(nullptr),
	  selected_device_id("auto"),
	  active_device_name("")
{
	scene_config.cut_on_lb = true;
	scene_config.trans_on_lt = true;

	memset(&last_state, 0, sizeof(last_state));

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

	init_directinput();
#endif
}

GamepadController::~GamepadController()
{
#ifdef _WIN32
	release_directinput_device();
	if (s_pDI) {
		s_pDI->Release();
		s_pDI = nullptr;
	}
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

static void switch_scene_by_name(const std::string &scene_name, bool preview_only)
{
	if (scene_name.empty())
		return;

	if (!obs_frontend_get_main_window())
		return;

	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_source_t *src = scenes.sources.array[i];
		const char *name = obs_source_get_name(src);
		if (name && scene_name == name) {
			if (preview_only) {
				obs_frontend_set_current_preview_scene(src);
			} else {
				obs_frontend_set_current_scene(src);
			}
			break;
		}
	}
	obs_frontend_source_list_free(&scenes);
}

std::vector<ControllerDeviceInfo> GamepadController::get_available_devices()
{
	std::vector<ControllerDeviceInfo> list;

	ControllerDeviceInfo autoDev;
	autoDev.id = "auto";
	autoDev.name = "⚡ Automático (Primeiro Ativo)";
	autoDev.is_xinput = false;
	autoDev.index = -1;
	list.push_back(autoDev);

#ifdef _WIN32
	// 1. Checa slots XInput (Xbox)
	if (p_xinput_get_state) {
		PFN_XInputGetState fnGetState = (PFN_XInputGetState)p_xinput_get_state;
		XINPUT_STATE xstate;
		for (DWORD i = 0; i < 4; i++) {
			if (fnGetState(i, &xstate) == ERROR_SUCCESS) {
				ControllerDeviceInfo dev;
				dev.id = "xinput_" + std::to_string(i);
				dev.name = "Xbox Controller (Slot " + std::to_string(i + 1) + ")";
				dev.is_xinput = true;
				dev.index = (int)i;
				list.push_back(dev);
			}
		}
	}

	// 2. Enumera DirectInput (PlayStation, Wireless Controller, GameSir, etc.)
	init_directinput();
	if (s_pDI) {
		s_dinput_devices.clear();
		s_pDI->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumJoysticksCallback, NULL, DIEDFL_ATTACHEDONLY);
		for (size_t i = 0; i < s_dinput_devices.size(); i++) {
			ControllerDeviceInfo dev;
			dev.id = "dinput_" + std::to_string(i);
			dev.name = s_dinput_devices[i].name;
			dev.is_xinput = false;
			dev.index = (int)i;
			list.push_back(dev);
		}
	}
#endif

	return list;
}

#ifdef _WIN32
static bool poll_xinput_slot(PFN_XInputGetState fnGetState, DWORD slot, GamepadState &state, float deadzone, float sensitivity)
{
	XINPUT_STATE xstate;
	ZeroMemory(&xstate, sizeof(XINPUT_STATE));
	if (fnGetState(slot, &xstate) != ERROR_SUCCESS)
		return false;

	state.connected = true;

	float raw_lx = (float)xstate.Gamepad.sThumbLX / 32767.0f;
	float raw_ly = (float)xstate.Gamepad.sThumbLY / 32767.0f;
	raw_lx = std::clamp(raw_lx, -1.0f, 1.0f);
	raw_ly = std::clamp(raw_ly, -1.0f, 1.0f);

	state.pan_axis = apply_axis_curve(raw_lx, deadzone) * sensitivity;
	state.tilt_axis = apply_axis_curve(raw_ly, deadzone) * sensitivity;

	state.trigger_left = (float)xstate.Gamepad.bLeftTrigger / 255.0f;
	state.trigger_right = (float)xstate.Gamepad.bRightTrigger / 255.0f;

	float zoom = 0.0f;
	if (state.trigger_right > 0.05f)
		zoom += (state.trigger_right - 0.05f) / 0.95f;
	if (state.trigger_left > 0.05f)
		zoom -= (state.trigger_left - 0.05f) / 0.95f;
	state.zoom_axis = std::clamp(zoom * sensitivity, -1.0f, 1.0f);

	uint32_t b = xstate.Gamepad.wButtons;
	state.buttons = b;
	state.btn_a = (b & XINPUT_GAMEPAD_A) != 0;
	state.btn_b = (b & XINPUT_GAMEPAD_B) != 0;
	state.btn_x = (b & XINPUT_GAMEPAD_X) != 0;
	state.btn_y = (b & XINPUT_GAMEPAD_Y) != 0;
	state.btn_lb = (b & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
	state.btn_rb = (b & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
	state.btn_lt = (state.trigger_left > 0.35f);
	state.btn_rt = (state.trigger_right > 0.35f);
	state.dpad_up = (b & XINPUT_GAMEPAD_DPAD_UP) != 0;
	state.dpad_down = (b & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
	state.dpad_left = (b & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
	state.dpad_right = (b & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
	state.btn_start = (b & XINPUT_GAMEPAD_START) != 0;
	state.btn_back = (b & XINPUT_GAMEPAD_BACK) != 0;
	state.btn_thumb_l = (b & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
	state.btn_thumb_r = (b & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;

	return true;
}

static bool poll_dinput_device(const GUID &guid, GamepadState &state, float deadzone, float sensitivity)
{
	if (!s_pDI) return false;

	if (!s_pDIDevice || !s_has_active_guid || memcmp(&s_current_guid, &guid, sizeof(GUID)) != 0) {
		release_directinput_device();
		HRESULT hr = s_pDI->CreateDevice(guid, &s_pDIDevice, NULL);
		if (FAILED(hr) || !s_pDIDevice)
			return false;

		hr = s_pDIDevice->SetDataFormat(&c_dfDIJoystick2);
		if (FAILED(hr)) {
			release_directinput_device();
			return false;
		}

		HWND hwnd = (HWND)obs_frontend_get_main_window();
		if (!hwnd)
			hwnd = GetDesktopWindow();
		s_pDIDevice->SetCooperativeLevel(hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);

		s_current_guid = guid;
		s_has_active_guid = true;
		s_pDIDevice->Acquire();
	}

	DIJOYSTATE2 js;
	ZeroMemory(&js, sizeof(DIJOYSTATE2));
	HRESULT hr = s_pDIDevice->Poll();
	hr = s_pDIDevice->GetDeviceState(sizeof(DIJOYSTATE2), &js);
	if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
		s_pDIDevice->Acquire();
		hr = s_pDIDevice->GetDeviceState(sizeof(DIJOYSTATE2), &js);
	}

	if (FAILED(hr))
		return false;

	state.connected = true;

	// Normaliza analógico esquerdo (Pan e Tilt)
	// Valores de 0 a 65535, centro em 32768
	float raw_lx = ((float)js.lX - 32768.0f) / 32768.0f;
	float raw_ly = -((float)js.lY - 32768.0f) / 32768.0f;
	raw_lx = std::clamp(raw_lx, -1.0f, 1.0f);
	raw_ly = std::clamp(raw_ly, -1.0f, 1.0f);

	state.pan_axis = apply_axis_curve(raw_lx, deadzone) * sensitivity;
	state.tilt_axis = apply_axis_curve(raw_ly, deadzone) * sensitivity;

	// Botões para o Wireless Controller / PlayStation / DirectInput
	// js.rgbButtons[0] = Quadrado/X, [1] = Cruz/A, [2] = Círculo/B, [3] = Triângulo/Y
	// js.rgbButtons[4] = L1 (LB), [5] = R1 (RB)
	// js.rgbButtons[6] = L2 (LT), [7] = R2 (RT)
	// js.rgbButtons[8] = Share/Back, [9] = Options/Start
	// js.rgbButtons[10] = L3, [11] = R3
	state.btn_x = (js.rgbButtons[0] & 0x80) != 0;
	state.btn_a = (js.rgbButtons[1] & 0x80) != 0;
	state.btn_b = (js.rgbButtons[2] & 0x80) != 0;
	state.btn_y = (js.rgbButtons[3] & 0x80) != 0;
	state.btn_lb = (js.rgbButtons[4] & 0x80) != 0;
	state.btn_rb = (js.rgbButtons[5] & 0x80) != 0;
	state.btn_lt = (js.rgbButtons[6] & 0x80) != 0;
	state.btn_rt = (js.rgbButtons[7] & 0x80) != 0;
	state.btn_back = (js.rgbButtons[8] & 0x80) != 0;
	state.btn_start = (js.rgbButtons[9] & 0x80) != 0;
	state.btn_thumb_l = (js.rgbButtons[10] & 0x80) != 0;
	state.btn_thumb_r = (js.rgbButtons[11] & 0x80) != 0;

	// Gatilhos analógicos ou digitais
	state.trigger_left = state.btn_lt ? 1.0f : 0.0f;
	state.trigger_right = state.btn_rt ? 1.0f : 0.0f;

	float zoom = 0.0f;
	if (state.trigger_right > 0.05f || state.btn_rt)
		zoom += 1.0f;
	if (state.trigger_left > 0.05f || state.btn_lt)
		zoom -= 1.0f;
	state.zoom_axis = std::clamp(zoom * sensitivity, -1.0f, 1.0f);

	// D-Pad via POV Hat
	DWORD pov = js.rgdwPOV[0];
	if (pov != 0xFFFFFFFF && pov != 65535) {
		if (pov >= 31500 || pov <= 4500)
			state.dpad_up = true;
		if (pov >= 4500 && pov <= 13500)
			state.dpad_right = true;
		if (pov >= 13500 && pov <= 22500)
			state.dpad_down = true;
		if (pov >= 22500 && pov <= 31500)
			state.dpad_left = true;
	}

	uint32_t b = 0;
	if (state.btn_a) b |= XINPUT_GAMEPAD_A;
	if (state.btn_b) b |= XINPUT_GAMEPAD_B;
	if (state.btn_x) b |= XINPUT_GAMEPAD_X;
	if (state.btn_y) b |= XINPUT_GAMEPAD_Y;
	if (state.btn_lb) b |= XINPUT_GAMEPAD_LEFT_SHOULDER;
	if (state.btn_rb) b |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
	if (state.btn_start) b |= XINPUT_GAMEPAD_START;
	if (state.btn_back) b |= XINPUT_GAMEPAD_BACK;
	if (state.btn_thumb_l) b |= XINPUT_GAMEPAD_LEFT_THUMB;
	if (state.btn_thumb_r) b |= XINPUT_GAMEPAD_RIGHT_THUMB;
	if (state.dpad_up) b |= XINPUT_GAMEPAD_DPAD_UP;
	if (state.dpad_down) b |= XINPUT_GAMEPAD_DPAD_DOWN;
	if (state.dpad_left) b |= XINPUT_GAMEPAD_DPAD_LEFT;
	if (state.dpad_right) b |= XINPUT_GAMEPAD_DPAD_RIGHT;
	state.buttons = b;

	return true;
}
#endif

bool GamepadController::poll_state(GamepadState &state)
{
	memset(&state, 0, sizeof(GamepadState));
	state.active_camera_index = active_camera;
	state.manual_active = is_manual_override;

#ifdef _WIN32
	PFN_XInputGetState fnGetState = (PFN_XInputGetState)p_xinput_get_state;

	// Caso 1: Usuário escolheu um slot específico do XInput
	if (selected_device_id.rfind("xinput_", 0) == 0 && fnGetState) {
		DWORD slot = (DWORD)std::atoi(selected_device_id.substr(7).c_str());
		if (poll_xinput_slot(fnGetState, slot, state, deadzone, sensitivity)) {
			active_device_name = "Xbox Controller (Slot " + std::to_string(slot + 1) + ")";
			last_state = state;
			return true;
		}
	}

	// Caso 2: Usuário escolheu um dispositivo específico de DirectInput
	if (selected_device_id.rfind("dinput_", 0) == 0) {
		int idx = std::atoi(selected_device_id.substr(7).c_str());
		if (idx >= 0 && idx < (int)s_dinput_devices.size()) {
			if (poll_dinput_device(s_dinput_devices[idx].guid, state, deadzone, sensitivity)) {
				active_device_name = s_dinput_devices[idx].name;
				last_state = state;
				return true;
			}
		}
	}

	// Caso 3: Automático (Tenta XInput primeiro, depois DirectInput)
	if (selected_device_id.empty() || selected_device_id == "auto") {
		if (fnGetState) {
			for (DWORD i = 0; i < 4; i++) {
				if (poll_xinput_slot(fnGetState, i, state, deadzone, sensitivity)) {
					active_device_name = "Xbox Controller (Slot " + std::to_string(i + 1) + ")";
					last_state = state;
					return true;
				}
			}
		}

		// Se nenhum XInput respondeu, tenta DirectInput
		init_directinput();
		if (s_pDI) {
			if (s_dinput_devices.empty()) {
				s_pDI->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumJoysticksCallback, NULL, DIEDFL_ATTACHEDONLY);
			}
			for (size_t i = 0; i < s_dinput_devices.size(); i++) {
				if (poll_dinput_device(s_dinput_devices[i].guid, state, deadzone, sensitivity)) {
					active_device_name = s_dinput_devices[i].name;
					last_state = state;
					return true;
				}
			}
		}
	}

	state.connected = false;
	active_device_name = "";
	last_state = state;
	return false;
#else
	state.connected = false;
	active_device_name = "";
	return false;
#endif
}

static void recall_ptz_preset(int camera_idx, int preset_num)
{
	blog(LOG_INFO, "[Gamepad PTZ] Recalling preset %d on camera %d", preset_num, camera_idx);
	proc_handler_t *ph = obs_get_proc_handler();
	if (ph) {
		calldata_t cd;
		calldata_init(&cd);
		calldata_set_int(&cd, "camera_index", camera_idx);
		calldata_set_int(&cd, "preset", preset_num);
		proc_handler_call(ph, "ptz_preset_recall", &cd);
		calldata_free(&cd);
	}
}

bool GamepadController::tick(float dt, GamepadState &state)
{
	if (!enabled)
		return false;

	if (!poll_state(state))
		return false;

	uint64_t now_ns = os_gettime_ns();
	bool stick_active = (std::abs(state.pan_axis) > 0.01f || std::abs(state.tilt_axis) > 0.01f ||
			     std::abs(state.zoom_axis) > 0.01f);

	if (stick_active) {
		is_manual_override = true;
		last_manual_activity_ns = now_ns;
	}

	uint32_t current_buttons = state.buttons;
	uint32_t pressed_buttons = current_buttons & (~prev_buttons);
	prev_buttons = current_buttons;

	float curr_lt = state.trigger_left;
	float curr_rt = state.trigger_right;
	bool lt_pressed = (curr_lt > 0.5f && prev_lt <= 0.5f);
	prev_lt = curr_lt;
	prev_rt = curr_rt;

	if ((pressed_buttons & XINPUT_GAMEPAD_RIGHT_THUMB) || (pressed_buttons & XINPUT_GAMEPAD_START)) {
		is_manual_override = !is_manual_override;
		if (on_mode_toggle)
			on_mode_toggle(is_manual_override);
	}

	bool is_rb_held = state.btn_rb;
	bool is_rt_held = (curr_rt > 0.4f || state.btn_rt);

	// 1. PRESETS PTZ NOS BOTÕES A, B, X, Y (1 a 12)
	if (is_rt_held) {
		if (pressed_buttons & XINPUT_GAMEPAD_A) {
			recall_ptz_preset(active_camera, 9);
			if (on_preset) on_preset(active_camera, 9);
		}
		if (pressed_buttons & XINPUT_GAMEPAD_B) {
			recall_ptz_preset(active_camera, 10);
			if (on_preset) on_preset(active_camera, 10);
		}
		if (pressed_buttons & XINPUT_GAMEPAD_X) {
			recall_ptz_preset(active_camera, 11);
			if (on_preset) on_preset(active_camera, 11);
		}
		if (pressed_buttons & XINPUT_GAMEPAD_Y) {
			recall_ptz_preset(active_camera, 12);
			if (on_preset) on_preset(active_camera, 12);
		}
	} else if (is_rb_held) {
		if (pressed_buttons & XINPUT_GAMEPAD_A) {
			recall_ptz_preset(active_camera, 5);
			if (on_preset) on_preset(active_camera, 5);
		}
		if (pressed_buttons & XINPUT_GAMEPAD_B) {
			recall_ptz_preset(active_camera, 6);
			if (on_preset) on_preset(active_camera, 6);
		}
		if (pressed_buttons & XINPUT_GAMEPAD_X) {
			recall_ptz_preset(active_camera, 7);
			if (on_preset) on_preset(active_camera, 7);
		}
		if (pressed_buttons & XINPUT_GAMEPAD_Y) {
			recall_ptz_preset(active_camera, 8);
			if (on_preset) on_preset(active_camera, 8);
		}
	} else {
		if (pressed_buttons & XINPUT_GAMEPAD_A) {
			recall_ptz_preset(active_camera, 1);
			if (on_preset) on_preset(active_camera, 1);
		}
		if (pressed_buttons & XINPUT_GAMEPAD_B) {
			recall_ptz_preset(active_camera, 2);
			if (on_preset) on_preset(active_camera, 2);
		}
		if (pressed_buttons & XINPUT_GAMEPAD_X) {
			recall_ptz_preset(active_camera, 3);
			if (on_preset) on_preset(active_camera, 3);
		}
		if (pressed_buttons & XINPUT_GAMEPAD_Y) {
			recall_ptz_preset(active_camera, 4);
			if (on_preset) on_preset(active_camera, 4);
		}
	}

	// 2. TROCA DE CENAS NO D-PAD
	if (is_rt_held) {
		if (pressed_buttons & XINPUT_GAMEPAD_DPAD_UP)
			switch_scene_by_name(scene_config.scene_rt_dpad_up, false);
		if (pressed_buttons & XINPUT_GAMEPAD_DPAD_DOWN)
			switch_scene_by_name(scene_config.scene_rt_dpad_down, false);
		if (pressed_buttons & XINPUT_GAMEPAD_DPAD_LEFT)
			switch_scene_by_name(scene_config.scene_rt_dpad_left, false);
		if (pressed_buttons & XINPUT_GAMEPAD_DPAD_RIGHT)
			switch_scene_by_name(scene_config.scene_rt_dpad_right, false);
	} else if (is_rb_held) {
		if (pressed_buttons & XINPUT_GAMEPAD_DPAD_UP)
			switch_scene_by_name(scene_config.scene_rb_dpad_up, false);
		if (pressed_buttons & XINPUT_GAMEPAD_DPAD_DOWN)
			switch_scene_by_name(scene_config.scene_rb_dpad_down, false);
		if (pressed_buttons & XINPUT_GAMEPAD_DPAD_LEFT)
			switch_scene_by_name(scene_config.scene_rb_dpad_left, false);
		if (pressed_buttons & XINPUT_GAMEPAD_DPAD_RIGHT)
			switch_scene_by_name(scene_config.scene_rb_dpad_right, false);
	} else {
		if (pressed_buttons & XINPUT_GAMEPAD_DPAD_UP)
			switch_scene_by_name(scene_config.scene_dpad_up, false);
		if (pressed_buttons & XINPUT_GAMEPAD_DPAD_DOWN)
			switch_scene_by_name(scene_config.scene_dpad_down, false);
		if (pressed_buttons & XINPUT_GAMEPAD_DPAD_LEFT)
			switch_scene_by_name(scene_config.scene_dpad_left, false);
		if (pressed_buttons & XINPUT_GAMEPAD_DPAD_RIGHT)
			switch_scene_by_name(scene_config.scene_dpad_right, false);
	}

	// 3. MESA DE CORTE (LB = Corte Seco, LT = Transição Suave)
	if (scene_config.cut_on_lb && (pressed_buttons & XINPUT_GAMEPAD_LEFT_SHOULDER)) {
		if (obs_frontend_get_main_window()) {
			obs_source_t *preview_scene = obs_frontend_get_current_preview_scene();
			if (preview_scene) {
				obs_frontend_set_current_scene(preview_scene);
				obs_source_release(preview_scene);
			}
		}
	}

	if (scene_config.trans_on_lt && lt_pressed) {
		if (obs_frontend_get_main_window()) {
			obs_frontend_preview_program_trigger_transition();
		}
	}

	state.manual_active = is_manual_override;
	state.active_camera_index = active_camera;

	if (is_manual_override && on_speed) {
		int speed_pan = (int)std::round(state.pan_axis * 24.0f);
		int speed_tilt = (int)std::round(state.tilt_axis * 20.0f);
		int speed_zoom = (int)std::round(state.zoom_axis * 7.0f);
		on_speed(active_camera, speed_pan, speed_tilt, speed_zoom);
	}

	return true;
}
