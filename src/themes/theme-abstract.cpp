#include "theme-abstract.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstring>

#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(x) (void)(x)
#endif

static const char *k_theme_id_abstract = "abstract";
static const char *k_theme_name_abstract = "Radial Abstraction";

static const char *ABSTRACT_PROP_COLOR_WAVE_A = "abs_color_wave_a";
static const char *ABSTRACT_PROP_COLOR_WAVE_B = "abs_color_wave_b";
static const char *ABSTRACT_PROP_COLOR_FILL = "abs_color_fill";
static const char *ABSTRACT_PROP_COLOR_FIRE = "abs_color_fire";

static const char *ABSTRACT_PROP_DB_WAVE_A = "abs_db_wave_a";
static const char *ABSTRACT_PROP_DB_WAVE_B = "abs_db_wave_b";
static const char *ABSTRACT_PROP_DB_FIRE = "abs_db_fire";

static const char *ABSTRACT_PROP_SEGMENTS = "abs_segments";
static const char *ABSTRACT_PROP_THICK_A = "abs_thickness_wave_a";
static const char *ABSTRACT_PROP_THICK_B = "abs_thickness_wave_b";

struct abstract_theme_data {
	std::vector<float> prev_r1;
	std::vector<float> prev_r2;
	bool initialized = false;

	float db_wave_a = -10.0f;
	float db_wave_b = -20.0f;
	float db_fire = -12.0f;
	uint32_t segments = 128;

	int thick_a = 2;
	int thick_b = 2;
};

// ─────────────────────────────────────────────
// Properties
// ─────────────────────────────────────────────

static void abstract_theme_add_properties(obs_properties_t *props)
{
	obs_properties_add_color(props, ABSTRACT_PROP_COLOR_WAVE_A, "Wave A Color");
	obs_properties_add_color(props, ABSTRACT_PROP_COLOR_WAVE_B, "Wave B Color");
	obs_properties_add_color(props, ABSTRACT_PROP_COLOR_FILL, "Fill Color");
	obs_properties_add_color(props, ABSTRACT_PROP_COLOR_FIRE, "Fireworks Color");

	obs_properties_add_int_slider(props, ABSTRACT_PROP_DB_WAVE_A, "Wave A Target dB", -60, 0, 1);
	obs_properties_add_int_slider(props, ABSTRACT_PROP_DB_WAVE_B, "Wave B Target dB", -60, 0, 1);
	obs_properties_add_int_slider(props, ABSTRACT_PROP_DB_FIRE, "Fireworks Threshold (dB)", -60, 0, 1);

	obs_properties_add_int_slider(props, ABSTRACT_PROP_SEGMENTS, "Shape Resolution", 32, 512, 8);
	obs_properties_add_int_slider(props, ABSTRACT_PROP_THICK_A, "Wave A Thickness", 1, 8, 1);
	obs_properties_add_int_slider(props, ABSTRACT_PROP_THICK_B, "Wave B Thickness", 1, 8, 1);
}

