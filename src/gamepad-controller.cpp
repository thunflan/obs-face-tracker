#include "gamepad-controller.hpp"
#include <util/platform.h>
#include <obs-frontend-api.h>
#include <obs.h>
#include <cmath>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <xinput.h>

typedef DWORD(WINAPI *PFN_XInputGetState)(DWORD dwUserIndex, XINPUT_STATE *pState);
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
	winmm_dll(nullptr),
	p_joy_get_pos_ex(nullptr)
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

	HMODULE wmod = LoadLibraryW(L"winmm.dll");
	if (wmod) {
		winmm_dll = (void *)wmod;
		p_joy_get_pos_ex = (void *)GetProcAddress(wmod, "joyGetPosEx");
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
	if (winmm_dll) {
		FreeLibrary((HMODULE)winmm_dll);
		winmm_dll = nullptr;
		p_joy_get_pos_ex = nullptr;
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

bool GamepadController::poll_state(GamepadState &state)
{
	memset(&state, 0, sizeof(GamepadState));
	state.active_camera_index = active_camera;
	state.manual_active = is_manual_override;

#ifdef _WIN32
	// 1. TENTA PRIMEIRO VIA XINPUT (Xbox padrão)
	if (p_xinput_get_state) {
		PFN_XInputGetState fnGetState = (PFN_XInputGetState)p_xinput_get_state;
		XINPUT_STATE xstate;
		ZeroMemory(&xstate, sizeof(XINPUT_STATE));

		for (DWORD i = 0; i < 4; i++) {
			if (fnGetState(i, &xstate) == ERROR_SUCCESS) {
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

				last_state = state;
				return true;
			}
		}
	}

	// 2. SE NÃO HOUVER XINPUT, TENTA VIA WINMM / DIRECTINPUT (GameSir Nova Lite, Bluetooth Wireless Controller, PS4/PS5, Switch)
	if (p_joy_get_pos_ex) {
		typedef UINT(WINAPI * PFN_joyGetPosEx)(UINT uJoyID, LPJOYINFOEX pji);
		PFN_joyGetPosEx fnJoy = (PFN_joyGetPosEx)p_joy_get_pos_ex;
		JOYINFOEX jie;
		jie.dwSize = sizeof(JOYINFOEX);
		jie.dwFlags = JOY_RETURNALL;

		for (UINT i = 0; i < 16; i++) {
			if (fnJoy(i, &jie) == 0) { // JOYERR_NOERROR
				state.connected = true;

				float raw_lx = ((float)jie.dwXpos - 32767.5f) / 32767.5f;
				float raw_ly = -((float)jie.dwYpos - 32767.5f) / 32767.5f;
				raw_lx = std::clamp(raw_lx, -1.0f, 1.0f);
				raw_ly = std::clamp(raw_ly, -1.0f, 1.0f);

				state.pan_axis = apply_axis_curve(raw_lx, deadzone) * sensitivity;
				state.tilt_axis = apply_axis_curve(raw_ly, deadzone) * sensitivity;

				// Gatilhos e Botões
				state.btn_a = (jie.dwButtons & (1 << 0)) != 0;
				state.btn_b = (jie.dwButtons & (1 << 1)) != 0;
				state.btn_x = (jie.dwButtons & (1 << 2)) != 0 || (jie.dwButtons & (1 << 3)) != 0;
				state.btn_y = (jie.dwButtons & (1 << 3)) != 0 || (jie.dwButtons & (1 << 4)) != 0;
				state.btn_lb = (jie.dwButtons & (1 << 4)) != 0;
				state.btn_rb = (jie.dwButtons & (1 << 5)) != 0;
				state.btn_lt = (jie.dwButtons & (1 << 6)) != 0;
				state.btn_rt = (jie.dwButtons & (1 << 7)) != 0;
				state.btn_back = (jie.dwButtons & (1 << 8)) != 0;
				state.btn_start = (jie.dwButtons & (1 << 9)) != 0;
				state.btn_thumb_l = (jie.dwButtons & (1 << 10)) != 0;
				state.btn_thumb_r = (jie.dwButtons & (1 << 11)) != 0;

				state.trigger_left = state.btn_lt ? 1.0f : 0.0f;
				state.trigger_right = state.btn_rt ? 1.0f : 0.0f;

				float zoom = 0.0f;
				if (state.btn_rt)
					zoom += 1.0f;
				if (state.btn_lt)
					zoom -= 1.0f;
				state.zoom_axis = std::clamp(zoom * sensitivity, -1.0f, 1.0f);

				// D-Pad via POV
				if (jie.dwPOV != 65535) {
					if (jie.dwPOV >= 31500 || jie.dwPOV <= 4500)
						state.dpad_up = true;
					if (jie.dwPOV >= 4500 && jie.dwPOV <= 13500)
						state.dpad_right = true;
					if (jie.dwPOV >= 13500 && jie.dwPOV <= 22500)
						state.dpad_down = true;
					if (jie.dwPOV >= 22500 && jie.dwPOV <= 31500)
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

				last_state = state;
				return true;
			}
		}
	}

	state.connected = false;
	last_state = state;
	return false;
#else
	state.connected = false;
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
