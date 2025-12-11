#include "theme-lightning.hpp"

#include <algorithm>
#include <cmath>
#include <vector>
#include <cstring>

#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(x) (void)(x)
#endif

static const char *k_theme_id_lightning = "lightning";
static const char *k_theme_name_lightning = "Storm Lightning";

// ─────────────────────────────────────────────
// Property keys
// ─────────────────────────────────────────────

static const char *LIGHT_PROP_COLOR_CORE = "lt_color_core";
static const char *LIGHT_PROP_COLOR_GLOW = "lt_color_glow";

static const char *LIGHT_PROP_DB_THRESHOLD = "lt_db_threshold";
static const char *LIGHT_PROP_DB_FULLSCALE = "lt_db_fullscale";

static const char *LIGHT_PROP_BOLT_COUNT = "lt_bolt_count";
static const char *LIGHT_PROP_JAGGED = "lt_jagged";
static const char *LIGHT_PROP_THICK_CORE = "lt_thick_core";
static const char *LIGHT_PROP_THICK_GLOW = "lt_thick_glow";

// ─────────────────────────────────────────────
// Theme data
// ─────────────────────────────────────────────

struct lightning_theme_data {
	std::vector<float> prev_length;
	bool initialized = false;

	float db_threshold = -24.0f;
	float db_fullscale = -6.0f;

	uint32_t bolts = 64;
	int jagged = 8;
	int thick_core = 1;
	int thick_glow = 3;
};

// ─────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────

static inline float db_from_amp(float a)
{
	if (a <= 1e-6f)
		return -120.0f;
	return 20.0f * log10f(a);
}

static inline float clamp01(float v)
{
	if (v < 0.0f)
		return 0.0f;
	if (v > 1.0f)
		return 1.0f;
	return v;
}

// simple deterministic hash -> [0,1)
static inline float hash11(float x)
{
	float s = sinf(x * 12.9898f) * 43758.5453f;
	return s - floorf(s);
}

// ─────────────────────────────────────────────
// Properties
// ─────────────────────────────────────────────

static void lightning_theme_add_properties(obs_properties_t *props)
{
	obs_properties_add_color(props, LIGHT_PROP_COLOR_CORE, "Core Lightning Color");
	obs_properties_add_color(props, LIGHT_PROP_COLOR_GLOW, "Glow Color");

	obs_properties_add_int_slider(props, LIGHT_PROP_DB_THRESHOLD, "Strike Threshold (dB)", -60, 0, 1);
	obs_properties_add_int_slider(props, LIGHT_PROP_DB_FULLSCALE, "Full Intensity dB", -60, 0, 1);

	obs_properties_add_int_slider(props, LIGHT_PROP_BOLT_COUNT, "Lightning Rays", 8, 256, 1);
	obs_properties_add_int_slider(props, LIGHT_PROP_JAGGED, "Jaggedness", 3, 32, 1);

	obs_properties_add_int_slider(props, LIGHT_PROP_THICK_CORE, "Core Thickness", 1, 8, 1);
	obs_properties_add_int_slider(props, LIGHT_PROP_THICK_GLOW, "Glow Thickness", 1, 8, 1);
}

