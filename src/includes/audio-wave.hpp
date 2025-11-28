#pragma once

#include <obs-module.h>
#include <graphics/graphics.h>

#include <mutex>
#include <vector>
#include <string>
#include <cstdint>

enum audio_wave_shape_type {
	AUDIO_WAVE_SHAPE_LINE = 0,
	AUDIO_WAVE_SHAPE_RECT = 1,
	AUDIO_WAVE_SHAPE_CIRCLE = 2,
	AUDIO_WAVE_SHAPE_HEX = 3,
	AUDIO_WAVE_SHAPE_STAR = 4,
	AUDIO_WAVE_SHAPE_TRIANGLE = 5,
	AUDIO_WAVE_SHAPE_DIAMOND = 6,
};

enum audio_wave_style_type {
	AUDIO_WAVE_STYLE_WAVE_LINE = 0,
	AUDIO_WAVE_STYLE_WAVE_BARS = 1,
	AUDIO_WAVE_STYLE_WAVE_LINEAR_SMOOTH = 2,
	AUDIO_WAVE_STYLE_WAVE_LINEAR_FILLED = 3,
};

struct audio_wave_source {
	obs_source_t *self = nullptr;
	std::string audio_source_name;
	uint32_t color = 0xFFFFFF;
	int width = 800;
	int height = 200;
	int shape = AUDIO_WAVE_SHAPE_LINE;
	int style = AUDIO_WAVE_STYLE_WAVE_LINE;
	float gain = 2.0f;
	float curve_power = 1.0f;
	bool mirror = false;
	int frame_density = 100;
	obs_weak_source_t *audio_weak = nullptr;
	std::mutex audio_mutex;
	std::vector<float> samples_left;
	std::vector<float> samples_right;
	size_t num_samples = 0;
	std::vector<float> wave;
};

void audio_wave_build_wave(audio_wave_source *s);
void audio_wave_draw(audio_wave_source *s, gs_eparam_t *color_param);

extern "C" void register_audio_wave_source(void);
