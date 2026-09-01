#include "gamepad-controller.hpp"
#include <util/platform.h>
#include <obs-frontend-api.h>
#include <obs.h>
#include <cmath>
#include <algorithm>
#include <SDL.h>

static bool s_sdl_initialized = false;
static SDL_GameController *s_current_controller = nullptr;
static SDL_Joystick *s_current_joystick = nullptr;
static int s_current_opened_index = -1;

static void ensure_sdl_init()
{
	if (!s_sdl_initialized) {
		SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
		SDL_SetHint(SDL_HINT_AUTO_UPDATE_JOYSTICKS, "1");
		if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) == 0) {
			s_sdl_initialized = true;
			blog(LOG_INFO, "[Gamepad SDL2] Subsystem inicializado com sucesso.");
		} else {
			blog(LOG_ERROR, "[Gamepad SDL2] Erro ao inicializar SDL: %s", SDL_GetError());
		}
	}
}

static void close_current_device()
{
	if (s_current_controller) {
		SDL_GameControllerClose(s_current_controller);
		s_current_controller = nullptr;
	}
	if (s_current_joystick) {
		SDL_JoystickClose(s_current_joystick);
		s_current_joystick = nullptr;
	}
	s_current_opened_index = -1;
}

GamepadController &GamepadController::get_instance()
{
	static GamepadController instance;
	return instance;
}

GamepadController::GamepadController()
	: enabled(true),
	  active_camera(1),
	  deadzone(0.12f),
	  sensitivity(1.0f),
	  is_manual_override(false),
	  last_manual_activity_ns(0),
	  prev_buttons(0),
	  prev_lt(0.0f),
	  prev_rt(0.0f),
	  curve_gamma(2.2f),
	  min_speed(0.04f),
	  max_speed(1.0f),
	  zoom_speed_mult(0.8f),
	  selected_device_id("auto"),
	  active_device_name("")
{
	scene_config.cut_on_lb = true;
	scene_config.trans_on_lt = true;

	memset(&last_state, 0, sizeof(last_state));
	ensure_sdl_init();
}

GamepadController::~GamepadController()
{
	close_current_device();
	if (s_sdl_initialized) {
		SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK);
		s_sdl_initialized = false;
	}
}

static inline float apply_progressive_curve(float raw, float deadzone, float curve_pow, float min_spd, float max_spd)
{
	float abs_val = std::abs(raw);
	if (abs_val <= deadzone)
		return 0.0f;

	float norm = (abs_val - deadzone) / (1.0f - deadzone);
	norm = std::clamp(norm, 0.0f, 1.0f);
	float curved = std::pow(norm, curve_pow);
	float spd = min_spd + (max_spd - min_spd) * curved;
	spd = std::clamp(spd, 0.0f, max_spd);
	return (raw < 0.0f) ? -spd : spd;
}

int GamepadController::get_obsptz_active_device_id()
{
	static int cached_id = 1;
	static uint64_t last_check_ns = 0;
	uint64_t now_ns = os_gettime_ns();

	// Verifica o arquivo de configuração do obs-ptz a cada 200ms para seguir a seleção ativa do PTZ Controls
	if (now_ns - last_check_ns > 200000000ULL) {
		last_check_ns = now_ns;
		const char *appdata = getenv("APPDATA");
		if (appdata) {
			std::string cfg_path = std::string(appdata) + "\\obs-studio\\plugin_config\\obs-ptz\\config.json";
			FILE *fp = os_fopen(cfg_path.c_str(), "rb");
			if (fp) {
				fseek(fp, 0, SEEK_END);
				long len = ftell(fp);
				fseek(fp, 0, SEEK_SET);
				if (len > 0 && len < 65536) {
					std::string content(len, '\0');
					size_t read_bytes = fread(&content[0], 1, len, fp);
					if (read_bytes == (size_t)len) {
						size_t pos = content.find("\"current_selected\"");
						if (pos != std::string::npos) {
							size_t colon = content.find(':', pos);
							if (colon != std::string::npos) {
								int id = std::atoi(content.c_str() + colon + 1);
								if (id >= 0) {
									cached_id = id;
								}
							}
						}
					}
				}
				fclose(fp);
			}
		}
	}
	return cached_id;
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
	ensure_sdl_init();
	std::vector<ControllerDeviceInfo> list;

	ControllerDeviceInfo autoDev;
	autoDev.id = "auto";
	autoDev.name = "⚡ Automático (Primeiro Ativo)";
	autoDev.is_gamecontroller = true;
	autoDev.index = -1;
	list.push_back(autoDev);

	if (!s_sdl_initialized)
		return list;

	SDL_JoystickUpdate();
	int numJoysticks = SDL_NumJoysticks();
	for (int i = 0; i < numJoysticks; i++) {
		ControllerDeviceInfo dev;
		dev.id = "sdl_" + std::to_string(i);
		dev.index = i;
		dev.is_gamecontroller = SDL_IsGameController(i);

		const char *name = nullptr;
		if (dev.is_gamecontroller) {
			name = SDL_GameControllerNameForIndex(i);
		}
		if (!name || !*name) {
			name = SDL_JoystickNameForIndex(i);
		}
		if (!name || !*name) {
			name = "Dispositivo de Jogo";
		}

		dev.name = name;
		list.push_back(dev);
	}

	return list;
}

