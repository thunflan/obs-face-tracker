#include "gamepad-controller.hpp"
#include <util/platform.h>
#include <obs-frontend-api.h>
#include <obs.h>
#include <cmath>
#include <algorithm>
#include <vector>
#include <string>
#include <SDL.h>

#include <QApplication>
#include <QMainWindow>
#include <QAbstractItemView>
#include <QItemSelectionModel>
#include <QModelIndex>
#include <QVariant>

static bool s_sdl_initialized = false;
static SDL_GameController *s_current_controller = nullptr;
static SDL_Joystick *s_current_joystick = nullptr;
static int s_current_opened_index = -1;

// Base de dados comunitária para controles populares no Windows (Bluetooth / DirectInput)
static const char *s_builtin_sdl_mappings[] = {
	// Xbox Wireless Controller Bluetooth (Windows)
	"030000005e040000120b000000007801,Xbox Wireless Controller,a:b0,b:b1,back:b10,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,leftshoulder:b6,leftstick:b13,lefttrigger:a5,leftx:a0,lefty:a1,rightshoulder:b7,rightstick:b14,righttrigger:a4,rightx:a2,righty:a3,start:b11,x:b2,y:b3,platform:Windows,",
	"030000005e040000130b000000007801,Xbox Series X Controller,a:b0,b:b1,back:b10,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,leftshoulder:b6,leftstick:b13,lefttrigger:a5,leftx:a0,lefty:a1,rightshoulder:b7,rightstick:b14,righttrigger:a4,rightx:a2,righty:a3,start:b11,x:b2,y:b3,platform:Windows,",
	"030000005e040000200b000000007801,Xbox Wireless Controller,a:b0,b:b1,back:b10,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,leftshoulder:b6,leftstick:b13,lefttrigger:a5,leftx:a0,lefty:a1,rightshoulder:b7,rightstick:b14,righttrigger:a4,rightx:a2,righty:a3,start:b11,x:b2,y:b3,platform:Windows,",
	"030000005e040000e002000000007801,Xbox Wireless Controller,a:b0,b:b1,back:b6,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b10,leftshoulder:b4,leftstick:b8,lefttrigger:a2,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b9,righttrigger:a5,rightx:a3,righty:a4,start:b7,x:b2,y:b3,platform:Windows,",
	"030000005e040000fd02000000007801,Xbox One Controller,a:b0,b:b1,back:b6,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b10,leftshoulder:b4,leftstick:b8,lefttrigger:a2,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b9,righttrigger:a5,rightx:a3,righty:a4,start:b7,x:b2,y:b3,platform:Windows,",
	// PS4 / PS5 DualShock / DualSense
	"030000004c050000c405000000007801,PS4 Controller,a:b1,b:b2,back:b8,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,leftshoulder:b4,leftstick:b10,lefttrigger:a3,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b11,righttrigger:a4,rightx:a2,righty:a5,start:b9,x:b0,y:b3,platform:Windows,",
	"030000004c050000e60c000000007801,PS5 Controller,a:b1,b:b2,back:b8,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,leftshoulder:b4,leftstick:b10,lefttrigger:a3,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b11,righttrigger:a4,rightx:a2,righty:a5,start:b9,x:b0,y:b3,platform:Windows,",
	nullptr
};

