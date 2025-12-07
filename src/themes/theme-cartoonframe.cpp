#include "theme-cartoonframe.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <graphics/graphics.h>

#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(x) (void)(x)
#endif

static const char *k_theme_id_cartoonframe = "cartoon_frame";
static const char *k_theme_name_cartoonframe = "Cartoon Camera Frame";

static const char *CFR_PROP_COLOR_FRAME = "cfr_color_frame";
static const char *CFR_PROP_COLOR_SPARK = "cfr_color_spark";

static const char *CFR_PROP_FRAME_THICKNESS = "cfr_frame_thickness";
static const char *CFR_PROP_FRAME_INSET = "cfr_frame_inset";
static const char *CFR_PROP_CORNER_LEN = "cfr_corner_length_ratio";

static const char *CFR_PROP_SPARK_COUNT = "cfr_spark_count";
static const char *CFR_PROP_SPARK_LENGTH = "cfr_spark_length";
static const char *CFR_PROP_SPARK_ENERGY = "cfr_spark_energy";
static const char *CFR_PROP_SPARK_MIN_LEVEL = "cfr_spark_min_level";
static const char *CFR_PROP_SPARK_SPEED = "cfr_spark_speed";

// ─────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────

static float cfr_clamp_float(float v, float lo, float hi)
{
	if (v < lo)
		return lo;
	if (v > hi)
		return hi;
	return v;
}

static float cfr_pseudo_rand01(uint32_t seed)
{
	uint32_t x = seed;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return (float)(x & 0x00FFFFFFu) / (float)0x01000000u;
}

struct cfr_spark {
	float pos = 0.0f;
	float life = 0.0f;
	float maxLife = 1.0f;
	float speed = 0.0f;
};

// Per-source theme data
struct cartoonframe_theme_data {
	int frame_thickness = 6;
	float inset_ratio = 0.08f;
	float corner_len_ratio = 0.22f;

	uint32_t spark_count = 40;
	float spark_length = 50.0f;
	float spark_energy = 0.8f;
	float spark_min_level = 0.25f;
	float spark_speed = 1.0f;

	std::vector<cfr_spark> sparks;

	float phase = 0.0f;
};

// ─────────────────────────────────────────────
// Properties
// ─────────────────────────────────────────────

static void cartoonframe_theme_add_properties(obs_properties_t *props)
{
	obs_properties_add_color(props, CFR_PROP_COLOR_FRAME, "Frame Color");
	obs_properties_add_color(props, CFR_PROP_COLOR_SPARK, "Sparkle Color");
	obs_properties_add_int_slider(props, CFR_PROP_FRAME_THICKNESS, "Frame Thickness", 1, 20, 1);
	obs_properties_add_float_slider(props, CFR_PROP_FRAME_INSET, "Frame Inset (relative to canvas)", 0.0, 0.4,
					0.01);
	obs_properties_add_float_slider(props, CFR_PROP_CORNER_LEN, "Corner Length (fraction of side)", 0.05, 0.5,
					0.01);
	obs_properties_add_int_slider(props, CFR_PROP_SPARK_COUNT, "Spark Count", 0, 200, 2);
	obs_properties_add_int_slider(props, CFR_PROP_SPARK_LENGTH, "Spark Length (px)", 5, 200, 5);
	obs_properties_add_float_slider(props, CFR_PROP_SPARK_ENERGY, "Spark Energy Response", 0.0, 2.0, 0.05);
	obs_properties_add_float_slider(props, CFR_PROP_SPARK_MIN_LEVEL, "Spark Min Level (0..1)", 0.0, 1.0, 0.05);
	obs_properties_add_float_slider(props, CFR_PROP_SPARK_SPEED, "Spark Base Speed", 0.0, 5.0, 0.05);
}

