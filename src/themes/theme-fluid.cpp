#include "theme-fluid.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(x) (void)(x)
#endif

static const char *k_theme_id_fluid = "fluid";
static const char *k_theme_name_fluid = "Fluid Wave";

static const char *FLUID_PROP_COLOR_TOP = "fluid_color_top";
static const char *FLUID_PROP_COLOR_BOTTOM = "fluid_color_bottom";
static const char *FLUID_PROP_COLOR_FILL = "fluid_color_fill";
static const char *FLUID_PROP_COLOR_DROP = "fluid_color_drop";

static const char *FLUID_PROP_BAND_HEIGHT = "fluid_band_height";
static const char *FLUID_PROP_VISCOSITY = "fluid_viscosity";
static const char *FLUID_PROP_DROP_THRESH = "fluid_drop_threshold";
static const char *FLUID_PROP_DROP_LENGTH = "fluid_drop_length";

struct fluid_theme_data {
	std::vector<float> prev_top;
	std::vector<float> prev_bottom;
	bool initialized = false;

	float band_height = 80.0f;
	float viscosity = 0.7f;
	float drop_threshold = 0.4f;
	float drop_length = 80.0f;
};

// ─────────────────────────────────────────────
// Properties
// ─────────────────────────────────────────────

static void fluid_theme_add_properties(obs_properties_t *props)
{
	obs_properties_add_color(props, FLUID_PROP_COLOR_TOP, "Top Outline Color");
	obs_properties_add_color(props, FLUID_PROP_COLOR_BOTTOM, "Bottom Outline Color");
	obs_properties_add_color(props, FLUID_PROP_COLOR_FILL, "Fill Color");
	obs_properties_add_color(props, FLUID_PROP_COLOR_DROP, "Drip Color");
	obs_properties_add_int_slider(props, FLUID_PROP_BAND_HEIGHT, "Band Height (px)", 20, 400, 5);
	obs_properties_add_float_slider(props, FLUID_PROP_VISCOSITY, "Viscosity (Smoothness)", 0.0, 1.0, 0.05);
	obs_properties_add_float_slider(props, FLUID_PROP_DROP_THRESH, "Drop Threshold (0..1)", 0.0, 1.0, 0.01);
	obs_properties_add_int_slider(props, FLUID_PROP_DROP_LENGTH, "Drop Length (px)", 5, 300, 5);
}

static void fluid_theme_update(audio_wave_source *s, obs_data_t *settings)
{
	if (!s || !settings)
		return;

	s->theme_style_id = "default";

	uint32_t col_top = (uint32_t)aw_get_int_default(settings, FLUID_PROP_COLOR_TOP, 0);
	uint32_t col_bottom = (uint32_t)aw_get_int_default(settings, FLUID_PROP_COLOR_BOTTOM, 0);
	uint32_t col_fill = (uint32_t)aw_get_int_default(settings, FLUID_PROP_COLOR_FILL, 0);
	uint32_t col_drop = (uint32_t)aw_get_int_default(settings, FLUID_PROP_COLOR_DROP, 0);

	if (col_top == 0)
		col_top = 0x00FFFF;
	if (col_bottom == 0)
		col_bottom = 0xFF00FF;
	if (col_fill == 0)
		col_fill = 0x101020;
	if (col_drop == 0)
		col_drop = 0xFFFF66;

	s->color = col_top;

	s->colors.clear();
	s->colors.push_back(audio_wave_named_color{"top", col_top});
	s->colors.push_back(audio_wave_named_color{"bottom", col_bottom});
	s->colors.push_back(audio_wave_named_color{"fill", col_fill});
	s->colors.push_back(audio_wave_named_color{"drop", col_drop});

	int band_h = aw_get_int_default(settings, FLUID_PROP_BAND_HEIGHT, 80);
	band_h = std::clamp(band_h, 20, 400);

	double visc = aw_get_float_default(settings, FLUID_PROP_VISCOSITY, 0.7f);
	if (visc < 0.0)
		visc = 0.0;
	if (visc > 1.0)
		visc = 1.0;

	double thr = aw_get_float_default(settings, FLUID_PROP_DROP_THRESH, 0.4f);
	if (thr < 0.0)
		thr = 0.0;
	if (thr > 1.0)
		thr = 1.0;

	int drop_len = aw_get_int_default(settings, FLUID_PROP_DROP_LENGTH, 80);
	drop_len = std::clamp(drop_len, 5, 300);

	auto *d = static_cast<fluid_theme_data *>(s->theme_data);
	if (!d) {
		d = new fluid_theme_data{};
		s->theme_data = d;
	}

	d->band_height = (float)band_h;
	d->viscosity = (float)visc;
	d->drop_threshold = (float)thr;
	d->drop_length = (float)drop_len;

	d->initialized = false;

	if (s->frame_density < 80)
		s->frame_density = 80;
}

// ─────────────────────────────────────────────
// Drawing
// ─────────────────────────────────────────────

