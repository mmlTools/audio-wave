#include "theme-fluidblob.hpp"
#include "audio-wave.hpp"
#include "audiowave-themes.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <graphics/graphics.h>

#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(x) (void)(x)
#endif

static const char *k_theme_id_fluidblob = "fluidblob";
static const char *k_theme_name_fluidblob = "Fluid Abstract";

static const char *FB_PROP_COLOR_OUTLINE = "fb_color_outline";
static const char *FB_PROP_COLOR_FILL = "fb_color_fill";
static const char *FB_PROP_COLOR_SPARK = "fb_color_spark";
static const char *FB_PROP_SEGMENTS = "fb_segments";
static const char *FB_PROP_VISCOSITY = "fb_viscosity";
static const char *FB_PROP_NOISE = "fb_noise_amount";
static const char *FB_PROP_THICK_OUTLINE = "fb_thickness_outline";
static const char *FB_PROP_ROT_SPEED = "fb_rotation_speed";
static const char *FB_PROP_SPARK_THRESH = "fb_spark_threshold";
static const char *FB_PROP_SPARK_LENGTH = "fb_spark_length";
static const char *FB_PROP_FILL_TRANSP = "fb_fill_transparent";

struct fluidblob_theme_data {
	std::vector<float> prev_r;
	bool initialized = false;

	uint32_t segments = 160;
	float viscosity = 0.6f;
	float noise_amount = 0.4f;
	int outline_thick = 3;
	float rot_speed = 0.6f;
	float phase = 0.0f;

	float spark_threshold = 0.35f;
	float spark_length = 60.0f;

	bool fill_transparent = false;
};

// ─────────────────────────────────────────────
// Properties
// ─────────────────────────────────────────────

static void fluidblob_theme_add_properties(obs_properties_t *props)
{
	obs_properties_add_color(props, FB_PROP_COLOR_OUTLINE, "Outline Color");
	obs_properties_add_color(props, FB_PROP_COLOR_FILL, "Fill Color");
	obs_properties_add_color(props, FB_PROP_COLOR_SPARK, "Spark Color");
	obs_properties_add_int_slider(props, FB_PROP_SEGMENTS, "Shape Resolution", 32, 512, 8);
	obs_properties_add_float_slider(props, FB_PROP_VISCOSITY, "Viscosity (Smoothness)", 0.0, 1.0, 0.05);
	obs_properties_add_float_slider(props, FB_PROP_NOISE, "Organic Wobble Amount", 0.0, 1.0, 0.05);
	obs_properties_add_int_slider(props, FB_PROP_THICK_OUTLINE, "Outline Thickness", 1, 8, 1);
	obs_properties_add_float_slider(props, FB_PROP_ROT_SPEED, "Rotation Speed", 0.0, 5.0, 0.1);
	obs_properties_add_float_slider(props, FB_PROP_SPARK_THRESH, "Spark Threshold (0..1)", 0.0, 1.0, 0.01);
	obs_properties_add_int_slider(props, FB_PROP_SPARK_LENGTH, "Spark Length (px)", 5, 200, 5);
	obs_properties_add_bool(props, FB_PROP_FILL_TRANSP, "Transparent Fill (outline only)");
}