static void cartoonframe_rebuild_sparks(cartoonframe_theme_data *d)
{
	if (!d)
		return;

	d->sparks.clear();
	d->sparks.resize(d->spark_count);

	for (uint32_t i = 0; i < d->spark_count; ++i) {
		float r0 = cfr_pseudo_rand01(i * 11u + 3u);
		float r1 = cfr_pseudo_rand01(i * 23u + 7u);
		float r2 = cfr_pseudo_rand01(i * 41u + 13u);

		cfr_spark s;
		s.pos = r0;
		s.maxLife = 0.6f + r1 * 1.8f;
		s.life = r2 * s.maxLife;
		s.speed = 0.5f + r1 * 1.5f;

		d->sparks[i] = s;
	}
}

static void cartoonframe_theme_update(audio_wave_source *s, obs_data_t *settings)
{
	if (!s || !settings)
		return;

	s->theme_style_id = "default";

	uint32_t col_frame = (uint32_t)aw_get_int_default(settings, CFR_PROP_COLOR_FRAME, 0);
	uint32_t col_spark = (uint32_t)aw_get_int_default(settings, CFR_PROP_COLOR_SPARK, 0);

	if (col_frame == 0)
		col_frame = 0xF2B24B;
	if (col_spark == 0)
		col_spark = 0xFFFFDD;

	s->color = col_frame;

	s->colors.clear();
	s->colors.push_back(audio_wave_named_color{"frame", col_frame});
	s->colors.push_back(audio_wave_named_color{"sparkles", col_spark});

	int frame_thickness = (int)aw_get_int_default(settings, CFR_PROP_FRAME_THICKNESS, 6);
	frame_thickness = std::clamp(frame_thickness, 1, 20);

	double inset = aw_get_float_default(settings, CFR_PROP_FRAME_INSET, 0.08f);
	inset = std::clamp(inset, 0.0, 0.4);

	double corner_len_ratio = aw_get_float_default(settings, CFR_PROP_CORNER_LEN, 0.22f);
	corner_len_ratio = std::clamp(corner_len_ratio, 0.05, 0.5);

	int spark_count = (int)aw_get_int_default(settings, CFR_PROP_SPARK_COUNT, 40);
	spark_count = std::clamp(spark_count, 0, 200);

	int spark_len = (int)aw_get_int_default(settings, CFR_PROP_SPARK_LENGTH, 50);
	spark_len = std::clamp(spark_len, 5, 200);

	double spark_energy = aw_get_float_default(settings, CFR_PROP_SPARK_ENERGY, 0.8f);
	spark_energy = std::clamp(spark_energy, 0.0, 2.0);

	double spark_min_level = aw_get_float_default(settings, CFR_PROP_SPARK_MIN_LEVEL, 0.25f);
	spark_min_level = std::clamp(spark_min_level, 0.0, 1.0);

	double spark_speed = aw_get_float_default(settings, CFR_PROP_SPARK_SPEED, 1.0f);
	spark_speed = std::clamp(spark_speed, 0.0, 5.0);

	auto *d = static_cast<cartoonframe_theme_data *>(s->theme_data);
	if (!d) {
		d = new cartoonframe_theme_data{};
		s->theme_data = d;
	}

	d->frame_thickness = frame_thickness;
	d->inset_ratio = (float)inset;
	d->corner_len_ratio = (float)corner_len_ratio;

	d->spark_count = (uint32_t)spark_count;
	d->spark_length = (float)spark_len;
	d->spark_energy = (float)spark_energy;
	d->spark_min_level = (float)spark_min_level;
	d->spark_speed = (float)spark_speed;

	if (d->sparks.size() != d->spark_count) {
		cartoonframe_rebuild_sparks(d);
	}

	if (s->frame_density < 60)
		s->frame_density = 60;
}

static void cfr_rect_perimeter_point_and_normal(float t, float hx, float hy, float &x, float &y, float &nx, float &ny)
{
	float edge = t * 4.0f;
	int side = (int)std::floor(edge);
	float u = edge - (float)side;

	switch (side) {
	case 0:
	default:
		x = -hx + 2.0f * hx * u;
		y = -hy;
		nx = 0.0f;
		ny = -1.0f;
		break;
	case 1:
		x = hx;
		y = -hy + 2.0f * hy * u;
		nx = 1.0f;
		ny = 0.0f;
		break;
	case 2:
		x = hx - 2.0f * hx * u;
		y = hy;
		nx = 0.0f;
		ny = 1.0f;
		break;
	case 3:
		x = -hx;
		y = hy - 2.0f * hy * u;
		nx = -1.0f;
		ny = 0.0f;
		break;
	}
}

