#pragma once
#include <cstdint>
#include <string>
#include <functional>

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

	SpeedCallback on_speed;
	PresetCallback on_preset;
	CameraChangeCallback on_camera_change;
	ModeToggleCallback on_mode_toggle;

	GamepadSceneConfig scene_config;
	GamepadState last_state;

	void *xinput_dll;
	void *p_xinput_get_state;
	void *winmm_dll;
	void *p_joy_get_pos_ex;

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

	void set_active_camera(int cam) { active_camera = cam; }
	int get_active_camera() const { return active_camera; }

	void set_speed_callback(SpeedCallback cb) { on_speed = cb; }
	void set_preset_callback(PresetCallback cb) { on_preset = cb; }
	void set_camera_callback(CameraChangeCallback cb) { on_camera_change = cb; }
	void set_mode_callback(ModeToggleCallback cb) { on_mode_toggle = cb; }

	GamepadSceneConfig &get_scene_config() { return scene_config; }
	const GamepadSceneConfig &get_scene_config() const { return scene_config; }
	void set_scene_config(const GamepadSceneConfig &cfg) { scene_config = cfg; }

	// Lê o estado atual do controle e dispara as ações necessárias
	bool tick(float dt, GamepadState &state);
	bool poll_state(GamepadState &state);

	const GamepadState &get_last_state() const { return last_state; }

	bool is_manual() const { return is_manual_override; }
	void set_manual(bool manual) { is_manual_override = manual; }
};

