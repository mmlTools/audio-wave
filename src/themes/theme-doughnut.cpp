#include "theme-doughnut.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstring>

#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(x) (void)(x)
#endif

static const char *k_theme_id_doughnut = "doughnut";
static const char *k_theme_name_doughnut = "Doughnut";

static const char *DOUGHNUT_PROP_COLOR_OUTER = "doughnut_color_outer";
static const char *DOUGHNUT_PROP_COLOR_INNER = "doughnut_color_inner";
static const char *DOUGHNUT_PROP_COLOR_FILL = "doughnut_color_fill";
static const char *DOUGHNUT_PROP_COLOR_DOTS = "doughnut_color_dots";

static const char *DOUGHNUT_PROP_SEGMENTS = "doughnut_segments";
static const char *DOUGHNUT_PROP_BAND_WIDTH = "doughnut_band_width";
static const char *DOUGHNUT_PROP_THICK_OUTER = "doughnut_thickness_outer";
static const char *DOUGHNUT_PROP_THICK_INNER = "doughnut_thickness_inner";
static const char *DOUGHNUT_PROP_DOT_THRESHOLD = "doughnut_dot_threshold";
static const char *DOUGHNUT_PROP_DOT_LENGTH = "doughnut_dot_length";

struct doughnut_theme_data {
	std::vector<float> prev_r;
	bool initialized = false;

	uint32_t segments = 128;
	float band_width = 40.0f;
	int thick_outer = 3;
	int thick_inner = 2;
	float dot_threshold = 0.25f;
	float dot_length = 40.0f;
};

// ─────────────────────────────────────────────
// Properties
// ─────────────────────────────────────────────

static void doughnut_theme_add_properties(obs_properties_t *props)
{
	obs_properties_add_color(props, DOUGHNUT_PROP_COLOR_OUTER, "Outer Ring Color");
	obs_properties_add_color(props, DOUGHNUT_PROP_COLOR_INNER, "Inner Ring Color");
	obs_properties_add_color(props, DOUGHNUT_PROP_COLOR_FILL, "Band Fill Color");
	obs_properties_add_color(props, DOUGHNUT_PROP_COLOR_DOTS, "Orbit Dots Color");
	obs_properties_add_int_slider(props, DOUGHNUT_PROP_SEGMENTS, "Shape Resolution", 32, 512, 8);
	obs_properties_add_int_slider(props, DOUGHNUT_PROP_BAND_WIDTH, "Band Width (px)", 10, 300, 5);
	obs_properties_add_int_slider(props, DOUGHNUT_PROP_THICK_OUTER, "Outer Ring Thickness", 1, 8, 1);
	obs_properties_add_int_slider(props, DOUGHNUT_PROP_THICK_INNER, "Inner Ring Thickness", 1, 8, 1);
	obs_properties_add_float_slider(props, DOUGHNUT_PROP_DOT_THRESHOLD, "Dot Threshold (0..1)", 0.0, 1.0, 0.01);
	obs_properties_add_int_slider(props, DOUGHNUT_PROP_DOT_LENGTH, "Dot Length (px)", 5, 200, 5);
}

static void doughnut_theme_update(audio_wave_source *s, obs_data_t *settings)
{
	if (!s || !settings)
		return;

	s->theme_style_id = "default";

	uint32_t col_outer = (uint32_t)aw_get_int_default(settings, DOUGHNUT_PROP_COLOR_OUTER, 0);
	uint32_t col_inner = (uint32_t)aw_get_int_default(settings, DOUGHNUT_PROP_COLOR_INNER, 0);
	uint32_t col_fill = (uint32_t)aw_get_int_default(settings, DOUGHNUT_PROP_COLOR_FILL, 0);
	uint32_t col_dots = (uint32_t)aw_get_int_default(settings, DOUGHNUT_PROP_COLOR_DOTS, 0);

	if (col_outer == 0)
		col_outer = 0xFF6600;
	if (col_inner == 0)
		col_inner = 0x00FFAA;
	if (col_fill == 0)
		col_fill = 0x101020;
	if (col_dots == 0)
		col_dots = 0xFFFFFF;

	s->color = col_outer;

	s->colors.clear();
	s->colors.push_back(audio_wave_named_color{"outer", col_outer});
	s->colors.push_back(audio_wave_named_color{"inner", col_inner});
	s->colors.push_back(audio_wave_named_color{"fill", col_fill});
	s->colors.push_back(audio_wave_named_color{"dots", col_dots});

	int seg = aw_get_int_default(settings, DOUGHNUT_PROP_SEGMENTS, 128);
	seg = std::clamp(seg, 32, 512);

	int band_w = aw_get_int_default(settings, DOUGHNUT_PROP_BAND_WIDTH, 40);
	band_w = std::clamp(band_w, 10, 300);

	int t_outer = aw_get_int_default(settings, DOUGHNUT_PROP_THICK_OUTER, 3);
	int t_inner = aw_get_int_default(settings, DOUGHNUT_PROP_THICK_INNER, 2);
	t_outer = std::clamp(t_outer, 1, 8);
	t_inner = std::clamp(t_inner, 1, 8);

	double thr = aw_get_float_default(settings, DOUGHNUT_PROP_DOT_THRESHOLD, 0.25f);
	thr = std::clamp(thr, 0.0, 1.0);

	int dot_len = aw_get_int_default(settings, DOUGHNUT_PROP_DOT_LENGTH, 40);
	dot_len = std::clamp(dot_len, 5, 200);

	auto *d = static_cast<doughnut_theme_data *>(s->theme_data);
	if (!d) {
		d = new doughnut_theme_data{};
		s->theme_data = d;
	}

	d->segments = (uint32_t)seg;
	d->band_width = (float)band_w;
	d->thick_outer = t_outer;
	d->thick_inner = t_inner;
	d->dot_threshold = (float)thr;
	d->dot_length = (float)dot_len;

	d->initialized = false;
}