bool GamepadController::poll_state(GamepadState &state)
{
	memset(&state, 0, sizeof(GamepadState));
	state.active_camera_index = active_camera;
	state.manual_active = is_manual_override;

	ensure_sdl_init();
	if (!s_sdl_initialized)
		return false;

	SDL_JoystickUpdate();
	SDL_GameControllerUpdate();

	int numJoysticks = SDL_NumJoysticks();
	if (numJoysticks <= 0) {
		close_current_device();
		state.connected = false;
		active_device_name = "";
		last_state = state;
		return false;
	}

	int target_index = -1;
	if (selected_device_id.empty() || selected_device_id == "auto") {
		target_index = 0;
	} else if (selected_device_id.rfind("sdl_", 0) == 0) {
		target_index = std::atoi(selected_device_id.substr(4).c_str());
		if (target_index < 0 || target_index >= numJoysticks) {
			target_index = 0;
		}
	}

	if (target_index < 0 || target_index >= numJoysticks) {
		close_current_device();
		state.connected = false;
		active_device_name = "";
		last_state = state;
		return false;
	}

	if (s_current_opened_index != target_index || (!s_current_controller && !s_current_joystick)) {
		close_current_device();
		if (SDL_IsGameController(target_index)) {
			s_current_controller = SDL_GameControllerOpen(target_index);
		}
		if (!s_current_controller) {
			s_current_joystick = SDL_JoystickOpen(target_index);
		}
		s_current_opened_index = target_index;
	}

	// 1. VIA SDL_GameController (XInput, PS4, PS5, Switch, GameSir, etc.)
	if (s_current_controller && SDL_GameControllerGetAttached(s_current_controller)) {
		state.connected = true;
		const char *cname = SDL_GameControllerName(s_current_controller);
		active_device_name = (cname && *cname) ? cname : SDL_JoystickNameForIndex(target_index);

		int16_t axis_x = SDL_GameControllerGetAxis(s_current_controller, SDL_CONTROLLER_AXIS_LEFTX);
		int16_t axis_y = SDL_GameControllerGetAxis(s_current_controller, SDL_CONTROLLER_AXIS_LEFTY);

		float raw_lx = (float)axis_x / 32767.0f;
		float raw_ly = -(float)axis_y / 32767.0f;
		raw_lx = std::clamp(raw_lx, -1.0f, 1.0f);
		raw_ly = std::clamp(raw_ly, -1.0f, 1.0f);

		state.pan_axis = apply_progressive_curve(raw_lx, deadzone, curve_gamma, min_speed, max_speed * sensitivity);
		state.tilt_axis = apply_progressive_curve(raw_ly, deadzone, curve_gamma, min_speed, max_speed * sensitivity);

		int16_t trig_l = SDL_GameControllerGetAxis(s_current_controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
		int16_t trig_r = SDL_GameControllerGetAxis(s_current_controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
		state.trigger_left = std::clamp((float)trig_l / 32767.0f, 0.0f, 1.0f);
		state.trigger_right = std::clamp((float)trig_r / 32767.0f, 0.0f, 1.0f);

		// Zoom no Analógico Direito (Stick Direito Y):
		// Empurrar para cima = Zoom In (+) progressivo | Puxar para baixo = Zoom Out (-) progressivo
		int16_t axis_ry = SDL_GameControllerGetAxis(s_current_controller, SDL_CONTROLLER_AXIS_RIGHTY);
		float raw_ry = -(float)axis_ry / 32767.0f;
		raw_ry = std::clamp(raw_ry, -1.0f, 1.0f);
		state.zoom_axis = apply_progressive_curve(raw_ry, deadzone, curve_gamma, min_speed, max_speed * zoom_speed_mult * sensitivity);

		state.btn_a = SDL_GameControllerGetButton(s_current_controller, SDL_CONTROLLER_BUTTON_A) != 0;
		state.btn_b = SDL_GameControllerGetButton(s_current_controller, SDL_CONTROLLER_BUTTON_B) != 0;
		state.btn_x = SDL_GameControllerGetButton(s_current_controller, SDL_CONTROLLER_BUTTON_X) != 0;
		state.btn_y = SDL_GameControllerGetButton(s_current_controller, SDL_CONTROLLER_BUTTON_Y) != 0;
		state.btn_lb = SDL_GameControllerGetButton(s_current_controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) != 0;
		state.btn_rb = SDL_GameControllerGetButton(s_current_controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) != 0;
		state.btn_lt = (state.trigger_left > 0.35f);
		state.btn_rt = (state.trigger_right > 0.35f);
		state.dpad_up = SDL_GameControllerGetButton(s_current_controller, SDL_CONTROLLER_BUTTON_DPAD_UP) != 0;
		state.dpad_down = SDL_GameControllerGetButton(s_current_controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN) != 0;
		state.dpad_left = SDL_GameControllerGetButton(s_current_controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT) != 0;
		state.dpad_right = SDL_GameControllerGetButton(s_current_controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) != 0;
		state.btn_start = SDL_GameControllerGetButton(s_current_controller, SDL_CONTROLLER_BUTTON_START) != 0;
		state.btn_back = SDL_GameControllerGetButton(s_current_controller, SDL_CONTROLLER_BUTTON_BACK) != 0;
		state.btn_thumb_l = SDL_GameControllerGetButton(s_current_controller, SDL_CONTROLLER_BUTTON_LEFTSTICK) != 0;
		state.btn_thumb_r = SDL_GameControllerGetButton(s_current_controller, SDL_CONTROLLER_BUTTON_RIGHTSTICK) != 0;

		last_state = state;
		return true;
	}

	// 2. FALLBACK VIA SDL_Joystick
	if (s_current_joystick && SDL_JoystickGetAttached(s_current_joystick)) {
		state.connected = true;
		const char *jname = SDL_JoystickName(s_current_joystick);
		active_device_name = (jname && *jname) ? jname : "Joystick";

		int numAxes = SDL_JoystickNumAxes(s_current_joystick);
		if (numAxes >= 2) {
			int16_t axis_x = SDL_JoystickGetAxis(s_current_joystick, 0);
			int16_t axis_y = SDL_JoystickGetAxis(s_current_joystick, 1);
			float raw_lx = (float)axis_x / 32767.0f;
			float raw_ly = -(float)axis_y / 32767.0f;
			state.pan_axis = apply_progressive_curve(raw_lx, deadzone, curve_gamma, min_speed, max_speed * sensitivity);
			state.tilt_axis = apply_progressive_curve(raw_ly, deadzone, curve_gamma, min_speed, max_speed * sensitivity);
		}

		if (numAxes >= 4) {
			int16_t axis_ry = SDL_JoystickGetAxis(s_current_joystick, 3);
			float raw_ry = -(float)axis_ry / 32767.0f;
			raw_ry = std::clamp(raw_ry, -1.0f, 1.0f);
			state.zoom_axis = apply_progressive_curve(raw_ry, deadzone, curve_gamma, min_speed, max_speed * zoom_speed_mult * sensitivity);
		}

		int numButtons = SDL_JoystickNumButtons(s_current_joystick);
		if (numButtons > 0) state.btn_a = SDL_JoystickGetButton(s_current_joystick, 0) != 0;
		if (numButtons > 1) state.btn_b = SDL_JoystickGetButton(s_current_joystick, 1) != 0;
		if (numButtons > 2) state.btn_x = SDL_JoystickGetButton(s_current_joystick, 2) != 0;
		if (numButtons > 3) state.btn_y = SDL_JoystickGetButton(s_current_joystick, 3) != 0;
		if (numButtons > 4) state.btn_lb = SDL_JoystickGetButton(s_current_joystick, 4) != 0;
		if (numButtons > 5) state.btn_rb = SDL_JoystickGetButton(s_current_joystick, 5) != 0;
		if (numButtons > 6) state.btn_lt = SDL_JoystickGetButton(s_current_joystick, 6) != 0;
		if (numButtons > 7) state.btn_rt = SDL_JoystickGetButton(s_current_joystick, 7) != 0;
		if (numButtons > 8) state.btn_back = SDL_JoystickGetButton(s_current_joystick, 8) != 0;
		if (numButtons > 9) state.btn_start = SDL_JoystickGetButton(s_current_joystick, 9) != 0;

		state.trigger_left = state.btn_lt ? 1.0f : 0.0f;
		state.trigger_right = state.btn_rt ? 1.0f : 0.0f;

		int numHats = SDL_JoystickNumHats(s_current_joystick);
		if (numHats > 0) {
			Uint8 hat = SDL_JoystickGetHat(s_current_joystick, 0);
			state.dpad_up = (hat & SDL_HAT_UP) != 0;
			state.dpad_down = (hat & SDL_HAT_DOWN) != 0;
			state.dpad_left = (hat & SDL_HAT_LEFT) != 0;
			state.dpad_right = (hat & SDL_HAT_RIGHT) != 0;
		}

		last_state = state;
		return true;
	}

	state.connected = false;
	active_device_name = "";
	last_state = state;
	return false;
}

static proc_handler_t *get_ptz_proc_handler()
{
	proc_handler_t *main_ph = obs_get_proc_handler();
	if (!main_ph)
		return nullptr;

	proc_handler_t *ptz_ph = nullptr;
	calldata_t cd;
	calldata_init(&cd);
	if (proc_handler_call(main_ph, "ptz_get_proc_handler", &cd)) {
		calldata_get_ptr(&cd, "return", &ptz_ph);
	}
	calldata_free(&cd);

	return ptz_ph ? ptz_ph : main_ph;
}

static void send_ptz_move(int device_id, float pan, float tilt, float zoom)
{
	proc_handler_t *ph = get_ptz_proc_handler();
	if (!ph)
		return;

	calldata_t cd;
	calldata_init(&cd);
	calldata_set_int(&cd, "device_id", device_id);
	calldata_set_float(&cd, "pan", pan);
	calldata_set_float(&cd, "tilt", tilt);
	calldata_set_float(&cd, "zoom", zoom);
	calldata_set_float(&cd, "focus", 0.0f);

	proc_handler_call(ph, "ptz_move_continuous", &cd);
	proc_handler_call(ph, "ptz_pantilt", &cd);
	calldata_free(&cd);
}

static void send_ptz_stop(int device_id)
{
	proc_handler_t *ph = get_ptz_proc_handler();
	if (!ph)
		return;

	calldata_t cd;
	calldata_init(&cd);
	calldata_set_int(&cd, "device_id", device_id);
	proc_handler_call(ph, "ptz_stop", &cd);
	calldata_free(&cd);
}

static void recall_ptz_preset(int camera_idx, int preset_num)
{
	blog(LOG_INFO, "[Gamepad PTZ] Chamando preset %d no dispositivo PTZ %d", preset_num, camera_idx);
	proc_handler_t *ph = get_ptz_proc_handler();
	if (!ph)
		return;

	calldata_t cd;

	// 1. Envio 1-based (ex: Preset 1 -> preset_id = 1)
	calldata_init(&cd);
	calldata_set_int(&cd, "device_id", camera_idx);
	calldata_set_int(&cd, "preset_id", preset_num);
	calldata_set_int(&cd, "preset", preset_num);
	calldata_set_int(&cd, "preset_num", preset_num);
	calldata_set_int(&cd, "camera_index", camera_idx);
	proc_handler_call(ph, "ptz_preset_recall", &cd);
	proc_handler_call(ph, "preset_recall", &cd);
	calldata_free(&cd);

	// 2. Envio 0-based (ex: Preset 1 -> preset_id = 0 para protocolos VISCA)
	if (preset_num > 0) {
		calldata_init(&cd);
		calldata_set_int(&cd, "device_id", camera_idx);
		calldata_set_int(&cd, "preset_id", preset_num - 1);
		calldata_set_int(&cd, "preset", preset_num - 1);
		calldata_set_int(&cd, "preset_num", preset_num - 1);
		calldata_set_int(&cd, "camera_index", camera_idx);
		proc_handler_call(ph, "ptz_preset_recall", &cd);
		proc_handler_call(ph, "preset_recall", &cd);
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
	bool stick_active = (std::abs(state.pan_axis) > 0.02f || std::abs(state.tilt_axis) > 0.02f ||
			     std::abs(state.zoom_axis) > 0.02f);

	if (stick_active) {
		is_manual_override = true;
		last_manual_activity_ns = now_ns;
	}

	uint32_t current_buttons = 0;
	if (state.btn_a) current_buttons |= (1 << 0);
	if (state.btn_b) current_buttons |= (1 << 1);
	if (state.btn_x) current_buttons |= (1 << 2);
	if (state.btn_y) current_buttons |= (1 << 3);
	if (state.btn_lb) current_buttons |= (1 << 4);
	if (state.btn_rb) current_buttons |= (1 << 5);
	if (state.btn_start) current_buttons |= (1 << 6);
	if (state.btn_back) current_buttons |= (1 << 7);
	if (state.btn_thumb_l) current_buttons |= (1 << 8);
	if (state.btn_thumb_r) current_buttons |= (1 << 9);
	if (state.dpad_up) current_buttons |= (1 << 10);
	if (state.dpad_down) current_buttons |= (1 << 11);
	if (state.dpad_left) current_buttons |= (1 << 12);
	if (state.dpad_right) current_buttons |= (1 << 13);
	state.buttons = current_buttons;

	uint32_t pressed_buttons = current_buttons & (~prev_buttons);
	prev_buttons = current_buttons;

	float curr_lt = state.trigger_left;
	float curr_rt = state.trigger_right;
	bool lt_pressed = (curr_lt > 0.5f && prev_lt <= 0.5f);
	prev_lt = curr_lt;
	prev_rt = curr_rt;

	if (state.btn_thumb_r || state.btn_start) {
		if (pressed_buttons & (1 << 9) || pressed_buttons & (1 << 6)) {
			is_manual_override = !is_manual_override;
			if (on_mode_toggle)
				on_mode_toggle(is_manual_override);
		}
	}

	bool is_rb_held = state.btn_rb;
	bool is_rt_held = (curr_rt > 0.4f || state.btn_rt);

	// 1. MESA DE CORTE NO D-PAD (1 a 12)
	if (pressed_buttons & (1 << 10)) { // D-Pad Up
		if (is_rt_held)
			switch_scene_by_name(scene_config.scene_rt_dpad_up, false);
		else if (is_rb_held)
			switch_scene_by_name(scene_config.scene_rb_dpad_up, false);
		else
			switch_scene_by_name(scene_config.scene_dpad_up, false);
	}
	if (pressed_buttons & (1 << 11)) { // D-Pad Down
		if (is_rt_held)
			switch_scene_by_name(scene_config.scene_rt_dpad_down, false);
		else if (is_rb_held)
			switch_scene_by_name(scene_config.scene_rb_dpad_down, false);
		else
			switch_scene_by_name(scene_config.scene_dpad_down, false);
	}
	if (pressed_buttons & (1 << 12)) { // D-Pad Left
		if (is_rt_held)
			switch_scene_by_name(scene_config.scene_rt_dpad_left, false);
		else if (is_rb_held)
			switch_scene_by_name(scene_config.scene_rb_dpad_left, false);
		else
			switch_scene_by_name(scene_config.scene_dpad_left, false);
	}
	if (pressed_buttons & (1 << 13)) { // D-Pad Right
		if (is_rt_held)
			switch_scene_by_name(scene_config.scene_rt_dpad_right, false);
		else if (is_rb_held)
			switch_scene_by_name(scene_config.scene_rb_dpad_right, false);
		else
			switch_scene_by_name(scene_config.scene_dpad_right, false);
	}

	// 2. CORTE E TRANSIÇÃO
	if (scene_config.cut_on_lb && (pressed_buttons & (1 << 4))) { // LB (Corte Seco)
		if (obs_frontend_get_main_window()) {
			obs_source_t *prev = obs_frontend_get_current_preview_scene();
			if (prev) {
				obs_frontend_set_current_scene(prev);
				obs_source_release(prev);
			}
		}
	}

	if (scene_config.trans_on_lt && lt_pressed) { // LT (Transição)
		if (obs_frontend_get_main_window()) {
			if (obs_frontend_preview_program_mode_active()) {
				obs_frontend_preview_program_trigger_transition();
			} else {
				obs_source_t *prev = obs_frontend_get_current_preview_scene();
				if (prev) {
					obs_frontend_set_current_scene(prev);
					obs_source_release(prev);
				}
			}
		}
	}

	// 3. PRESETS PTZ NOS BOTÕES A, B, X, Y (1 a 12)
	int preset_to_call = -1;
	if (pressed_buttons & (1 << 0)) { // A
		preset_to_call = is_rt_held ? 9 : (is_rb_held ? 5 : 1);
	} else if (pressed_buttons & (1 << 1)) { // B
		preset_to_call = is_rt_held ? 10 : (is_rb_held ? 6 : 2);
	} else if (pressed_buttons & (1 << 2)) { // X
		preset_to_call = is_rt_held ? 11 : (is_rb_held ? 7 : 3);
	} else if (pressed_buttons & (1 << 3)) { // Y
		preset_to_call = is_rt_held ? 12 : (is_rb_held ? 8 : 4);
	}

	int target_camera = get_obsptz_active_device_id();

	if (preset_to_call > 0) {
		recall_ptz_preset(target_camera, preset_to_call);
		if (on_preset)
			on_preset(target_camera, preset_to_call);
	}

	// 4. RETORNO AO RASTREAMENTO FACIAL APÓS 5 SEGUNDOS DE INATIVIDADE
	if (is_manual_override && !stick_active) {
		if (now_ns - last_manual_activity_ns > 5000000000ULL) {
			is_manual_override = false;
			if (on_mode_toggle)
				on_mode_toggle(is_manual_override);
		}
	}

	// 5. ENVIO DIRETO DE VELOCIDADE PTZ (Pan, Tilt, Zoom)
	static float s_last_sent_pan = 0.0f;
	static float s_last_sent_tilt = 0.0f;
	static float s_last_sent_zoom = 0.0f;
	static uint64_t s_last_send_time_ns = 0;

	bool is_moving = (std::abs(state.pan_axis) > 0.02f || std::abs(state.tilt_axis) > 0.02f || std::abs(state.zoom_axis) > 0.02f);
	bool was_moving = (std::abs(s_last_sent_pan) > 0.02f || std::abs(s_last_sent_tilt) > 0.02f || std::abs(s_last_sent_zoom) > 0.02f);

	if (is_moving) {
		if (now_ns - s_last_send_time_ns > 33000000ULL ||
		    std::abs(state.pan_axis - s_last_sent_pan) > 0.02f ||
		    std::abs(state.tilt_axis - s_last_sent_tilt) > 0.02f ||
		    std::abs(state.zoom_axis - s_last_sent_zoom) > 0.02f) {
			send_ptz_move(target_camera, state.pan_axis, state.tilt_axis, state.zoom_axis);
			s_last_sent_pan = state.pan_axis;
			s_last_sent_tilt = state.tilt_axis;
			s_last_sent_zoom = state.zoom_axis;
			s_last_send_time_ns = now_ns;
		}
	} else if (was_moving) {
		send_ptz_move(target_camera, 0.0f, 0.0f, 0.0f);
		send_ptz_stop(target_camera);
		s_last_sent_pan = 0.0f;
		s_last_sent_tilt = 0.0f;
		s_last_sent_zoom = 0.0f;
		s_last_send_time_ns = now_ns;
	}

	if (is_manual_override && on_speed) {
		int p_spd = (int)std::round(state.pan_axis * 100.0f);
		int t_spd = (int)std::round(state.tilt_axis * 100.0f);
		int z_spd = (int)std::round(state.zoom_axis * 100.0f);
		on_speed(target_camera, p_spd, t_spd, z_spd);
	}

	return true;
}
