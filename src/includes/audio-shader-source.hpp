#pragma once

#include <obs-module.h>
#include <graphics/graphics.h>

#include <atomic>
#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct audio_shader_source {
	obs_source_t *self = nullptr;

	std::string audio_source_name;
	obs_weak_source_t *audio_weak = nullptr;

	std::atomic<bool> alive{true};
	std::atomic<uint32_t> audio_cb_inflight{0};

	std::mutex audio_mutex;
	std::vector<float> mono_ring;
	size_t mono_pos = 0;
	size_t mono_count = 0;
	float raw_level = 0.0f;
	float raw_peak = 0.0f;

	std::mutex render_mutex;

	uint32_t width = 1920;
	uint32_t height = 1080;
	bool use_obs_canvas = true;
	uint32_t logged_width = 0;
	uint32_t logged_height = 0;

	float react_db = -55.0f;
	float peak_db = -6.0f;
	float attack_ms = 25.0f;
	float release_ms = 180.0f;
	uint64_t last_ts_ns = 0;

	float level = 0.0f;
	float peak = 0.0f;
	float bass = 0.0f;
	float mid = 0.0f;
	float treble = 0.0f;
	std::array<float, 64> bands{};

	int fft_size = 2048;
	int band_count = 64;
	int sample_rate = 48000;

	std::string effect_path;
	gs_effect_t *effect = nullptr;
	std::string effect_error;
	bool reload_effect = true;
	bool render_logged_ok = false;
	bool render_logged_no_effect = false;
	bool render_logged_no_technique = false;

	// Generic user parameters. Effects can read them as option1..option8.
	std::array<float, 8> options{};
	// OBS stores colours as ABGR (R in bits 0-7). Values here must match source_defaults.
	std::array<uint32_t, 4> colors{0xFFFFFFu, 0xFFD200u, 0xBB509Du, 0xAC3CFFu};
};

extern "C" void register_audio_shader_source(void);