static void fluid_theme_draw(audio_wave_source *s, gs_eparam_t *color_param)
{
	if (!s || s->width <= 0 || s->height <= 0)
		return;

	const size_t frames = s->wave.size();
	if (frames < 2)
		return;

	auto *d = static_cast<fluid_theme_data *>(s->theme_data);
	if (!d) {
		d = new fluid_theme_data{};
		s->theme_data = d;
	}

	const float w = (float)s->width;
	const float h = (float)s->height;
	const float mid_y = h * 0.5f;
	const uint32_t wx = (uint32_t)std::max(1.0f, w);

	const uint32_t col_top = audio_wave_get_color(s, 0, s->color);
	const uint32_t col_bottom = audio_wave_get_color(s, 1, col_top);
	const uint32_t col_fill = audio_wave_get_color(s, 2, col_bottom);
	const uint32_t col_drop = audio_wave_get_color(s, 3, col_fill);

	std::vector<float> amp(wx);
	for (uint32_t x = 0; x < wx; ++x) {
		const size_t idx = (size_t)((double)x * (double)(frames - 1) / std::max(1.0, (double)(wx - 1)));
		amp[x] = (idx < frames) ? s->wave[idx] : 0.0f;
	}

	std::vector<float> amp_smooth(wx);
	if (!amp.empty()) {
		float prev = amp[0];
		amp_smooth[0] = prev;
		const float alpha_space = 0.25f;
		for (uint32_t i = 1; i < wx; ++i) {
			prev = prev + alpha_space * (amp[i] - prev);
			amp_smooth[i] = prev;
		}
	}

	if (d->prev_top.size() != wx) {
		d->prev_top.assign(wx, mid_y);
		d->prev_bottom.assign(wx, mid_y);
		d->initialized = false;
	}

	std::vector<float> top_y(wx), bottom_y(wx);

	const float base_alpha = 0.05f;
	const float extra_alpha = 0.35f;
	const float alpha_time = base_alpha + extra_alpha * (1.0f - d->viscosity);
	const float half_band = d->band_height * 0.5f;

	for (uint32_t x = 0; x < wx; ++x) {
		float a = amp_smooth[x];
		if (a < 0.0f)
			a = 0.0f;
		if (a > 1.0f)
			a = 1.0f;

		float v = audio_wave_apply_curve(s, a);

		float offset = v * half_band;

		float top_target = mid_y - offset;
		float bottom_target = mid_y + offset;

		if (!d->initialized) {
			d->prev_top[x] = top_target;
			d->prev_bottom[x] = bottom_target;
		}

		float ty = d->prev_top[x] + alpha_time * (top_target - d->prev_top[x]);
		float by = d->prev_bottom[x] + alpha_time * (bottom_target - d->prev_bottom[x]);

		d->prev_top[x] = ty;
		d->prev_bottom[x] = by;

		top_y[x] = ty;
		bottom_y[x] = by;
	}

	d->initialized = true;

	gs_matrix_push();

	if (color_param)
		audio_wave_set_solid_color(color_param, col_fill);

	gs_render_start(true);
	for (uint32_t x = 0; x + 1 < wx; ++x) {
		const float x0 = (float)x;
		const float x1 = (float)(x + 1);

		const float t0 = top_y[x];
		const float t1 = top_y[x + 1];
		const float b0 = bottom_y[x];
		const float b1 = bottom_y[x + 1];

		gs_vertex2f(x0, b0);
		gs_vertex2f(x0, t0);
		gs_vertex2f(x1, b1);

		gs_vertex2f(x1, b1);
		gs_vertex2f(x0, t0);
		gs_vertex2f(x1, t1);
	}
	gs_render_stop(GS_TRIS);

	if (color_param)
		audio_wave_set_solid_color(color_param, col_top);

	gs_render_start(true);
	for (uint32_t x = 0; x < wx; ++x) {
		gs_vertex2f((float)x, top_y[x]);
	}
	gs_render_stop(GS_LINESTRIP);

	if (color_param)
		audio_wave_set_solid_color(color_param, col_bottom);

	gs_render_start(true);
	for (uint32_t x = 0; x < wx; ++x) {
		gs_vertex2f((float)x, bottom_y[x]);
	}
	gs_render_stop(GS_LINESTRIP);

	if (color_param)
		audio_wave_set_solid_color(color_param, col_drop);

	gs_render_start(true);
	for (uint32_t x = 0; x < wx; x += 3) {
		float a = amp_smooth[x];
		if (a < 0.0f)
			a = 0.0f;
		if (a > 1.0f)
			a = 1.0f;

		float v = audio_wave_apply_curve(s, a);
		if (v < d->drop_threshold)
			continue;

		float extra = (v - d->drop_threshold) / std::max(0.001f, 1.0f - d->drop_threshold);
		if (extra < 0.0f)
			extra = 0.0f;
		if (extra > 1.0f)
			extra = 1.0f;

		float len = d->drop_length * extra;

		const float x0 = (float)x;
		const float y0 = bottom_y[x];
		const float y1 = std::min(h, y0 + len);

		gs_vertex2f(x0, y0);
		gs_vertex2f(x0, y1);
	}
	gs_render_stop(GS_LINES);

	gs_matrix_pop();
}

static void fluid_theme_destroy_data(audio_wave_source *s)
{
	if (!s || !s->theme_data)
		return;

	auto *d = static_cast<fluid_theme_data *>(s->theme_data);
	delete d;
	s->theme_data = nullptr;
}

static const audio_wave_theme k_fluid_theme = {
	k_theme_id_fluid,   k_theme_name_fluid, fluid_theme_add_properties,
	fluid_theme_update, fluid_theme_draw,   fluid_theme_destroy_data,
};

void audio_wave_register_fluid_theme()
{
	audio_wave_register_theme(&k_fluid_theme);
}