static void ensure_sdl_init()
{
	if (!s_sdl_initialized) {
		SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
		SDL_SetHint(SDL_HINT_AUTO_UPDATE_JOYSTICKS, "1");
		if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) == 0) {
			s_sdl_initialized = true;
			for (int i = 0; s_builtin_sdl_mappings[i] != nullptr; i++) {
				SDL_GameControllerAddMapping(s_builtin_sdl_mappings[i]);
			}
			blog(LOG_INFO, "[Gamepad SDL2] Subsystem inicializado e base de dados de mapeamento carregada.");
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
	  active_device_name(""),
	  active_device_guid(""),
	  active_profile_name("⚡ Automático (Padrão)"),
	  is_listening_input(false),
	  listening_action(VirtualAction::BtnA),
	  last_raw_input_desc("Nenhum")
{
	scene_config.cut_on_lb = true;
	scene_config.trans_on_lt = true;

	profiles.push_back(create_auto_profile());
	profiles.push_back(create_default_xbox_profile());
	profiles.push_back(create_default_playstation_profile());

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

struct ObsPtzDeviceMap {
	int id;
	std::string name;
};

static std::vector<ObsPtzDeviceMap> load_obsptz_devices_from_config()
{
	std::vector<ObsPtzDeviceMap> list;
	const char *appdata = getenv("APPDATA");
	if (!appdata) return list;

	std::string cfg_path = std::string(appdata) + "\\obs-studio\\plugin_config\\obs-ptz\\config.json";
	FILE *fp = os_fopen(cfg_path.c_str(), "rb");
	if (!fp) return list;

	fseek(fp, 0, SEEK_END);
	long len = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (len <= 0 || len > 131072) {
		fclose(fp);
		return list;
	}

	std::string content(len, '\0');
	size_t r = fread(&content[0], 1, len, fp);
	fclose(fp);
	if (r != (size_t)len) return list;

	size_t dev_pos = content.find("\"devices\"");
	if (dev_pos == std::string::npos) return list;

	size_t cur = dev_pos;
	while (true) {
		size_t obj_start = content.find('{', cur);
		if (obj_start == std::string::npos) break;
		size_t obj_end = content.find('}', obj_start);
		if (obj_end == std::string::npos) break;

		std::string obj = content.substr(obj_start, obj_end - obj_start + 1);
		size_t id_pos = obj.find("\"id\"");
		if (id_pos != std::string::npos) {
			size_t col = obj.find(':', id_pos);
			if (col != std::string::npos) {
				int dev_id = std::atoi(obj.c_str() + col + 1);
				std::string dev_name = "Câmera " + std::to_string(dev_id);
				size_t name_pos = obj.find("\"name\"");
				if (name_pos != std::string::npos) {
					size_t q1 = obj.find('"', name_pos + 6);
					if (q1 != std::string::npos) {
						size_t q2 = obj.find('"', q1 + 1);
						if (q2 != std::string::npos) {
							dev_name = obj.substr(q1 + 1, q2 - q1 - 1);
						}
					}
				}
				list.push_back({dev_id, dev_name});
			}
		}
		cur = obj_end + 1;
	}
	return list;
}

static int s_active_ptz_id = 1;
static std::string s_active_ptz_name = "Câmera 1";

int GamepadController::get_obsptz_active_device_id()
{
	static uint64_t last_ui_check_ns = 0;
	uint64_t now_ns = os_gettime_ns();

	// Verifica a UI do Qt a cada 30ms (resposta em tempo real ao clique do usuário)
	if (now_ns - last_ui_check_ns > 30000000ULL) {
		last_ui_check_ns = now_ns;

		QMainWindow *main_win = (QMainWindow *)obs_frontend_get_main_window();
		QAbstractItemView *camView = nullptr;
		if (main_win) {
			camView = main_win->findChild<QAbstractItemView *>("cameraList");
		}
		if (!camView && qApp) {
			const auto widgets = qApp->allWidgets();
			for (QWidget *w : widgets) {
				if (w && w->objectName() == "cameraList") {
					camView = qobject_cast<QAbstractItemView *>(w);
					if (camView) break;
				}
			}
		}

		if (camView && camView->model()) {
			QModelIndex curr = camView->currentIndex();
			if (!curr.isValid() && camView->selectionModel()) {
				auto sel = camView->selectionModel()->selectedIndexes();
				if (!sel.isEmpty())
					curr = sel.first();
			}

			if (curr.isValid()) {
				int row = curr.row();
				QString itemText = camView->model()->data(curr, Qt::DisplayRole).toString();

				static std::vector<ObsPtzDeviceMap> s_devs = load_obsptz_devices_from_config();
				static uint64_t last_file_load = 0;
				if (now_ns - last_file_load > 2000000000ULL) { // recarrega lista do config a cada 2s
					last_file_load = now_ns;
					s_devs = load_obsptz_devices_from_config();
				}

				bool found = false;
				// 1. Tenta casar pelo nome da câmera exibida na lista
				if (!itemText.isEmpty()) {
					for (const auto &d : s_devs) {
						if (QString::fromUtf8(d.name.c_str()) == itemText) {
							s_active_ptz_id = d.id;
							s_active_ptz_name = d.name;
							found = true;
							break;
						}
					}
				}

				// 2. Tenta casar pelo índice da linha (row)
				if (!found && row >= 0 && row < (int)s_devs.size()) {
					s_active_ptz_id = s_devs[row].id;
					s_active_ptz_name = s_devs[row].name;
					found = true;
				}

				// 3. Fallback: UserRole se o modelo do PTZ armazenar o ID diretamente
				if (!found) {
					QVariant v = camView->model()->data(curr, Qt::UserRole);
					if (v.isValid() && v.toInt() > 0) {
						s_active_ptz_id = v.toInt();
						s_active_ptz_name = itemText.toUtf8().constData();
					} else {
						s_active_ptz_id = row + 1;
						s_active_ptz_name = "Câmera " + std::to_string(s_active_ptz_id);
					}
				}
			}
		}
	}
	return s_active_ptz_id;
}

std::string GamepadController::get_obsptz_active_device_name()
{
	get_obsptz_active_device_id();
	return s_active_ptz_name;
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
	autoDev.guid = "";
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

		char guidStr[64] = {0};
		SDL_JoystickGUID guid = SDL_JoystickGetDeviceGUID(i);
		SDL_JoystickGetGUIDString(guid, guidStr, sizeof(guidStr));
		dev.guid = guidStr;

		list.push_back(dev);
	}

	return list;
}

bool GamepadController::poll_state(GamepadState &state)
{
	memset(&state, 0, sizeof(GamepadState));
	state.active_camera_index = active_camera;
	state.manual_active = is_manual_override;
	state.last_raw_input_desc = last_raw_input_desc;

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
		active_device_guid = "";
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
		active_device_guid = "";
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

	SDL_Joystick *joy = s_current_controller ? SDL_GameControllerGetJoystick(s_current_controller) : s_current_joystick;
	if (joy) {
		char guidStr[64] = {0};
		SDL_JoystickGUID guid = SDL_JoystickGetGUID(joy);
		SDL_JoystickGetGUIDString(guid, guidStr, sizeof(guidStr));
		active_device_guid = guidStr;
	}

	// -------------------------------------------------------------
	// MODO DE ESCUTA (REBIND / WIZARD DE EMULADOR)
	// -------------------------------------------------------------
	if (is_listening_input && joy) {
		state.connected = true;

		// 1. Verifica botões físicos
		int numBtns = SDL_JoystickNumButtons(joy);
		for (int b = 0; b < numBtns; b++) {
			if (SDL_JoystickGetButton(joy, b) != 0) {
				InputBinding newBind(BindingType::SdlButton, b, 0, "Botão " + std::to_string(b));
				last_raw_input_desc = "Botão " + std::to_string(b);
				state.last_raw_input_desc = last_raw_input_desc;
				if (on_bound_callback) {
					auto cb = on_bound_callback;
					VirtualAction act = listening_action;
					is_listening_input = false;
					on_bound_callback = nullptr;
					cb(act, newBind);
				}
				last_state = state;
				return true;
			}
		}

		// 2. Verifica D-Pad / Hats
		int numHats = SDL_JoystickNumHats(joy);
		for (int h = 0; h < numHats; h++) {
			Uint8 hat = SDL_JoystickGetHat(joy, h);
			if (hat != SDL_HAT_CENTERED) {
				int dir = 0;
				std::string dirName = "";
				if (hat & SDL_HAT_UP) { dir = SDL_HAT_UP; dirName = "Cima"; }
				else if (hat & SDL_HAT_DOWN) { dir = SDL_HAT_DOWN; dirName = "Baixo"; }
				else if (hat & SDL_HAT_LEFT) { dir = SDL_HAT_LEFT; dirName = "Esquerda"; }
				else if (hat & SDL_HAT_RIGHT) { dir = SDL_HAT_RIGHT; dirName = "Direita"; }
				if (dir != 0) {
					InputBinding newBind(BindingType::SdlHat, h, dir, "D-Pad " + dirName);
					last_raw_input_desc = "Hat " + std::to_string(h) + " (" + dirName + ")";
					state.last_raw_input_desc = last_raw_input_desc;
					if (on_bound_callback) {
						auto cb = on_bound_callback;
						VirtualAction act = listening_action;
						is_listening_input = false;
						on_bound_callback = nullptr;
						cb(act, newBind);
					}
					last_state = state;
					return true;
				}
			}
		}

		// 3. Verifica Eixos (deslocamento forte para mapear gatilhos como botões)
		int numAxes = SDL_JoystickNumAxes(joy);
		for (int a = 0; a < numAxes; a++) {
			int16_t val = SDL_JoystickGetAxis(joy, a);
			if (val > 24000 || val < -24000) {
				int sign = (val > 0) ? 1 : -1;
				std::string axisDesc = "Eixo " + std::to_string(a) + (sign > 0 ? " (+)" : " (-)");
				InputBinding newBind(BindingType::SdlAxis, a, sign, axisDesc);
				last_raw_input_desc = axisDesc;
				state.last_raw_input_desc = last_raw_input_desc;
				if (on_bound_callback) {
					auto cb = on_bound_callback;
					VirtualAction act = listening_action;
					is_listening_input = false;
					on_bound_callback = nullptr;
					cb(act, newBind);
				}
				last_state = state;
				return true;
			}
		}

		state.last_raw_input_desc = "⏳ Aguardando pressionar botão...";
		last_state = state;
		return true;
	}

	// Sniffer de entrada bruta (exibição em tempo real no dock de teste)
	if (joy) {
		int numBtns = SDL_JoystickNumButtons(joy);
		for (int b = 0; b < numBtns; b++) {
			if (SDL_JoystickGetButton(joy, b) != 0) {
				last_raw_input_desc = "Botão Físico " + std::to_string(b);
				break;
			}
		}
		int numHats = SDL_JoystickNumHats(joy);
		for (int h = 0; h < numHats; h++) {
			Uint8 hat = SDL_JoystickGetHat(joy, h);
			if (hat != SDL_HAT_CENTERED) {
				std::string dirName = "";
				if (hat & SDL_HAT_UP) dirName = "Cima";
				else if (hat & SDL_HAT_DOWN) dirName = "Baixo";
				else if (hat & SDL_HAT_LEFT) dirName = "Esquerda";
				else if (hat & SDL_HAT_RIGHT) dirName = "Direita";
				last_raw_input_desc = "D-Pad " + dirName;
				break;
			}
		}
	}
	state.last_raw_input_desc = last_raw_input_desc;

	// -------------------------------------------------------------
	// LEITURA COM PERFIL PERSONALIZADO (CUSTOM REMAP)
	// -------------------------------------------------------------
	GamepadCustomProfile &prof = get_active_profile();
	if (prof.is_custom && joy) {
		state.connected = true;
		const char *jname = SDL_JoystickName(joy);
		active_device_name = (jname && *jname) ? jname : "Controle Personalizado";

		auto read_bind = [&](VirtualAction act) -> bool {
			const InputBinding &b = prof.bindings[(int)act];
			if (b.type == BindingType::SdlButton && b.index >= 0) {
				return SDL_JoystickGetButton(joy, b.index) != 0;
			} else if (b.type == BindingType::SdlHat && b.index >= 0) {
				return (SDL_JoystickGetHat(joy, b.index) & b.param) != 0;
			} else if (b.type == BindingType::SdlAxis && b.index >= 0) {
				int16_t ax = SDL_JoystickGetAxis(joy, b.index);
				return (b.param > 0) ? (ax > 16000) : (ax < -16000);
			}
			return false;
		};

		state.btn_a = read_bind(VirtualAction::BtnA);
		state.btn_b = read_bind(VirtualAction::BtnB);
		state.btn_x = read_bind(VirtualAction::BtnX);
		state.btn_y = read_bind(VirtualAction::BtnY);
		state.btn_lb = read_bind(VirtualAction::BtnLB);
		state.btn_rb = read_bind(VirtualAction::BtnRB);
		state.btn_lt = read_bind(VirtualAction::BtnLT);
		state.btn_rt = read_bind(VirtualAction::BtnRT);
		state.dpad_up = read_bind(VirtualAction::DpadUp);
		state.dpad_down = read_bind(VirtualAction::DpadDown);
		state.dpad_left = read_bind(VirtualAction::DpadLeft);
		state.dpad_right = read_bind(VirtualAction::DpadRight);
		state.btn_start = read_bind(VirtualAction::BtnStart);
		state.btn_back = read_bind(VirtualAction::BtnBack);
		state.btn_thumb_l = read_bind(VirtualAction::BtnThumbL);
		state.btn_thumb_r = read_bind(VirtualAction::BtnThumbR);

		int numAxes = SDL_JoystickNumAxes(joy);
		int pan_idx = prof.axis_pan.axis_index >= 0 ? prof.axis_pan.axis_index : 0;
		int tilt_idx = prof.axis_tilt.axis_index >= 0 ? prof.axis_tilt.axis_index : 1;
		int zoom_idx = prof.axis_zoom.axis_index >= 0 ? prof.axis_zoom.axis_index : 3;

		float raw_lx = (pan_idx < numAxes) ? (float)SDL_JoystickGetAxis(joy, pan_idx) / 32767.0f : 0.0f;
		float raw_ly = (tilt_idx < numAxes) ? -(float)SDL_JoystickGetAxis(joy, tilt_idx) / 32767.0f : 0.0f;
		if (prof.axis_pan.inverted) raw_lx = -raw_lx;
		if (prof.axis_tilt.inverted) raw_ly = -raw_ly;

		state.pan_axis = apply_progressive_curve(raw_lx, deadzone, curve_gamma, min_speed, max_speed * sensitivity);
		state.tilt_axis = apply_progressive_curve(raw_ly, deadzone, curve_gamma, min_speed, max_speed * sensitivity);

		float raw_ry = (zoom_idx < numAxes) ? -(float)SDL_JoystickGetAxis(joy, zoom_idx) / 32767.0f : 0.0f;
		if (prof.axis_zoom.inverted) raw_ry = -raw_ry;
		state.zoom_axis = apply_progressive_curve(raw_ry, deadzone, curve_gamma, min_speed, max_speed * zoom_speed_mult * sensitivity);

		state.trigger_left = state.btn_lt ? 1.0f : 0.0f;
		state.trigger_right = state.btn_rt ? 1.0f : 0.0f;

		last_state = state;
		return true;
	}

	// -------------------------------------------------------------
	// LEITURA PADRÃO AUTOMÁTICA (SDL_GameController / XInput)
	// -------------------------------------------------------------
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

		// Zoom no Analógico Direito (Stick Direito Y)
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

	// Fallback via SDL_Joystick
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
	active_device_guid = "";
	last_state = state;
	return false;
}

bool GamepadController::is_ptz_available()
{
	proc_handler_t *main_ph = obs_get_proc_handler();
	if (!main_ph)
		return false;

	// 1. Verifica se o plugin dedicado obs-ptz-tracker está instalado
	calldata_t cd;
	calldata_init(&cd);
	bool is_installed = false;
	if (proc_handler_call(main_ph, "ptz_tracker_is_installed", &cd)) {
		calldata_get_bool(&cd, "installed", &is_installed);
		calldata_free(&cd);
		if (is_installed)
			return true;
	} else {
		calldata_free(&cd);
	}

	// 2. Verifica se o plugin obs-ptz oficial está registrado
	calldata_init(&cd);
	proc_handler_t *ptz_ph = nullptr;
	if (proc_handler_call(main_ph, "ptz_get_proc_handler", &cd)) {
		calldata_get_ptr(&cd, "return", &ptz_ph);
		calldata_free(&cd);
		if (ptz_ph != nullptr)
			return true;
	} else {
		calldata_free(&cd);
	}

	// 3. Verifica se os procedimentos de movimento PTZ existem
	calldata_init(&cd);
	calldata_set_int(&cd, "device_id", 0);
	calldata_set_float(&cd, "pan", 0.0f);
	calldata_set_float(&cd, "tilt", 0.0f);
	calldata_set_float(&cd, "zoom", 0.0f);
	if (proc_handler_call(main_ph, "ptz_move_continuous", &cd) ||
	    proc_handler_call(main_ph, "ptz_pantilt", &cd)) {
		calldata_free(&cd);
		return true;
	}
	calldata_free(&cd);

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
	if (!GamepadController::get_instance().is_ptz_available())
		return;

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
	if (!GamepadController::get_instance().is_ptz_available())
		return;

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
	if (!GamepadController::get_instance().is_ptz_available()) {
		blog(LOG_WARNING, "[Gamepad] Preset %d ignorado: o plugin PTZ precisa estar instalado!", preset_num);
		return;
	}

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

	static int s_prev_target_camera = 1;
	int target_camera = get_obsptz_active_device_id();
	if (target_camera != s_prev_target_camera) {
		send_ptz_stop(s_prev_target_camera);
		s_prev_target_camera = target_camera;
		blog(LOG_INFO, "[Gamepad PTZ] Câmera selecionada alternada para ID %d (%s)",
		     target_camera, get_obsptz_active_device_name().c_str());
	}

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

void GamepadController::start_listening(VirtualAction action, OnInputBoundCallback cb)
{
	listening_action = action;
	on_bound_callback = cb;
	is_listening_input = true;
	blog(LOG_INFO, "[Gamepad Remap] Aguardando entrada para a ação %d (%s)...",
	     (int)action, get_action_name(action));
}

void GamepadController::cancel_listening()
{
	is_listening_input = false;
	on_bound_callback = nullptr;
}

GamepadCustomProfile *GamepadController::find_profile(const std::string &name)
{
	for (auto &p : profiles) {
		if (p.name == name)
			return &p;
	}
	return nullptr;
}

GamepadCustomProfile &GamepadController::get_active_profile()
{
	for (auto &p : profiles) {
		if (p.name == active_profile_name)
			return p;
	}
	if (!profiles.empty())
		return profiles[0];

	static GamepadCustomProfile fallback = create_auto_profile();
	return fallback;
}

void GamepadController::set_active_profile(const std::string &name)
{
	active_profile_name = name;
	blog(LOG_INFO, "[Gamepad Profile] Perfil ativo alterado para: %s", name.c_str());
}

void GamepadController::add_or_update_profile(const GamepadCustomProfile &prof)
{
	for (auto &p : profiles) {
		if (p.name == prof.name) {
			p = prof;
			return;
		}
	}
	profiles.push_back(prof);
}

void GamepadController::delete_profile(const std::string &name)
{
	if (name == "⚡ Automático (Padrão)" || name == "Xbox Controller (Padrão)" || name == "PlayStation (Padrão)")
		return;

	for (auto it = profiles.begin(); it != profiles.end(); ++it) {
		if (it->name == name) {
			profiles.erase(it);
			if (active_profile_name == name) {
				active_profile_name = "⚡ Automático (Padrão)";
			}
			return;
		}
	}
}

GamepadCustomProfile GamepadController::create_auto_profile()
{
	GamepadCustomProfile p;
	p.name = "⚡ Automático (Padrão)";
	p.device_guid = "";
	p.is_custom = false;
	return p;
}

GamepadCustomProfile GamepadController::create_default_xbox_profile()
{
	GamepadCustomProfile p;
	p.name = "Xbox Controller (Padrão)";
	p.device_guid = "";
	p.is_custom = true;

	p.bindings[(int)VirtualAction::BtnA] = {BindingType::SdlButton, 0, 0, "Botão 0 (A)"};
	p.bindings[(int)VirtualAction::BtnB] = {BindingType::SdlButton, 1, 0, "Botão 1 (B)"};
	p.bindings[(int)VirtualAction::BtnX] = {BindingType::SdlButton, 2, 0, "Botão 2 (X)"};
	p.bindings[(int)VirtualAction::BtnY] = {BindingType::SdlButton, 3, 0, "Botão 3 (Y)"};
	p.bindings[(int)VirtualAction::BtnLB] = {BindingType::SdlButton, 4, 0, "Botão 4 (LB)"};
	p.bindings[(int)VirtualAction::BtnRB] = {BindingType::SdlButton, 5, 0, "Botão 5 (RB)"};
	p.bindings[(int)VirtualAction::BtnBack] = {BindingType::SdlButton, 6, 0, "Botão 6 (View/Back)"};
	p.bindings[(int)VirtualAction::BtnStart] = {BindingType::SdlButton, 7, 0, "Botão 7 (Menu/Start)"};
	p.bindings[(int)VirtualAction::BtnThumbL] = {BindingType::SdlButton, 8, 0, "Botão 8 (L3/LS)"};
	p.bindings[(int)VirtualAction::BtnThumbR] = {BindingType::SdlButton, 9, 0, "Botão 9 (R3/RS)"};

	p.bindings[(int)VirtualAction::DpadUp] = {BindingType::SdlHat, 0, SDL_HAT_UP, "D-Pad Cima (Hat 0)"};
	p.bindings[(int)VirtualAction::DpadDown] = {BindingType::SdlHat, 0, SDL_HAT_DOWN, "D-Pad Baixo (Hat 0)"};
	p.bindings[(int)VirtualAction::DpadLeft] = {BindingType::SdlHat, 0, SDL_HAT_LEFT, "D-Pad Esquerda (Hat 0)"};
	p.bindings[(int)VirtualAction::DpadRight] = {BindingType::SdlHat, 0, SDL_HAT_RIGHT, "D-Pad Direita (Hat 0)"};

	p.bindings[(int)VirtualAction::BtnLT] = {BindingType::SdlAxis, 2, 1, "Eixo 2 (+) [LT]"};
	p.bindings[(int)VirtualAction::BtnRT] = {BindingType::SdlAxis, 5, 1, "Eixo 5 (+) [RT]"};

	p.axis_pan = {0, false, "Analógico Esquerdo X"};
	p.axis_tilt = {1, false, "Analógico Esquerdo Y"};
	p.axis_zoom = {3, false, "Analógico Direito Y"};

	return p;
}

GamepadCustomProfile GamepadController::create_default_playstation_profile()
{
	GamepadCustomProfile p;
	p.name = "PlayStation (Padrão)";
	p.device_guid = "";
	p.is_custom = true;

	p.bindings[(int)VirtualAction::BtnA] = {BindingType::SdlButton, 1, 0, "Botão 1 (X)"};
	p.bindings[(int)VirtualAction::BtnB] = {BindingType::SdlButton, 2, 0, "Botão 2 (Círculo)"};
	p.bindings[(int)VirtualAction::BtnX] = {BindingType::SdlButton, 0, 0, "Botão 0 (Quadrado)"};
	p.bindings[(int)VirtualAction::BtnY] = {BindingType::SdlButton, 3, 0, "Botão 3 (Triângulo)"};
	p.bindings[(int)VirtualAction::BtnLB] = {BindingType::SdlButton, 4, 0, "Botão 4 (L1)"};
	p.bindings[(int)VirtualAction::BtnRB] = {BindingType::SdlButton, 5, 0, "Botão 5 (R1)"};
	p.bindings[(int)VirtualAction::BtnBack] = {BindingType::SdlButton, 8, 0, "Botão 8 (Share)"};
	p.bindings[(int)VirtualAction::BtnStart] = {BindingType::SdlButton, 9, 0, "Botão 9 (Options)"};
	p.bindings[(int)VirtualAction::BtnThumbL] = {BindingType::SdlButton, 10, 0, "Botão 10 (L3)"};
	p.bindings[(int)VirtualAction::BtnThumbR] = {BindingType::SdlButton, 11, 0, "Botão 11 (R3)"};

	p.bindings[(int)VirtualAction::DpadUp] = {BindingType::SdlHat, 0, SDL_HAT_UP, "D-Pad Cima (Hat 0)"};
	p.bindings[(int)VirtualAction::DpadDown] = {BindingType::SdlHat, 0, SDL_HAT_DOWN, "D-Pad Baixo (Hat 0)"};
	p.bindings[(int)VirtualAction::DpadLeft] = {BindingType::SdlHat, 0, SDL_HAT_LEFT, "D-Pad Esquerda (Hat 0)"};
	p.bindings[(int)VirtualAction::DpadRight] = {BindingType::SdlHat, 0, SDL_HAT_RIGHT, "D-Pad Direita (Hat 0)"};

	p.bindings[(int)VirtualAction::BtnLT] = {BindingType::SdlAxis, 3, 1, "Eixo 3 (+) [L2]"};
	p.bindings[(int)VirtualAction::BtnRT] = {BindingType::SdlAxis, 4, 1, "Eixo 4 (+) [R2]"};

	p.axis_pan = {0, false, "Analógico Esquerdo X"};
	p.axis_tilt = {1, false, "Analógico Esquerdo Y"};
	p.axis_zoom = {2, false, "Analógico Direito Y"};

	return p;
}

void GamepadController::save_profiles(obs_data_t *props)
{
	if (!props) return;

	obs_data_set_string(props, "gamepad_active_profile", active_profile_name.c_str());
	obs_data_set_string(props, "gamepad_selected_device", selected_device_id.c_str());

	obs_data_array_t *arr = obs_data_array_create();
	for (const auto &p : profiles) {
		obs_data_t *p_data = obs_data_create();
		obs_data_set_string(p_data, "name", p.name.c_str());
		obs_data_set_string(p_data, "guid", p.device_guid.c_str());
		obs_data_set_bool(p_data, "is_custom", p.is_custom);

		obs_data_array_t *bind_arr = obs_data_array_create();
		for (int i = 0; i < (int)VirtualAction::Count; i++) {
			obs_data_t *b_data = obs_data_create();
			const auto &b = p.bindings[i];
			obs_data_set_int(b_data, "action", i);
			obs_data_set_int(b_data, "type", (int)b.type);
			obs_data_set_int(b_data, "index", b.index);
			obs_data_set_int(b_data, "param", b.param);
			obs_data_set_string(b_data, "desc", b.display_name.c_str());
			obs_data_array_push_back(bind_arr, b_data);
			obs_data_release(b_data);
		}
		obs_data_set_array(p_data, "bindings", bind_arr);
		obs_data_array_release(bind_arr);

		obs_data_set_int(p_data, "axis_pan_idx", p.axis_pan.axis_index);
		obs_data_set_bool(p_data, "axis_pan_inv", p.axis_pan.inverted);
		obs_data_set_int(p_data, "axis_tilt_idx", p.axis_tilt.axis_index);
		obs_data_set_bool(p_data, "axis_tilt_inv", p.axis_tilt.inverted);
		obs_data_set_int(p_data, "axis_zoom_idx", p.axis_zoom.axis_index);
		obs_data_set_bool(p_data, "axis_zoom_inv", p.axis_zoom.inverted);

		obs_data_array_push_back(arr, p_data);
		obs_data_release(p_data);
	}

	obs_data_set_array(props, "gamepad_custom_profiles", arr);
	obs_data_array_release(arr);
}

void GamepadController::load_profiles(obs_data_t *props)
{
	if (!props) return;

	const char *act_prof = obs_data_get_string(props, "gamepad_active_profile");
	if (act_prof && *act_prof) {
		active_profile_name = act_prof;
	}

	const char *sel_dev = obs_data_get_string(props, "gamepad_selected_device");
	if (sel_dev && *sel_dev) {
		selected_device_id = sel_dev;
	}

	obs_data_array_t *arr = obs_data_get_array(props, "gamepad_custom_profiles");
	if (arr) {
		size_t count = obs_data_array_count(arr);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *p_data = obs_data_array_item(arr, i);
			if (!p_data) continue;

			GamepadCustomProfile p;
			p.name = obs_data_get_string(p_data, "name");
			p.device_guid = obs_data_get_string(p_data, "guid");
			p.is_custom = obs_data_get_bool(p_data, "is_custom");

			obs_data_array_t *bind_arr = obs_data_get_array(p_data, "bindings");
			if (bind_arr) {
				size_t b_count = obs_data_array_count(bind_arr);
				for (size_t j = 0; j < b_count; j++) {
					obs_data_t *b_data = obs_data_array_item(bind_arr, j);
					if (!b_data) continue;

					int act = (int)obs_data_get_int(b_data, "action");
					if (act >= 0 && act < (int)VirtualAction::Count) {
						p.bindings[act].type = (BindingType)obs_data_get_int(b_data, "type");
						p.bindings[act].index = (int)obs_data_get_int(b_data, "index");
						p.bindings[act].param = (int)obs_data_get_int(b_data, "param");
						const char *dsc = obs_data_get_string(b_data, "desc");
						p.bindings[act].display_name = dsc ? dsc : "";
					}
					obs_data_release(b_data);
				}
				obs_data_array_release(bind_arr);
			}

			p.axis_pan.axis_index = (int)obs_data_get_int(p_data, "axis_pan_idx");
			p.axis_pan.inverted = obs_data_get_bool(p_data, "axis_pan_inv");
			p.axis_tilt.axis_index = (int)obs_data_get_int(p_data, "axis_tilt_idx");
			p.axis_tilt.inverted = obs_data_get_bool(p_data, "axis_tilt_inv");
			p.axis_zoom.axis_index = (int)obs_data_get_int(p_data, "axis_zoom_idx");
			p.axis_zoom.inverted = obs_data_get_bool(p_data, "axis_zoom_inv");

			add_or_update_profile(p);
			obs_data_release(p_data);
		}
		obs_data_array_release(arr);
	}
}


