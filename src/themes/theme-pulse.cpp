#include "theme-pulse.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
#include <util/platform.h>

static const char *k_theme_id_pulse = "pulse_rotate_spectrum";
static const char *k_theme_name_pulse = "Colorful Abstract Spectrum";

#define P_STYLE "style"
#define P_ROT_SPEED "rotation_speed"
#define P_HUE_SPEED "hue_speed"
#define P_SENSITIVITY "sensitivity"
#define P_SMOOTHING "fluidity"
#define P_INNER_GAP "inner_gap"
#define P_BAR_COUNT "thickness"

static uint32_t hsv_to_rgb(float h, float s, float v)
{
	float r, g, b;
	int i = (int)(h * 6);
	float f = h * 6 - i;
	float p = v * (1 - s);
	float q = v * (1 - f * s);
	float t = v * (1 - (1 - f) * s);
	switch (i % 6) {
	case 0:
		r = v, g = t, b = p;
		break;
	case 1:
		r = q, g = v, b = p;
		break;
	case 2:
		r = p, g = v, b = t;
		break;
	case 3:
		r = p, g = q, b = v;
		break;
	case 4:
		r = t, g = p, b = v;
		break;
	default:
		r = v, g = p, b = q;
		break;
	}
	return 0xFF000000 | ((uint8_t)(b * 255) << 16) | ((uint8_t)(g * 255) << 8) | (uint8_t)(r * 255);
}

static void pulse_theme_add_properties(obs_properties_t *props)
{
	obs_property_t *style =
		obs_properties_add_list(props, P_STYLE, "Style", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(style, "Spectrum Bars", "spectrum");
	obs_property_list_add_string(style, "Fluid Solid", "solid");

	obs_properties_add_float_slider(props, P_ROT_SPEED, "Rotation Speed", -5.0f, 5.0f, 0.1f);
	obs_properties_add_float_slider(props, P_HUE_SPEED, "Color Cycle Speed", 0.0f, 2.0f, 0.05f);
	obs_properties_add_float_slider(props, P_SENSITIVITY, "Audio Reactivity", 1.0f, 20.0f, 0.5f);
	obs_properties_add_float_slider(props, P_SMOOTHING, "Smoothing (Fluidity)", 0.1f, 1.0f, 0.05f);
	obs_properties_add_float_slider(props, P_INNER_GAP, "Circle Size", 0.1f, 0.9f, 0.01f);
	obs_properties_add_int_slider(props, P_BAR_COUNT, "Bar Density", 60, 300, 10);
}

static void pulse_theme_update(audio_wave_source *s, obs_data_t *settings)
{
	s->theme_style_id = obs_data_get_string(settings, P_STYLE);
	s->rot_speed = (float)obs_data_get_double(settings, P_ROT_SPEED);
	s->hue_speed = (float)obs_data_get_double(settings, P_HUE_SPEED);
	s->sensitivity = (float)obs_data_get_double(settings, P_SENSITIVITY);
	s->fluidity = (float)obs_data_get_double(settings, P_SMOOTHING);
	s->inner_gap = (float)obs_data_get_double(settings, P_INNER_GAP);
	s->thickness = (int)obs_data_get_int(settings, P_BAR_COUNT);

	if (s->inner_gap <= 0.0f)
		s->inner_gap = 0.5f;
	if (s->thickness <= 0)
		s->thickness = 120;
}

static void pulse_theme_draw(audio_wave_source *s, gs_eparam_t *color_param)
{
	if (!s || s->width <= 0 || s->height <= 0)
		return;

	uint64_t ts = os_gettime_ns();
	float t = (float)((double)ts / 1000000000.0);
	float rotation = t * s->rot_speed;
	float hue_base = fmodf(t * s->hue_speed, 1.0f);

	const float cx = s->width * 0.5f;
	const float cy = s->height * 0.5f;
	const float R_max = std::min(cx, cy);
	const float R_base = R_max * s->inner_gap;

	const size_t frames = s->wave.size();
	const uint32_t bar_count = (uint32_t)s->thickness;

	// Drawing the spectrum lines
	for (uint32_t i = 0; i < bar_count; ++i) {
		float u = (float)i / (float)bar_count;

		// Spectrum color mapping (rainbow around the circle)
		float hue = fmodf(hue_base + u, 1.0f);
		if (color_param)
			audio_wave_set_solid_color(color_param, hsv_to_rgb(hue, 0.8f, 1.0f));

		// Mirrored sampling for 360-degree seamlessness
		float mirrored_u = (u < 0.5f) ? (u * 2.0f) : (2.0f - (u * 2.0f));
		size_t idx = (size_t)(mirrored_u * (float)(frames - 1));
		float val = (idx < frames) ? s->wave[idx] : 0.0f;

		// Apply sensitivity and smoothing
		float height = val * s->sensitivity * R_max * 0.6f;

		// Calculate angles
		float angle = (u * 2.0f * (float)M_PI) + rotation;
		float cos_a = cosf(angle);
		float sin_a = sinf(angle);

		// Draw the vertical-style bar
		gs_render_start(true);
		float r_start = R_base;
		float r_end = R_base + 2.0f + height; // 2px minimum height

		gs_vertex2f(cx + cos_a * r_start, cy + sin_a * r_start);
		gs_vertex2f(cx + cos_a * r_end, cy + sin_a * r_end);

		// Using lines with thickness for that "Spectrum" look
		gs_render_stop(GS_LINES);
	}
}

static void pulse_theme_destroy_data(audio_wave_source *s)
{
	(void)s;
}

static const audio_wave_theme k_pulse_theme = {
	k_theme_id_pulse,   k_theme_name_pulse, pulse_theme_add_properties,
	pulse_theme_update, pulse_theme_draw,   pulse_theme_destroy_data,
};

void audio_wave_register_pulse_theme()
{
	audio_wave_register_theme(&k_pulse_theme);
}