static void abstract_theme_update(audio_wave_source *s, obs_data_t *settings)
{
	if (!s || !settings)
		return;

	s->theme_style_id = "default";

	uint32_t col_wave_a = (uint32_t)aw_get_int_default(settings, ABSTRACT_PROP_COLOR_WAVE_A, 0);
	uint32_t col_wave_b = (uint32_t)aw_get_int_default(settings, ABSTRACT_PROP_COLOR_WAVE_B, 0);
	uint32_t col_fill = (uint32_t)aw_get_int_default(settings, ABSTRACT_PROP_COLOR_FILL, 0);
	uint32_t col_fire = (uint32_t)aw_get_int_default(settings, ABSTRACT_PROP_COLOR_FIRE, 0);

	if (col_wave_a == 0)
		col_wave_a = 0xFF00FF;
	if (col_wave_b == 0)
		col_wave_b = 0x00FFFF;
	if (col_fill == 0)
		col_fill = 0x220022;
	if (col_fire == 0)
		col_fire = 0xFFFF00;

	s->color = col_wave_a;

	s->colors.clear();
	s->colors.push_back(audio_wave_named_color{"wave_a", col_wave_a});
	s->colors.push_back(audio_wave_named_color{"wave_b", col_wave_b});
	s->colors.push_back(audio_wave_named_color{"fill", col_fill});
	s->colors.push_back(audio_wave_named_color{"firework", col_fire});

	int db_a = aw_get_int_default(settings, ABSTRACT_PROP_DB_WAVE_A, -10);
	int db_b = aw_get_int_default(settings, ABSTRACT_PROP_DB_WAVE_B, -20);
	int db_f = aw_get_int_default(settings, ABSTRACT_PROP_DB_FIRE, -12);

	db_a = std::clamp(db_a, -60, 0);
	db_b = std::clamp(db_b, -60, 0);
	db_f = std::clamp(db_f, -60, 0);

	int seg = aw_get_int_default(settings, ABSTRACT_PROP_SEGMENTS, 128);
	seg = std::clamp(seg, 32, 512);

	int thick_a = aw_get_int_default(settings, ABSTRACT_PROP_THICK_A, 2);
	int thick_b = aw_get_int_default(settings, ABSTRACT_PROP_THICK_B, 2);
	thick_a = std::clamp(thick_a, 1, 8);
	thick_b = std::clamp(thick_b, 1, 8);

	auto *d = static_cast<abstract_theme_data *>(s->theme_data);
	if (!d) {
		d = new abstract_theme_data{};
		s->theme_data = d;
	}

	d->db_wave_a = (float)db_a;
	d->db_wave_b = (float)db_b;
	d->db_fire = (float)db_f;
	d->segments = (uint32_t)seg;
	d->thick_a = thick_a;
	d->thick_b = thick_b;

	d->initialized = false;
}

// ─────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────

static inline float db_from_amp(float a)
{
	if (a <= 1e-6f)
		return -120.0f;
	return 20.0f * log10f(a);
}

static float normalize_db_range(float a, float targetDb, float floorDb)
{
	if (a <= 1e-6f)
		return 0.0f;

	float db = db_from_amp(a);

	if (db <= floorDb)
		return 0.0f;

	if (targetDb > floorDb) {
		float norm = (db - floorDb) / (targetDb - floorDb);
		if (norm < 0.0f)
			norm = 0.0f;
		if (norm > 1.0f)
			norm = 1.0f;
		return norm;
	}

	return 0.0f;
}

// ─────────────────────────────────────────────
// Drawing
// ─────────────────────────────────────────────