static void lightning_theme_update(audio_wave_source *s, obs_data_t *settings)
{
	if (!s || !settings)
		return;

	// keep style id predictable
	s->theme_style_id = "default";

	uint32_t col_core = (uint32_t)aw_get_int_default(settings, LIGHT_PROP_COLOR_CORE, 0);
	uint32_t col_glow = (uint32_t)aw_get_int_default(settings, LIGHT_PROP_COLOR_GLOW, 0);

	// fallbacks
	if (col_core == 0)
		col_core = 0xFFFFFF; // bright white core
	if (col_glow == 0)
		col_glow = 0x33CCFF; // cyan/blue glow

	s->color = col_core;

	s->colors.clear();
	s->colors.push_back(audio_wave_named_color{"core", col_core});
	s->colors.push_back(audio_wave_named_color{"glow", col_glow});

	int db_th = aw_get_int_default(settings, LIGHT_PROP_DB_THRESHOLD, -24);
	int db_fs = aw_get_int_default(settings, LIGHT_PROP_DB_FULLSCALE, -6);

	db_th = std::clamp(db_th, -60, 0);
	db_fs = std::clamp(db_fs, -60, 0);

	// ensure fullscale is above threshold, otherwise the mapping breaks
	if (db_fs <= db_th)
		db_fs = db_th + 1;

	int bolt_count = aw_get_int_default(settings, LIGHT_PROP_BOLT_COUNT, 64);
	int jagged = aw_get_int_default(settings, LIGHT_PROP_JAGGED, 8);
	int thick_core = aw_get_int_default(settings, LIGHT_PROP_THICK_CORE, 1);
	int thick_glow = aw_get_int_default(settings, LIGHT_PROP_THICK_GLOW, 3);

	bolt_count = std::clamp(bolt_count, 8, 256);
	jagged = std::clamp(jagged, 3, 32);
	thick_core = std::clamp(thick_core, 1, 8);
	thick_glow = std::clamp(thick_glow, 1, 8);

	auto *d = static_cast<lightning_theme_data *>(s->theme_data);
	if (!d) {
		d = new lightning_theme_data{};
		s->theme_data = d;
	}

	d->db_threshold = (float)db_th;
	d->db_fullscale = (float)db_fs;
	d->bolts = (uint32_t)bolt_count;
	d->jagged = jagged;
	d->thick_core = thick_core;
	d->thick_glow = thick_glow;

	d->initialized = false;
}

// ─────────────────────────────────────────────
// Drawing (NO BACKGROUND)
// ─────────────────────────────────────────────

