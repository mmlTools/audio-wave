#pragma once

#include <obs-module.h>
#include <graphics/graphics.h>

#include <mutex>
#include <vector>
#include <string>
#include <cstdint>

struct audio_wave_source {
	obs_source_t *self = nullptr;

	std::string audio_source_name;

	uint32_t color = 0xFFFFFF;
	uint32_t color2 = 0x00FF00;
	int width = 800;
	int height = 200;
	int mode = 0; // 0 = Wave, 1 = Bars, 2 = Rectangular, 3 = Rectangular (Filled)
	bool use_gradient = false;
	float gain = 2.0f;
	bool mirror = false;

	int frame_radius = 0;    // 0..100
	int frame_density = 100; // %

	// Audio capture
	obs_weak_source_t *audio_weak = nullptr;

	std::mutex audio_mutex;
	std::vector<float> samples_left;
	std::vector<float> samples_right;
	size_t num_samples = 0;

	// Pre-processed mono wave [0..1]
	std::vector<float> wave;
};

void audio_wave_build_wave(audio_wave_source *s);
void audio_wave_draw(audio_wave_source *s, gs_eparam_t *color_param);

extern "C" void register_audio_wave_source(void);
