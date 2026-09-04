#pragma once

#include <vector>
#include <deque>
#include "helper.hpp"
#include "ptz-slew-limiter.hpp"

struct face_tracker_ptz
{
	face_tracker_ptz()
		: context(nullptr),
		  known_width(0),
		  known_height(0),
		  rendered(false),
		  is_active(false),
		  scaler(nullptr),
		  scaler_buffer(nullptr),
		  face_found(false),
		  face_found_last(false),
		  ftm(nullptr),
		  track_z(0.25f),
		  track_x(0.0f),
		  track_y(0.0f),
		  kp_x(50.0f),
		  kp_y(50.0f),
		  kp_z(40.0f),
		  f_att_int(1.0f),
		  face_found_last_ns(0),
		  face_lost_preset_sent(0),
		  face_lost_preset_timeout_ms(5000),
		  face_lost_ptz_preset(-1),
		  face_lost_zoomout_timeout_ms(4000),
		  ptz_smoothness(1.0f),
		  last_ptz_cmd_sent_ns(0),
		  debug_faces(false),
		  debug_notrack(false),
		  debug_always_show(false),
		  debug_data_tracker(nullptr),
		  debug_data_error(nullptr),
		  debug_data_control(nullptr),
		  debug_data_tracker_last(nullptr),
		  debug_data_error_last(nullptr),
		  debug_data_control_last(nullptr),
		  ptz_type(nullptr),
		  is_paused(false),
		  hotkey_pause(OBS_INVALID_HOTKEY_PAIR_ID),
		  hotkey_reset(OBS_INVALID_HOTKEY_ID)
	{
		memset(u, 0, sizeof(u));
		memset(u_linear, 0, sizeof(u_linear));
		memset(ptz_query, 0, sizeof(ptz_query));
		memset(last_sent_u, 0, sizeof(last_sent_u));
		memset(&scaler_src_info, 0, sizeof(scaler_src_info));
		memset(&scaler_dst_info, 0, sizeof(scaler_dst_info));
	}

	obs_source_t *context;
	uint32_t known_width;
	uint32_t known_height;
	bool rendered;
	bool is_active;

	video_scaler_t *scaler;
	uint8_t *scaler_buffer;
	struct video_scale_info scaler_src_info;
	struct video_scale_info scaler_dst_info;

	f3 detect_err;
	bool face_found, face_found_last;

	class ft_manager_for_ftptz *ftm;

	float track_z, track_x, track_y;

	float kp_x, kp_y, kp_z;
	f3 ki;
	f3 klpf;
	f3 tlpf;
	f3 e_deadband, e_nonlinear; // deadband and nonlinear amount for error input
	f3 filter_int;
	f3 filter_lpf;
	float f_att_int;
	int u[3];
	float u_linear[3];
	float ptz_query[3];
	uint64_t face_found_last_ns;
	int face_lost_preset_sent;

	int face_lost_preset_timeout_ms;
	int face_lost_ptz_preset;
	int face_lost_zoomout_timeout_ms;

	PtzRampController ramp;
	float ptz_smoothness;
	uint64_t last_ptz_cmd_sent_ns;
	int last_sent_u[3];

	bool debug_faces;
	bool debug_notrack;
	bool debug_always_show;
	FILE *debug_data_tracker;
	FILE *debug_data_error;
	FILE *debug_data_control;
	char *debug_data_tracker_last;
	char *debug_data_error_last;
	char *debug_data_control_last;

	char *ptz_type;

	bool is_paused;
	obs_hotkey_pair_id hotkey_pause;
	obs_hotkey_id hotkey_reset;
};