static void lightning_theme_draw(audio_wave_source *s, gs_eparam_t *color_param)
{
	if (!s || s->width <= 0 || s->height <= 0)
		return;

	const size_t frames = s->wave.size();
	if (frames < 2)
		return;

	auto *d = static_cast<lightning_theme_data *>(s->theme_data);
	if (!d) {
		d = new lightning_theme_data{};
		s->theme_data = d;
	}

	const float w = (float)s->width;
	const float h = (float)s->height;
	const float cx = w * 0.5f;
	const float cy = h * 0.5f;

	const float maxR = std::min(w, h) * 0.5f * 0.95f; // fade out near edges

	const uint32_t bolts = std::max(d->bolts, 8u);

	const uint32_t col_core = audio_wave_get_color(s, 0, s->color);
	const uint32_t col_glow = audio_wave_get_color(s, 1, col_core);

	// ensure buffers
	if (d->prev_length.size() != bolts) {
		d->prev_length.assign(bolts, 0.0f);
		d->initialized = false;
	}

	// ── Build per-bolt amplitude → radial length
	const float alpha_time = 0.25f; // smoothing
	for (uint32_t i = 0; i < bolts; ++i) {
		const float u = (float)i / (float)(bolts - 1);
		const size_t idx = (size_t)(u * (float)(frames - 1));

		float a = (idx < frames) ? s->wave[idx] : 0.0f;
		if (a < 1e-6f)
			a = 0.0f;

		float db = db_from_amp(a);

		float length_target = 0.0f;

		// Only create a strike above threshold
		if (db > d->db_threshold) {
			float t = (db - d->db_threshold) / (d->db_fullscale - d->db_threshold + 1e-3f);
			t = clamp01(t);
			length_target = t * maxR;
		}

		if (!d->initialized)
			d->prev_length[i] = length_target;

		float L = d->prev_length[i] + alpha_time * (length_target - d->prev_length[i]);

		if (L < 0.0f)
			L = 0.0f;

		d->prev_length[i] = L;
	}

	d->initialized = true;

	gs_matrix_push();

	// Precompute some constants
	const float twoPi = 2.0f * (float)M_PI;
	const int steps_base = std::max(d->jagged, 3);

	// Max angular offset (radians) for jaggedness
	const float maxAngleOffsetBase = 0.35f;

	// ── Glow pass: thicker, softer
	if (color_param)
		audio_wave_set_solid_color(color_param, col_glow);

	int thickGlow = d->thick_glow;
	if (thickGlow < 1)
		thickGlow = 1;

	{
		const float half = (float)(thickGlow - 1) * 0.5f;

		for (uint32_t i = 0; i < bolts; ++i) {
			float L = d->prev_length[i];
			if (L <= 1.0f)
				continue;

			const float baseAngle = ((float)i / (float)bolts) * twoPi;
			const int steps = steps_base;
			const float stepR = L / (float)steps;

			for (int t = 0; t < thickGlow; ++t) {
				const float radialOffset = (float)t - half;

				gs_render_start(true);

				for (int j = 0; j <= steps; ++j) {
					float rr = stepR * (float)j + radialOffset;
					if (rr < 0.0f)
						rr = 0.0f;

					// fade jaggedness toward center & edge
					float v = (float)j / (float)steps;
					float fadeJagged = (1.0f - fabsf(v - 0.7f));
					fadeJagged = clamp01(fadeJagged);

					float n = hash11((float)i * 13.37f + (float)j * 7.91f);
					float angleOffset = (n - 0.5f) * maxAngleOffsetBase * fadeJagged;

					float ang = baseAngle + angleOffset;

					const float x = cx + cosf(ang) * rr;
					const float y = cy + sinf(ang) * rr;

					gs_vertex2f(x, y);
				}

				gs_render_stop(GS_LINESTRIP);
			}
		}
	}

	// ── Core pass: thinner, sharper line sitting on top
	if (color_param)
		audio_wave_set_solid_color(color_param, col_core);

	int thickCore = d->thick_core;
	if (thickCore < 1)
		thickCore = 1;

	{
		const float half = (float)(thickCore - 1) * 0.5f;

		for (uint32_t i = 0; i < bolts; ++i) {
			float L = d->prev_length[i];
			if (L <= 1.0f)
				continue;

			const float baseAngle = ((float)i / (float)bolts) * twoPi;
			const int steps = steps_base;
			const float stepR = L / (float)steps;

			for (int t = 0; t < thickCore; ++t) {
				const float radialOffset = (float)t - half;

				gs_render_start(true);

				for (int j = 0; j <= steps; ++j) {
					float rr = stepR * (float)j + radialOffset;
					if (rr < 0.0f)
						rr = 0.0f;

					float v = (float)j / (float)steps;
					float fadeJagged = (1.0f - fabsf(v - 0.7f));
					fadeJagged = clamp01(fadeJagged);

					float n = hash11((float)i * 31.17f + (float)j * 19.31f);
					float angleOffset = (n - 0.5f) * maxAngleOffsetBase * fadeJagged;

					float ang = baseAngle + angleOffset;

					const float x = cx + cosf(ang) * rr;
					const float y = cy + sinf(ang) * rr;

					gs_vertex2f(x, y);
				}

				gs_render_stop(GS_LINESTRIP);
			}
		}
	}

	gs_matrix_pop();
}

// ─────────────────────────────────────────────
// Destroy
// ─────────────────────────────────────────────

static void lightning_theme_destroy_data(audio_wave_source *s)
{
	if (!s || !s->theme_data)
		return;

	auto *d = static_cast<lightning_theme_data *>(s->theme_data);
	delete d;
	s->theme_data = nullptr;
}

// ─────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────

static const audio_wave_theme k_lightning_theme = {
	k_theme_id_lightning,   k_theme_name_lightning, lightning_theme_add_properties,
	lightning_theme_update, lightning_theme_draw,   lightning_theme_destroy_data,
};

void audio_wave_register_lightning_theme()
{
	audio_wave_register_theme(&k_lightning_theme);
}