// ─────────────────────────────────────────────
// Drawing
// ─────────────────────────────────────────────

static void cartoonframe_theme_draw(audio_wave_source *s, gs_eparam_t *color_param)
{
	if (!s || s->width <= 0 || s->height <= 0)
		return;

	if (s->wave.empty())
		audio_wave_build_wave(s);

	const size_t frames = s->wave.size();
	if (frames < 2)
		return;

	auto *d = static_cast<cartoonframe_theme_data *>(s->theme_data);
	if (!d) {
		d = new cartoonframe_theme_data{};
		s->theme_data = d;
	}

	const float w = (float)s->width;
	const float h = (float)s->height;
	const float cx = w * 0.5f;
	const float cy = h * 0.5f;

	const float min_dim = std::min(w, h);
	const float margin = d->inset_ratio * min_dim;

	const float hx = std::max(0.0f, w * 0.5f - margin);
	const float hy = std::max(0.0f, h * 0.5f - margin);

	const float sideX = 2.0f * hx;
	const float sideY = 2.0f * hy;

	const float cornerLenX = sideX * d->corner_len_ratio;
	const float cornerLenY = sideY * d->corner_len_ratio;

	const uint32_t col_frame = audio_wave_get_color(s, 0, s->color);
	const uint32_t col_spark = audio_wave_get_color(s, 1, 0xFFFFFF);

	float max_a = 0.0f;
	for (size_t i = 0; i < frames; ++i) {
		float v = s->wave[i];
		if (v > max_a)
			max_a = v;
	}
	max_a = cfr_clamp_float(max_a, 0.0f, 1.0f);
	float v_global = audio_wave_apply_curve(s, max_a);

	int base_thick = d->frame_thickness;
	int extra_thick = (int)std::round(v_global * 4.0f);
	int thick = std::clamp(base_thick + extra_thick, 1, 30);

	gs_matrix_push();

	if (color_param)
		audio_wave_set_solid_color(color_param, col_frame);

	for (int t = 0; t < thick; ++t) {
		float offset = (float)t - (float)(thick - 1) * 0.5f;

		gs_render_start(true);

		{
			float x0 = cx - hx + offset;
			float y0 = cy - hy + offset;
			float x1 = x0 + cornerLenX;
			float y1 = y0 + cornerLenY;

			gs_vertex2f(x0, y0);
			gs_vertex2f(x1, y0);

			gs_vertex2f(x0, y0);
			gs_vertex2f(x0, y1);
		}

		{
			float x0 = cx + hx + offset;
			float y0 = cy - hy + offset;
			float x1 = x0 - cornerLenX;
			float y1 = y0 + cornerLenY;

			gs_vertex2f(x0, y0);
			gs_vertex2f(x1, y0);

			gs_vertex2f(x0, y0);
			gs_vertex2f(x0, y1);
		}

		{
			float x0 = cx + hx + offset;
			float y0 = cy + hy + offset;
			float x1 = x0 - cornerLenX;
			float y1 = y0 - cornerLenY;

			gs_vertex2f(x0, y0);
			gs_vertex2f(x1, y0);

			gs_vertex2f(x0, y0);
			gs_vertex2f(x0, y1);
		}

		{
			float x0 = cx - hx + offset;
			float y0 = cy + hy + offset;
			float x1 = x0 + cornerLenX;
			float y1 = y0 - cornerLenY;

			gs_vertex2f(x0, y0);
			gs_vertex2f(x1, y0);

			gs_vertex2f(x0, y0);
			gs_vertex2f(x0, y1);
		}

		gs_render_stop(GS_LINES);
	}

	if (color_param)
		audio_wave_set_solid_color(color_param, col_spark);

	if (d->sparks.size() != d->spark_count)
		cartoonframe_rebuild_sparks(d);

	const float dt = 0.03f;
	const float base_len = d->spark_length;

	gs_render_start(true);

	for (uint32_t i = 0; i < d->sparks.size(); ++i) {
		auto &sp = d->sparks[i];

		float pos_norm = sp.pos;
		pos_norm -= std::floor(pos_norm);
		if (pos_norm < 0.0f)
			pos_norm += 1.0f;

		size_t idx = (size_t)(pos_norm * (float)(frames - 1));
		if (idx >= frames)
			idx = frames - 1;

		float a_local = s->wave[idx];
		a_local = cfr_clamp_float(a_local, 0.0f, 1.0f);
		float v_local = audio_wave_apply_curve(s, a_local);

		bool active = (v_local >= d->spark_min_level);
		if (!active) {
			sp.life += dt * 0.6f;
			if (sp.life > sp.maxLife) {
				uint32_t seed = i * 97u + 17u;
				float r0 = cfr_pseudo_rand01(seed);
				float r1 = cfr_pseudo_rand01(seed * 3u + 11u);
				float r2 = cfr_pseudo_rand01(seed * 5u + 23u);

				sp.pos = r0;
				sp.maxLife = 0.6f + r1 * 1.8f;
				sp.life = 0.0f;
				sp.speed = 0.5f + r2 * 1.5f;
			}

			float speed_scale_idle = 0.3f;
			sp.pos += (d->spark_speed + sp.speed * speed_scale_idle) * dt;
			sp.pos -= std::floor(sp.pos);
			continue;
		}

		sp.life += dt * (0.4f + v_local * 2.0f);
		if (sp.life > sp.maxLife) {
			uint32_t seed = i * 101u + 31u;
			float r0 = cfr_pseudo_rand01(seed);
			float r1 = cfr_pseudo_rand01(seed * 7u + 19u);
			float r2 = cfr_pseudo_rand01(seed * 11u + 23u);

			sp.pos = r0;
			sp.maxLife = 0.6f + r1 * 2.0f;
			sp.life = 0.0f;
			sp.speed = 0.5f + r2 * 1.6f;
		}

		float speed_scale = 0.2f + v_local * d->spark_energy;
		sp.pos += (d->spark_speed + sp.speed * speed_scale) * dt;
		sp.pos -= std::floor(sp.pos);

		float phase = cfr_clamp_float(sp.life / sp.maxLife, 0.0f, 1.0f);
		float life_intensity = (phase < 0.5f) ? (phase * 2.0f) : ((1.0f - phase) * 2.0f);
		life_intensity = cfr_clamp_float(life_intensity, 0.0f, 1.0f);

		float intensity = life_intensity * (0.3f + 0.7f * v_local);
		intensity = cfr_clamp_float(intensity, 0.0f, 1.0f);

		float len = base_len * (0.3f + 0.7f * intensity);

		float px, py, nx, ny;
		cfr_rect_perimeter_point_and_normal(sp.pos, hx, hy, px, py, nx, ny);

		float sx = cx + px + nx * (float)thick * 0.5f;
		float sy = cy + py + ny * (float)thick * 0.5f;

		float ex = sx + nx * len;
		float ey = sy + ny * len;

		gs_vertex2f(sx, sy);
		gs_vertex2f(ex, ey);
	}

	gs_render_stop(GS_LINES);

	gs_matrix_pop();
}

static void cartoonframe_theme_destroy_data(audio_wave_source *s)
{
	if (!s || !s->theme_data)
		return;

	auto *d = static_cast<cartoonframe_theme_data *>(s->theme_data);
	delete d;
	s->theme_data = nullptr;
}

static const audio_wave_theme k_cartoonframe_theme = {k_theme_id_cartoonframe,
						      k_theme_name_cartoonframe,
						      cartoonframe_theme_add_properties,
						      cartoonframe_theme_update,
						      cartoonframe_theme_draw,
						      cartoonframe_theme_destroy_data,
						      nullptr};

void audio_wave_register_cartoonframe_theme()
{
	audio_wave_register_theme(&k_cartoonframe_theme);
}
