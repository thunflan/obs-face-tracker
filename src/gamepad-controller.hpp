#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

enum class VirtualAction {
	BtnA = 0,
	BtnB,
	BtnX,
	BtnY,
	BtnLB,
	BtnRB,
	BtnLT,
	BtnRT,
	DpadUp,
	DpadDown,
	DpadLeft,
	DpadRight,
	BtnStart,
	BtnBack,
	BtnThumbL,
	BtnThumbR,
	Count
};

static inline const char *get_action_name(VirtualAction act)
{
	switch (act) {
	case VirtualAction::BtnA: return "Botão A (Preset 1 / Seleção)";
	case VirtualAction::BtnB: return "Botão B (Preset 2 / Voltar)";
	case VirtualAction::BtnX: return "Botão X (Preset 3)";
	case VirtualAction::BtnY: return "Botão Y (Preset 4)";
	case VirtualAction::BtnLB: return "Bumper LB (Corte Seco)";
	case VirtualAction::BtnRB: return "Bumper RB (Modificador 1)";
	case VirtualAction::BtnLT: return "Gatilho LT (Transição)";
	case VirtualAction::BtnRT: return "Gatilho RT (Modificador 2)";
	case VirtualAction::DpadUp: return "D-Pad Cima (Cena 1)";
	case VirtualAction::DpadDown: return "D-Pad Baixo (Cena 2)";
	case VirtualAction::DpadLeft: return "D-Pad Esquerda (Cena 3)";
	case VirtualAction::DpadRight: return "D-Pad Direita (Cena 4)";
	case VirtualAction::BtnStart: return "Menu / Start (Modo Manual/Auto)";
	case VirtualAction::BtnBack: return "View / Back / Select";
	case VirtualAction::BtnThumbL: return "Analógico L3 (Clique Esquerdo)";
	case VirtualAction::BtnThumbR: return "Analógico R3 (Clique Direito)";
	default: return "Desconhecido";
	}
}

enum class BindingType {
	None = 0,
	SdlButton,
	SdlHat,
	SdlAxis
};

struct InputBinding {
	BindingType type;
	int index;
	int param; // Para Hat: bitmask (1=Up, 2=Right, 4=Down, 8=Left). Para Eixo: +1 ou -1
	std::string display_name;

	InputBinding() : type(BindingType::None), index(-1), param(0), display_name("Não Mapeado") {}
	InputBinding(BindingType t, int idx, int p = 0, const std::string &disp = "")
		: type(t), index(idx), param(p), display_name(disp) {}
};

struct AxisBinding {
	int axis_index;
	bool inverted;
	std::string display_name;

	AxisBinding() : axis_index(-1), inverted(false), display_name("Padrão") {}
	AxisBinding(int idx, bool inv = false, const std::string &disp = "")
		: axis_index(idx), inverted(inv), display_name(disp) {}
};

struct GamepadCustomProfile {
	std::string name;
	std::string device_guid;
	bool is_custom;
	InputBinding bindings[(int)VirtualAction::Count];
	AxisBinding axis_pan;
	AxisBinding axis_tilt;
	AxisBinding axis_zoom;

	GamepadCustomProfile() : is_custom(false) {}
};

struct GamepadState {
	bool connected;
	float pan_axis;   // -1.0f a +1.0f
	float tilt_axis;  // -1.0f a +1.0f
	float zoom_axis;  // -1.0f (zoom out) a +1.0f (zoom in)
	float trigger_left;  // 0.0f a 1.0f (LT)
	float trigger_right; // 0.0f a 1.0f (RT)
	uint32_t buttons;
	bool btn_a;
	bool btn_b;
	bool btn_x;
	bool btn_y;
	bool btn_lb;
	bool btn_rb;
	bool btn_lt;
	bool btn_rt;
	bool dpad_up;
	bool dpad_down;
	bool dpad_left;
	bool dpad_right;
	bool btn_start;
	bool btn_back;
	bool btn_thumb_l;
	bool btn_thumb_r;
	bool manual_active;
	int active_camera_index;
	std::string last_raw_input_desc;
};

struct GamepadSceneConfig {
	std::string scene_dpad_up;    // Cena 1 (D-Pad Cima)
	std::string scene_dpad_down;  // Cena 2 (D-Pad Baixo)
	std::string scene_dpad_left;  // Cena 3 (D-Pad Esquerda)
	std::string scene_dpad_right; // Cena 4 (D-Pad Direita)

	std::string scene_rb_dpad_up;    // Cena 5 (RB + D-Pad Cima)
	std::string scene_rb_dpad_down;  // Cena 6 (RB + D-Pad Baixo)
	std::string scene_rb_dpad_left;  // Cena 7 (RB + D-Pad Esquerda)
	std::string scene_rb_dpad_right; // Cena 8 (RB + D-Pad Direita)

	std::string scene_rt_dpad_up;    // Cena 9 (RT + D-Pad Cima)
	std::string scene_rt_dpad_down;  // Cena 10 (RT + D-Pad Baixo)
	std::string scene_rt_dpad_left;  // Cena 11 (RT + D-Pad Esquerda)
	std::string scene_rt_dpad_right; // Cena 12 (RT + D-Pad Direita)

	bool cut_on_lb;   // Corte Seco no LB
	bool trans_on_lt; // Transição Suave no LT
};