static void fluidblob_theme_update(audio_wave_source *s, obs_data_t *settings)
{
	if (!s || !settings)
		return;

	s->theme_style_id = "default";

	uint32_t col_outline = (uint32_t)aw_get_int_default(settings, FB_PROP_COLOR_OUTLINE, 0);
	uint32_t col_fill = (uint32_t)aw_get_int_default(settings, FB_PROP_COLOR_FILL, 0);
	uint32_t col_spark = (uint32_t)aw_get_int_default(settings, FB_PROP_COLOR_SPARK, 0);

	if (col_outline == 0)
		col_outline = 0x00FFFF;
	if (col_fill == 0)
		col_fill = 0x101020;
	if (col_spark == 0)
		col_spark = 0xFFFFAA;

	s->color = col_outline;

	s->colors.clear();
	s->colors.push_back(audio_wave_named_color{"outline", col_outline});
	s->colors.push_back(audio_wave_named_color{"fill", col_fill});
	s->colors.push_back(audio_wave_named_color{"spark", col_spark});

	int seg = aw_get_int_default(settings, FB_PROP_SEGMENTS, 160);
	seg = std::clamp(seg, 32, 512);

	double visc = aw_get_float_default(settings, FB_PROP_VISCOSITY, 0.6f);
	visc = std::clamp(visc, 0.0, 1.0);

	double noise = aw_get_float_default(settings, FB_PROP_NOISE, 0.4f);
	noise = std::clamp(noise, 0.0, 1.0);

	int thick = aw_get_int_default(settings, FB_PROP_THICK_OUTLINE, 3);
	thick = std::clamp(thick, 1, 8);

	double rot = aw_get_float_default(settings, FB_PROP_ROT_SPEED, 0.6f);
	rot = std::clamp(rot, 0.0, 5.0);

	double thr = aw_get_float_default(settings, FB_PROP_SPARK_THRESH, 0.35f);
	thr = std::clamp(thr, 0.0, 1.0);

	int spark_len = aw_get_int_default(settings, FB_PROP_SPARK_LENGTH, 60);
	spark_len = std::clamp(spark_len, 5, 200);

	bool fill_transparent = obs_data_get_bool(settings, FB_PROP_FILL_TRANSP);

	auto *d = static_cast<fluidblob_theme_data *>(s->theme_data);
	if (!d) {
		d = new fluidblob_theme_data{};
		s->theme_data = d;
	}

	d->segments = (uint32_t)seg;
	d->viscosity = (float)visc;
	d->noise_amount = (float)noise;
	d->outline_thick = thick;
	d->rot_speed = (float)rot;
	d->spark_threshold = (float)thr;
	d->spark_length = (float)spark_len;
	d->fill_transparent = fill_transparent;
	d->initialized = false;

	if (s->frame_density < 80)
		s->frame_density = 80;
}

// ─────────────────────────────────────────────
// Drawing (blob geometry, outline, sparks)
// ─────────────────────────────────────────────