// ─────────────────────────────────────────────
// Drawing
// ─────────────────────────────────────────────

static void doughnut_theme_draw(audio_wave_source *s, gs_eparam_t *color_param)
{
	if (!s || s->width <= 0 || s->height <= 0)
		return;

	const size_t frames = s->wave.size();
	if (frames < 2)
		return;

	auto *d = static_cast<doughnut_theme_data *>(s->theme_data);
	if (!d) {
		d = new doughnut_theme_data{};
		s->theme_data = d;
	}

	const float w = (float)s->width;
	const float h = (float)s->height;
	const float cx = w * 0.5f;
	const float cy = h * 0.5f;

	const float Rbase = std::min(w, h) * 0.30f;
	const float Rext = std::min(w, h) * 0.25f;

	const uint32_t segments = std::max(d->segments, 32u);

	const uint32_t col_outer = audio_wave_get_color(s, 0, s->color);
	const uint32_t col_inner = audio_wave_get_color(s, 1, col_outer);
	const uint32_t col_fill = audio_wave_get_color(s, 2, col_inner);
	const uint32_t col_dots = audio_wave_get_color(s, 3, col_fill);

	std::vector<float> amp(segments);
	for (uint32_t i = 0; i < segments; ++i) {
		const float u = (float)i / (float)segments;
		const size_t idx = (size_t)(u * (float)(frames - 1));
		amp[i] = (idx < frames) ? s->wave[idx] : 0.0f;
	}

	std::vector<float> amp_smooth(segments);
	if (!amp.empty()) {
		float prev = amp[0];
		amp_smooth[0] = prev;
		const float alpha_space = 0.20f;
		for (uint32_t i = 1; i < segments; ++i) {
			prev = prev + alpha_space * (amp[i] - prev);
			amp_smooth[i] = prev;
		}

		float wrap = amp_smooth[segments - 1];
		amp_smooth[0] = 0.5f * (amp_smooth[0] + wrap);
	}

	if (d->prev_r.size() != segments) {
		d->prev_r.assign(segments, Rbase);
		d->initialized = false;
	}

	std::vector<float> r_center(segments);
	const float alpha_time = 0.30f;

	for (uint32_t i = 0; i < segments; ++i) {
		float a = amp_smooth[i];
		if (a < 0.0f)
			a = 0.0f;
		if (a > 1.0f)
			a = 1.0f;

		a = audio_wave_apply_curve(s, a);

		float r_target = Rbase + a * Rext;

		if (!d->initialized)
			d->prev_r[i] = r_target;

		float r_s = d->prev_r[i] + alpha_time * (r_target - d->prev_r[i]);
		d->prev_r[i] = r_s;
		r_center[i] = r_s;
	}

	d->initialized = true;

	std::vector<float> cos_t(segments), sin_t(segments);
	for (uint32_t i = 0; i < segments; ++i) {
		const float t = ((float)i / (float)segments) * 2.0f * (float)M_PI;
		cos_t[i] = std::cos(t);
		sin_t[i] = std::sin(t);
	}

	const float half_band = d->band_width * 0.5f;

	std::vector<float> r_inner(segments), r_outer(segments);
	for (uint32_t i = 0; i < segments; ++i) {
		float ri = r_center[i] - half_band;
		float ro = r_center[i] + half_band;
		if (ri < 0.0f)
			ri = 0.0f;
		r_inner[i] = ri;
		r_outer[i] = ro;
	}

	std::vector<float> x_in(segments), y_in(segments);
	std::vector<float> x_out(segments), y_out(segments);
	for (uint32_t i = 0; i < segments; ++i) {
		x_in[i] = cx + cos_t[i] * r_inner[i];
		y_in[i] = cy + sin_t[i] * r_inner[i];
		x_out[i] = cx + cos_t[i] * r_outer[i];
		y_out[i] = cy + sin_t[i] * r_outer[i];
	}

	gs_matrix_push();

	if (color_param)
		audio_wave_set_solid_color(color_param, col_fill);

	gs_render_start(true);
	for (uint32_t i = 0; i < segments; ++i) {
		uint32_t next = (i + 1) % segments;

		gs_vertex2f(x_in[i], y_in[i]);
		gs_vertex2f(x_out[i], y_out[i]);
		gs_vertex2f(x_out[next], y_out[next]);

		gs_vertex2f(x_in[i], y_in[i]);
		gs_vertex2f(x_out[next], y_out[next]);
		gs_vertex2f(x_in[next], y_in[next]);
	}
	gs_render_stop(GS_TRIS);

	if (color_param)
		audio_wave_set_solid_color(color_param, col_outer);

	int thickO = d->thick_outer;
	if (thickO < 1)
		thickO = 1;

	{
		const float half = (float)(thickO - 1) * 0.5f;
		for (int t = 0; t < thickO; ++t) {
			const float offset = (float)t - half;

			gs_render_start(true);
			for (uint32_t i = 0; i <= segments; ++i) {
				uint32_t idx = (i == segments) ? 0 : i;
				float rad = r_outer[idx] + offset;
				if (rad < 0.0f)
					rad = 0.0f;

				const float x = cx + cos_t[idx] * rad;
				const float y = cy + sin_t[idx] * rad;

				gs_vertex2f(x, y);
			}
			gs_render_stop(GS_LINESTRIP);
		}
	}

	if (color_param)
		audio_wave_set_solid_color(color_param, col_inner);

	int thickI = d->thick_inner;
	if (thickI < 1)
		thickI = 1;

	{
		const float half = (float)(thickI - 1) * 0.5f;
		for (int t = 0; t < thickI; ++t) {
			const float offset = (float)t - half;

			gs_render_start(true);
			for (uint32_t i = 0; i <= segments; ++i) {
				uint32_t idx = (i == segments) ? 0 : i;
				float rad = r_inner[idx] + offset;
				if (rad < 0.0f)
					rad = 0.0f;

				const float x = cx + cos_t[idx] * rad;
				const float y = cy + sin_t[idx] * rad;

				gs_vertex2f(x, y);
			}
			gs_render_stop(GS_LINESTRIP);
		}
	}

	if (color_param)
		audio_wave_set_solid_color(color_param, col_dots);

	gs_render_start(true);
	for (uint32_t i = 0; i < segments; ++i) {
		float a = amp_smooth[i];
		if (a < 0.0f)
			a = 0.0f;
		if (a > 1.0f)
			a = 1.0f;

		float v = audio_wave_apply_curve(s, a);
		if (v < d->dot_threshold)
			continue;

		float extra = (v - d->dot_threshold) / std::max(0.001f, 1.0f - d->dot_threshold);
		if (extra < 0.0f)
			extra = 0.0f;
		if (extra > 1.0f)
			extra = 1.0f;

		float len = d->dot_length * extra;

		const float r_start = r_outer[i] + 2.0f;
		const float r_end = r_start + len;

		const float x_start = cx + cos_t[i] * r_start;
		const float y_start = cy + sin_t[i] * r_start;
		const float x_end = cx + cos_t[i] * r_end;
		const float y_end = cy + sin_t[i] * r_end;

		gs_vertex2f(x_start, y_start);
		gs_vertex2f(x_end, y_end);
	}
	gs_render_stop(GS_LINES);

	gs_matrix_pop();
}

static void doughnut_theme_destroy_data(audio_wave_source *s)
{
	if (!s || !s->theme_data)
		return;

	auto *d = static_cast<doughnut_theme_data *>(s->theme_data);
	delete d;
	s->theme_data = nullptr;
}

static const audio_wave_theme k_doughnut_theme = {
	k_theme_id_doughnut,   k_theme_name_doughnut, doughnut_theme_add_properties,
	doughnut_theme_update, doughnut_theme_draw,   doughnut_theme_destroy_data,
};

void audio_wave_register_doughnut_theme()
{
	audio_wave_register_theme(&k_doughnut_theme);
}
