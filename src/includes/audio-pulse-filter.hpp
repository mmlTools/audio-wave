#pragma once

#include <obs-module.h>
#include <graphics/graphics.h>

#include <cstdint>
#include <mutex>
#include <string>

enum class pulse_band : int {
	level = 0,
	peak = 1,
	bass = 2,
	mid = 3,
	treble = 4,
};

enum class pulse_mode : int {
	follow = 0,
	beat = 1,
};

enum class pulse_anchor : int {
	center = 0,
	bottom = 1,
	top = 2,
};

struct audio_pulse_filter {
	obs_source_t *self = nullptr;

	// Empty name means "use the filtered source's own audio".
	std::string audio_source_name;
	obs_weak_source_t *audio_weak = nullptr;
	float retry_attach_s = 0.0f;

	// Guards settings and pulse state shared between update (UI thread) and tick/render (graphics thread).
	std::mutex state_mutex;

	// Written by the audio thread, consumed once per video tick.
	std::mutex audio_mutex;
	float lp_bass = 0.0f;
	float lp_mid_low = 0.0f;
	float lp_mid_high = 0.0f;
	double acc_level_sq = 0.0;
	double acc_bass_sq = 0.0;
	double acc_mid_sq = 0.0;
	double acc_treble_sq = 0.0;
	float acc_peak = 0.0f;
	size_t acc_frames = 0;
	float alpha_bass = 0.0f;
	float alpha_mid_low = 0.0f;
	float alpha_mid_high = 0.0f;

	pulse_band band = pulse_band::level;
	pulse_mode mode = pulse_mode::follow;
	pulse_anchor anchor = pulse_anchor::center;
	float react_db = -45.0f;
	float peak_db = -6.0f;
	float attack_ms = 15.0f;
	float release_ms = 220.0f;
	float beat_sensitivity = 0.35f;
	float beat_cooldown_ms = 120.0f;

	float scale_power = 0.15f;
	bool shrink = false;
	float brightness_power = 0.0f;
	float opacity_floor = 1.0f;

	float target = 0.0f;
	float envelope = 0.0f;
	float beat_average = 0.0f;
	float beat_cooldown_s = 0.0f;

	gs_effect_t *effect = nullptr;
};

extern "C" void register_audio_pulse_filter(void);