static void abstract_theme_draw(audio_wave_source *s, gs_eparam_t *color_param)
{
	if (!s || s->width <= 0 || s->height <= 0)
		return;

	const size_t frames = s->wave.size();
	if (frames < 2)
		return;

	auto *d = static_cast<abstract_theme_data *>(s->theme_data);
	if (!d) {
		d = new abstract_theme_data{};
		s->theme_data = d;
	}

	const float w = (float)s->width;
	const float h = (float)s->height;
	const float cx = w * 0.5f;
	const float cy = h * 0.5f;

	const float Rbase = std::min(w, h) * 0.25f;
	const float RextA = std::min(w, h) * 0.20f;
	const float RextB = std::min(w, h) * 0.15f;
	const float Rfire = std::min(w, h) * 0.30f;

	const uint32_t segments = std::max(d->segments, 32u);

	const uint32_t col_wave_a = audio_wave_get_color(s, 0, s->color);
	const uint32_t col_wave_b = audio_wave_get_color(s, 1, col_wave_a);
	const uint32_t col_fill = audio_wave_get_color(s, 2, col_wave_b);
	const uint32_t col_fire = audio_wave_get_color(s, 3, col_fill);

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

	if (d->prev_r1.size() != segments) {
		d->prev_r1.assign(segments, Rbase);
		d->prev_r2.assign(segments, Rbase * 0.8f);
		d->initialized = false;
	}

	std::vector<float> r1(segments), r2(segments);

	const float floorDb = -60.0f;
	const float alpha_time = 0.30f;

	for (uint32_t i = 0; i < segments; ++i) {
		float a = amp_smooth[i];

		float nA = normalize_db_range(a, d->db_wave_a, floorDb);
		float nB = normalize_db_range(a, d->db_wave_b, floorDb);

		nA = audio_wave_apply_curve(s, nA);
		nB = audio_wave_apply_curve(s, nB);

		float r1_target = Rbase + nA * RextA;
		float r2_target = Rbase * 0.7f + nB * RextB;

		if (!d->initialized) {
			d->prev_r1[i] = r1_target;
			d->prev_r2[i] = r2_target;
		}

		float r1_s = d->prev_r1[i] + alpha_time * (r1_target - d->prev_r1[i]);
		float r2_s = d->prev_r2[i] + alpha_time * (r2_target - d->prev_r2[i]);

		d->prev_r1[i] = r1_s;
		d->prev_r2[i] = r2_s;

		r1[i] = r1_s;
		r2[i] = r2_s;
	}

	d->initialized = true;

	std::vector<float> cos_t(segments), sin_t(segments);
	for (uint32_t i = 0; i < segments; ++i) {
		const float t = ((float)i / (float)segments) * 2.0f * (float)M_PI;
		cos_t[i] = std::cos(t);
		sin_t[i] = std::sin(t);
	}

	std::vector<float> r_outer(segments);
	for (uint32_t i = 0; i < segments; ++i) {
		r_outer[i] = std::max(r1[i], r2[i]);
	}

	std::vector<float> x_outer(segments), y_outer(segments);
	for (uint32_t i = 0; i < segments; ++i) {
		x_outer[i] = cx + cos_t[i] * r_outer[i];
		y_outer[i] = cy + sin_t[i] * r_outer[i];
	}

	gs_matrix_push();

	if (color_param)
		audio_wave_set_solid_color(color_param, col_fill);

	gs_render_start(true);
	for (uint32_t i = 0; i < segments; ++i) {
		uint32_t next = (i + 1) % segments;

		gs_vertex2f(cx, cy);
		gs_vertex2f(x_outer[i], y_outer[i]);
		gs_vertex2f(x_outer[next], y_outer[next]);
	}
	gs_render_stop(GS_TRIS);

	if (color_param)
		audio_wave_set_solid_color(color_param, col_wave_a);

	int thickA = d->thick_a;
	if (thickA < 1)
		thickA = 1;

	{
		const float half = (float)(thickA - 1) * 0.5f;

		for (int t = 0; t < thickA; ++t) {
			const float offset = (float)t - half;

			gs_render_start(true);
			for (uint32_t i = 0; i <= segments; ++i) {
				uint32_t idx = (i == segments) ? 0 : i;

				float rad = r1[idx] + offset;
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
		audio_wave_set_solid_color(color_param, col_wave_b);

	int thickB = d->thick_b;
	if (thickB < 1)
		thickB = 1;

	{
		const float half = (float)(thickB - 1) * 0.5f;

		for (int t = 0; t < thickB; ++t) {
			const float offset = (float)t - half;

			gs_render_start(true);
			for (uint32_t i = 0; i <= segments; ++i) {
				uint32_t idx = (i == segments) ? 0 : i;

				float rad = r2[idx] + offset;
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
		audio_wave_set_solid_color(color_param, col_fire);

	gs_render_start(true);
	for (uint32_t i = 0; i < segments; ++i) {
		float a = amp_smooth[i];
		if (a <= 1e-6f)
			continue;

		float db = db_from_amp(a);
		if (db < d->db_fire)
			continue;

		float over = (db - d->db_fire) / 20.0f;
		if (over < 0.0f)
			over = 0.0f;
		if (over > 1.0f)
			over = 1.0f;

		float r_start = r1[i];
		float r_end = r1[i] + over * Rfire;

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

static void abstract_theme_destroy_data(audio_wave_source *s)
{
	if (!s || !s->theme_data)
		return;

	auto *d = static_cast<abstract_theme_data *>(s->theme_data);
	delete d;
	s->theme_data = nullptr;
}

static const audio_wave_theme k_abstract_theme = {
	k_theme_id_abstract,   k_theme_name_abstract, abstract_theme_add_properties,
	abstract_theme_update, abstract_theme_draw,   abstract_theme_destroy_data,
};

void audio_wave_register_abstract_theme()
{
	audio_wave_register_theme(&k_abstract_theme);
}
