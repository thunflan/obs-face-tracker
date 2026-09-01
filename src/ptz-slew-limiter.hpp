#pragma once
#include <algorithm>
#include <cmath>

class PtzSlewLimiter {
	float current_val;
	float max_accel; // Unidades por segundo
	bool has_init;

public:
	PtzSlewLimiter(float max_accel = 15.0f) : current_val(0.0f), max_accel(max_accel), has_init(false) {}

	void set_max_accel(float accel) { max_accel = std::max(accel, 0.1f); }

	void reset() {
		current_val = 0.0f;
		has_init = false;
	}

	float step(float target, float dt) {
		if (!has_init) {
			current_val = target;
			has_init = true;
			return current_val;
		}

		if (dt <= 0.0f)
			dt = 1.0f / 60.0f;

		float max_delta = max_accel * dt;
		float diff = target - current_val;

		if (std::abs(diff) <= max_delta) {
			current_val = target;
		} else if (diff > 0.0f) {
			current_val += max_delta;
		} else {
			current_val -= max_delta;
		}

		return current_val;
	}

	float get() const { return current_val; }
};

struct PtzRampController {
	PtzSlewLimiter pan_limiter;
	PtzSlewLimiter tilt_limiter;
	PtzSlewLimiter zoom_limiter;

	PtzRampController() : pan_limiter(18.0f), tilt_limiter(14.0f), zoom_limiter(8.0f) {}

	void set_smoothness(float smoothness_level) {
		// smoothness_level: 0.1 (muito suave / rampa longa) até 2.0 (muito rápido)
		smoothness_level = std::clamp(smoothness_level, 0.1f, 3.0f);
		pan_limiter.set_max_accel(16.0f * smoothness_level);
		tilt_limiter.set_max_accel(12.0f * smoothness_level);
		zoom_limiter.set_max_accel(8.0f * smoothness_level);
	}

	void reset() {
		pan_limiter.reset();
		tilt_limiter.reset();
		zoom_limiter.reset();
	}

	void process(float target_pan, float target_tilt, float target_zoom, float dt,
		     float &out_pan, float &out_tilt, float &out_zoom) {
		out_pan = pan_limiter.step(target_pan, dt);
		out_tilt = tilt_limiter.step(target_tilt, dt);
		out_zoom = zoom_limiter.step(target_zoom, dt);
	}
};