struct ControllerDeviceInfo {
	std::string id;        // "auto", "sdl_0", "sdl_1", etc.
	std::string name;      // Nome do controle tal como o Windows usa (ex: "Wireless Controller")
	std::string guid;      // GUID identificador único de hardware do controle
	bool is_gamecontroller;
	int index;
};

class GamepadController {
public:
	typedef std::function<void(int camera_id, int pan_speed, int tilt_speed, int zoom_speed)> SpeedCallback;
	typedef std::function<void(int camera_id, int preset_num)> PresetCallback;
	typedef std::function<void(int new_camera_id)> CameraChangeCallback;
	typedef std::function<void(bool manual_mode)> ModeToggleCallback;

private:
	bool enabled;
	int active_camera;
	float deadzone;
	float sensitivity;
	bool is_manual_override;
	uint64_t last_manual_activity_ns;
	uint32_t prev_buttons;
	float prev_lt;
	float prev_rt;

	float curve_gamma;
	float min_speed;
	float max_speed;
	float zoom_speed_mult;

	SpeedCallback on_speed;
	PresetCallback on_preset;
	CameraChangeCallback on_camera_change;
	ModeToggleCallback on_mode_toggle;

	GamepadSceneConfig scene_config;
	GamepadState last_state;

	std::string selected_device_id;
	std::string active_device_name;
	std::string active_device_guid;

	// Perfis e Mapeamento
	std::vector<GamepadCustomProfile> profiles;
	std::string active_profile_name;
	GamepadCustomProfile default_auto_profile;

	// Modo de Escuta para Rebind / Wizard
	bool is_listening_input;
	VirtualAction listening_action;
	std::function<void(VirtualAction, const InputBinding &)> on_bound_callback;
	std::string last_raw_input_desc;

public:
	GamepadController();
	~GamepadController();

	static GamepadController &get_instance();

	void set_enabled(bool en) { enabled = en; }
	bool is_enabled() const { return enabled; }

	void set_deadzone(float dz) { deadzone = dz; }
	float get_deadzone() const { return deadzone; }

	void set_sensitivity(float sens) { sensitivity = sens; }
	float get_sensitivity() const { return sensitivity; }

	void set_curve_gamma(float g) { curve_gamma = g; }
	float get_curve_gamma() const { return curve_gamma; }

	void set_min_speed(float s) { min_speed = s; }
	float get_min_speed() const { return min_speed; }

	void set_max_speed(float s) { max_speed = s; }
	float get_max_speed() const { return max_speed; }

	void set_zoom_speed_mult(float z) { zoom_speed_mult = z; }
	float get_zoom_speed_mult() const { return zoom_speed_mult; }

	int get_obsptz_active_device_id();
	std::string get_obsptz_active_device_name();
	void set_active_camera(int cam) { active_camera = cam; }
	int get_active_camera() const { return active_camera; }

	void set_speed_callback(SpeedCallback cb) { on_speed = cb; }
	void set_preset_callback(PresetCallback cb) { on_preset = cb; }
	void set_camera_callback(CameraChangeCallback cb) { on_camera_change = cb; }
	void set_mode_callback(ModeToggleCallback cb) { on_mode_toggle = cb; }

	GamepadSceneConfig &get_scene_config() { return scene_config; }
	const GamepadSceneConfig &get_scene_config() const { return scene_config; }
	void set_scene_config(const GamepadSceneConfig &cfg) { scene_config = cfg; }

	// Dispositivos e Dropdown de seleção
	std::vector<ControllerDeviceInfo> get_available_devices();
	void set_selected_device(const std::string &id) { selected_device_id = id; }
	std::string get_selected_device() const { return selected_device_id; }
	std::string get_active_device_name() const { return active_device_name; }
	std::string get_active_device_guid() const { return active_device_guid; }

	// Modo de Escuta para Rebind / Wizard
	typedef std::function<void(VirtualAction action, const InputBinding &binding)> OnInputBoundCallback;
	void start_listening(VirtualAction action, OnInputBoundCallback cb);
	void cancel_listening();
	bool is_listening() const { return is_listening_input; }
	VirtualAction get_listening_action() const { return listening_action; }
	std::string get_last_raw_input() const { return last_raw_input_desc; }

	// Gerenciamento de Perfis
	std::vector<GamepadCustomProfile> &get_profiles() { return profiles; }
	const std::vector<GamepadCustomProfile> &get_profiles() const { return profiles; }
	GamepadCustomProfile *find_profile(const std::string &name);
	GamepadCustomProfile &get_active_profile();
	std::string get_active_profile_name() const { return active_profile_name; }
	void set_active_profile(const std::string &name);
	void add_or_update_profile(const GamepadCustomProfile &prof);
	void delete_profile(const std::string &name);

	static GamepadCustomProfile create_auto_profile();
	static GamepadCustomProfile create_default_xbox_profile();
	static GamepadCustomProfile create_default_playstation_profile();

	void save_profiles(struct obs_data *props);
	void load_profiles(struct obs_data *props);

	// Lê o estado atual do controle e dispara as ações necessárias
	bool tick(float dt, GamepadState &state);
	bool poll_state(GamepadState &state);

	const GamepadState &get_last_state() const { return last_state; }

	bool is_manual() const { return is_manual_override; }
	void set_manual(bool manual) { is_manual_override = manual; }

	// Verifica se o plugin PTZ está instalado e disponível no OBS
	bool is_ptz_available();
};
