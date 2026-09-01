#pragma once
#include <cstdint>
#include <string>
#include <functional>

struct GamepadState {
	bool connected;
	float pan_axis;   // -1.0f a +1.0f
	float tilt_axis;  // -1.0f a +1.0f
	float zoom_axis;  // -1.0f (zoom out) a +1.0f (zoom in)
	bool manual_active;
	int active_camera_index;
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

	SpeedCallback on_speed;
	PresetCallback on_preset;
	CameraChangeCallback on_camera_change;
	ModeToggleCallback on_mode_toggle;

	void *xinput_dll;
	void *p_xinput_get_state;

public:
	GamepadController();
	~GamepadController();

	void set_enabled(bool en) { enabled = en; }
	bool is_enabled() const { return enabled; }

	void set_deadzone(float dz) { deadzone = dz; }
	void set_sensitivity(float sens) { sensitivity = sens; }
	void set_active_camera(int cam) { active_camera = cam; }
	int get_active_camera() const { return active_camera; }

	void set_speed_callback(SpeedCallback cb) { on_speed = cb; }
	void set_preset_callback(PresetCallback cb) { on_preset = cb; }
	void set_camera_callback(CameraChangeCallback cb) { on_camera_change = cb; }
	void set_mode_callback(ModeToggleCallback cb) { on_mode_toggle = cb; }

	// Chama a cada tick da engine (ex: 60 fps)
	bool tick(float dt, GamepadState &state);

	bool is_manual() const { return is_manual_override; }
	void set_manual(bool manual) { is_manual_override = manual; }
};