static void fluidblob_theme_draw(audio_wave_source *s, gs_eparam_t *color_param)
{
	if (!s || s->width <= 0 || s->height <= 0)
		return;

	if (s->wave.empty())
		audio_wave_build_wave(s);

	const size_t frames = s->wave.size();
	if (frames < 2)
		return;

	auto *d = static_cast<fluidblob_theme_data *>(s->theme_data);
	if (!d) {
		d = new fluidblob_theme_data{};
		s->theme_data = d;
	}

	const float w = (float)s->width;
	const float h = (float)s->height;
	const float cx = w * 0.5f;
	const float cy = h * 0.5f;

	const float base_radius = std::min(w, h) * 0.28f;
	const float audio_radius = std::min(w, h) * 0.25f;
	const float noise_radius = std::min(w, h) * 0.15f;

	const uint32_t segments = std::max(d->segments, 32u);

	const uint32_t col_outline = audio_wave_get_color(s, 0, s->color);
	const uint32_t col_fill = audio_wave_get_color(s, 1, col_outline);
	const uint32_t col_spark = audio_wave_get_color(s, 2, col_fill);

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
		const float alpha_space = 0.25f;
		for (uint32_t i = 1; i < segments; ++i) {
			prev = prev + alpha_space * (amp[i] - prev);
			amp_smooth[i] = prev;
		}
		float wrap = amp_smooth[segments - 1];
		amp_smooth[0] = 0.5f * (amp_smooth[0] + wrap);
	}

	if (d->prev_r.size() != segments) {
		d->prev_r.assign(segments, base_radius);
		d->initialized = false;
	}

	std::vector<float> radius(segments);

	const float base_alpha = 0.06f;
	const float extra_alpha = 0.34f;
	const float alpha_time = base_alpha + extra_alpha * (1.0f - d->viscosity);

	d->phase += d->rot_speed * (float)M_PI / 180.0f;
	if (d->phase > 2.0f * (float)M_PI)
		d->phase -= 2.0f * (float)M_PI;

	const int noise_harmonics = 2 + (int)std::round(d->noise_amount * 3.0f);

	for (uint32_t i = 0; i < segments; ++i) {
		float a = amp_smooth[i];
		a = std::clamp(a, 0.0f, 1.0f);

		float v = audio_wave_apply_curve(s, a);

		float r_target = base_radius + v * audio_radius;

		const float angle = ((float)i / (float)segments) * 2.0f * (float)M_PI;
		float wobble = 0.0f;
		const float n_amp = d->noise_amount;

		for (int h2 = 1; h2 <= noise_harmonics; ++h2) {
			float contrib = std::sin((float)h2 * angle + d->phase + (float)h2 * 0.7f);
			wobble += contrib * (1.0f / (float)h2);
		}
		wobble /= (float)noise_harmonics;

		r_target += wobble * n_amp * noise_radius;

		if (!d->initialized)
			d->prev_r[i] = r_target;

		float r_s = d->prev_r[i] + alpha_time * (r_target - d->prev_r[i]);
		d->prev_r[i] = r_s;
		radius[i] = r_s;
	}

	d->initialized = true;

	std::vector<float> cos_t(segments), sin_t(segments);
	for (uint32_t i = 0; i < segments; ++i) {
		const float t = ((float)i / (float)segments) * 2.0f * (float)M_PI;
		cos_t[i] = std::cos(t);
		sin_t[i] = std::sin(t);
	}

	std::vector<float> x(segments), y(segments);
	for (uint32_t i = 0; i < segments; ++i) {
		x[i] = cx + cos_t[i] * radius[i];
		y[i] = cy + sin_t[i] * radius[i];
	}

	gs_matrix_push();

	if (!d->fill_transparent) {
		if (color_param)
			audio_wave_set_solid_color(color_param, col_fill);

		gs_render_start(true);
		for (uint32_t i = 0; i < segments; ++i) {
			uint32_t next = (i + 1) % segments;

			gs_vertex2f(cx, cy);
			gs_vertex2f(x[i], y[i]);
			gs_vertex2f(x[next], y[next]);
		}
		gs_render_stop(GS_TRIS);
	}

	if (color_param)
		audio_wave_set_solid_color(color_param, col_outline);

	int thick = d->outline_thick;
	if (thick < 1)
		thick = 1;

	{
		const float half = (float)(thick - 1) * 0.5f;
		for (int t = 0; t < thick; ++t) {
			const float offset = (float)t - half;

			gs_render_start(true);
			for (uint32_t i = 0; i <= segments; ++i) {
				uint32_t idx = (i == segments) ? 0 : i;
				float r = radius[idx] + offset;
				if (r < 0.0f)
					r = 0.0f;

				const float px = cx + cos_t[idx] * r;
				const float py = cy + sin_t[idx] * r;

				gs_vertex2f(px, py);
			}
			gs_render_stop(GS_LINESTRIP);
		}
	}

	if (color_param)
		audio_wave_set_solid_color(color_param, col_spark);

	gs_render_start(true);
	for (uint32_t i = 0; i < segments; i += 2) {
		float a = amp_smooth[i];
		a = std::clamp(a, 0.0f, 1.0f);

		float v = audio_wave_apply_curve(s, a);
		if (v < d->spark_threshold)
			continue;

		float extra = (v - d->spark_threshold) / std::max(0.001f, 1.0f - d->spark_threshold);
		extra = std::clamp(extra, 0.0f, 1.0f);

		float len = d->spark_length * extra;

		const float r_start = radius[i] + 2.0f;
		const float r_end = r_start + len;

		const float xs = cx + cos_t[i] * r_start;
		const float ys = cy + sin_t[i] * r_start;
		const float xe = cx + cos_t[i] * r_end;
		const float ye = cy + sin_t[i] * r_end;

		gs_vertex2f(xs, ys);
		gs_vertex2f(xe, ye);
	}
	gs_render_stop(GS_LINES);

	gs_matrix_pop();
}

static void fluidblob_theme_destroy_data(audio_wave_source *s)
{
	if (!s || !s->theme_data)
		return;

	auto *d = static_cast<fluidblob_theme_data *>(s->theme_data);
	delete d;
	s->theme_data = nullptr;
}

static const audio_wave_theme k_fluidblob_theme = {k_theme_id_fluidblob,
						   k_theme_name_fluidblob,
						   fluidblob_theme_add_properties,
						   fluidblob_theme_update,
						   fluidblob_theme_draw,
						   fluidblob_theme_destroy_data,
						   nullptr};

void audio_wave_register_fluidblob_theme()
{
	audio_wave_register_theme(&k_fluidblob_theme);
}
