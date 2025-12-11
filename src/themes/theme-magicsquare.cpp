#include "theme-magicsquare.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <graphics/graphics.h>

#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(x) (void)(x)
#endif

static const char *k_theme_id_magicsquare = "magic_square";
static const char *k_theme_name_magicsquare = "Magic Square Sparkles";

static const char *MSQ_PROP_COLOR_CORE = "msq_color_core";
static const char *MSQ_PROP_COLOR_RING = "msq_color_ring";
static const char *MSQ_PROP_COLOR_SPARK = "msq_color_spark";

static const char *MSQ_PROP_SEGMENTS = "msq_segments";
static const char *MSQ_PROP_VISCOSITY = "msq_viscosity";
static const char *MSQ_PROP_NOISE = "msq_noise";
static const char *MSQ_PROP_RING_THICKNESS = "msq_ring_thickness";
static const char *MSQ_PROP_ROT_SPEED = "msq_rotation_speed";

static const char *MSQ_PROP_CORE_SIZE = "msq_core_size";
static const char *MSQ_PROP_RING_SIZE = "msq_ring_size";

static const char *MSQ_PROP_SPARK_COUNT = "msq_spark_count";
static const char *MSQ_PROP_SPARK_LENGTH = "msq_spark_length";
static const char *MSQ_PROP_SPARK_ORBIT = "msq_spark_orbit";
static const char *MSQ_PROP_SPARK_ENERGY = "msq_spark_energy_response";
static const char *MSQ_PROP_SPARK_MIN_LEVEL = "msq_spark_min_level";

// ─────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────

static float msq_clamp_float(float v, float lo, float hi)
{
	if (v < lo)
		return lo;
	if (v > hi)
		return hi;
	return v;
}

static float msq_pseudo_rand01(uint32_t seed)
{
	uint32_t x = seed;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return (float)(x & 0x00FFFFFFu) / (float)0x01000000u;
}

struct msq_spark {
	float pos = 0.0f;
	float offset = 0.0f;
	float life = 0.0f;
	float maxLife = 1.0f;
	float speed = 0.0f;
};

struct magicsquare_theme_data {
	std::vector<float> prev_offset;
	bool initialized = false;

	uint32_t segments = 120;
	float viscosity = 0.65f;
	float noise_amount = 0.3f;
	int ring_thickness = 4;
	float rot_speed = 0.5f;
	float phase = 0.0f;

	float core_size = 1.0f;
	float ring_size = 1.0f;

	uint32_t spark_count = 40;
	float spark_length = 60.0f;
	float spark_orbit_mult = 1.20f;
	float spark_energy_resp = 0.6f;
	float spark_min_level = 0.25f;

	std::vector<msq_spark> sparks;
};

// ─────────────────────────────────────────────
// Properties
// ─────────────────────────────────────────────

static void magicsquare_theme_add_properties(obs_properties_t *props)
{
	obs_properties_add_color(props, MSQ_PROP_COLOR_CORE, "Core Glow Color");
	obs_properties_add_color(props, MSQ_PROP_COLOR_RING, "Ring Color");
	obs_properties_add_color(props, MSQ_PROP_COLOR_SPARK, "Sparkle Color");

	obs_properties_add_int_slider(props, MSQ_PROP_SEGMENTS, "Shape Resolution", 32, 512, 8);
	obs_properties_add_float_slider(props, MSQ_PROP_VISCOSITY, "Viscosity (Smoothness)", 0.0, 1.0, 0.05);
	obs_properties_add_float_slider(props, MSQ_PROP_NOISE, "Organic Wobble Amount", 0.0, 1.0, 0.05);
	obs_properties_add_int_slider(props, MSQ_PROP_RING_THICKNESS, "Ring Thickness", 0, 10, 1);
	obs_properties_add_float_slider(props, MSQ_PROP_ROT_SPEED, "Rotation Speed", 0.0, 5.0, 0.1);

	obs_properties_add_float_slider(props, MSQ_PROP_CORE_SIZE, "Core Size", 0.0, 2.0, 0.05);
	obs_properties_add_float_slider(props, MSQ_PROP_RING_SIZE, "Ring Size", 0.0, 2.0, 0.05);

	obs_properties_add_int_slider(props, MSQ_PROP_SPARK_COUNT, "Spark Count", 0, 200, 2);
	obs_properties_add_int_slider(props, MSQ_PROP_SPARK_LENGTH, "Spark Length (px)", 5, 200, 5);
	obs_properties_add_float_slider(props, MSQ_PROP_SPARK_ORBIT, "Spark Orbit Radius", 0.8, 2.0, 0.05);
	obs_properties_add_float_slider(props, MSQ_PROP_SPARK_ENERGY, "Spark Energy Response", 0.0, 1.5, 0.05);
	obs_properties_add_float_slider(props, MSQ_PROP_SPARK_MIN_LEVEL, "Spark Min Level (0..1)", 0.0, 1.0, 0.05);
}

static void magicsquare_rebuild_sparks(magicsquare_theme_data *d)
{
	if (!d)
		return;

	d->sparks.clear();
	d->sparks.resize(d->spark_count);

	for (uint32_t i = 0; i < d->spark_count; ++i) {
		float r0 = msq_pseudo_rand01(i * 11u + 3u);
		float r1 = msq_pseudo_rand01(i * 23u + 7u);
		float r2 = msq_pseudo_rand01(i * 41u + 13u);
		float r3 = msq_pseudo_rand01(i * 59u + 17u);

		msq_spark s;
		s.pos = r0;
		s.offset = 0.3f + r1 * 0.7f;
		s.maxLife = 0.7f + r2 * 1.8f;
		s.life = r3 * s.maxLife;
		s.speed = 0.2f + r1 * 1.2f;

		d->sparks[i] = s;
	}
}

static void magicsquare_theme_update(audio_wave_source *s, obs_data_t *settings)
{
	if (!s || !settings)
		return;

	s->theme_style_id = "default";

	uint32_t col_core = (uint32_t)aw_get_int_default(settings, MSQ_PROP_COLOR_CORE, 0);
	uint32_t col_ring = (uint32_t)aw_get_int_default(settings, MSQ_PROP_COLOR_RING, 0);
	uint32_t col_spark = (uint32_t)aw_get_int_default(settings, MSQ_PROP_COLOR_SPARK, 0);

	if (col_core == 0)
		col_core = 0xFF99DD;
	if (col_ring == 0)
		col_ring = 0x66CCFF;
	if (col_spark == 0)
		col_spark = 0xFFFFCC;

	s->color = col_ring;

	s->colors.clear();
	s->colors.push_back(audio_wave_named_color{"core", col_core});
	s->colors.push_back(audio_wave_named_color{"ring", col_ring});
	s->colors.push_back(audio_wave_named_color{"sparkles", col_spark});

	int seg = aw_get_int_default(settings, MSQ_PROP_SEGMENTS, 120);
	seg = std::clamp(seg, 32, 512);

	double visc = aw_get_float_default(settings, MSQ_PROP_VISCOSITY, 0.65f);
	visc = std::clamp(visc, 0.0, 1.0);

	double noise = aw_get_float_default(settings, MSQ_PROP_NOISE, 0.3f);
	noise = std::clamp(noise, 0.0, 1.0);

	int ring_thick = aw_get_int_default(settings, MSQ_PROP_RING_THICKNESS, 4);
	ring_thick = std::clamp(ring_thick, 0, 10);

	double rot = aw_get_float_default(settings, MSQ_PROP_ROT_SPEED, 0.5f);
	rot = std::clamp(rot, 0.0, 5.0);

	double core_size = aw_get_float_default(settings, MSQ_PROP_CORE_SIZE, 1.0f);
	core_size = std::clamp(core_size, 0.0, 2.0);

	double ring_size = aw_get_float_default(settings, MSQ_PROP_RING_SIZE, 1.0f);
	ring_size = std::clamp(ring_size, 0.0, 2.0);

	int spark_count = aw_get_int_default(settings, MSQ_PROP_SPARK_COUNT, 40);
	spark_count = std::clamp(spark_count, 0, 200);

	int spark_len = aw_get_int_default(settings, MSQ_PROP_SPARK_LENGTH, 60);
	spark_len = std::clamp(spark_len, 5, 200);

	double spark_orbit = aw_get_float_default(settings, MSQ_PROP_SPARK_ORBIT, 1.20f);
	spark_orbit = std::clamp(spark_orbit, 0.8, 2.0);

	double spark_energy = aw_get_float_default(settings, MSQ_PROP_SPARK_ENERGY, 0.6f);
	spark_energy = std::clamp(spark_energy, 0.0, 1.5);

	double spark_min_lvl = aw_get_float_default(settings, MSQ_PROP_SPARK_MIN_LEVEL, 0.25f);
	spark_min_lvl = std::clamp(spark_min_lvl, 0.0, 1.0);

	auto *d = static_cast<magicsquare_theme_data *>(s->theme_data);
	if (!d) {
		d = new magicsquare_theme_data{};
		s->theme_data = d;
	}

	d->segments = (uint32_t)seg;
	d->viscosity = (float)visc;
	d->noise_amount = (float)noise;
	d->ring_thickness = ring_thick;
	d->rot_speed = (float)rot;
	d->core_size = (float)core_size;
	d->ring_size = (float)ring_size;
	d->spark_count = (uint32_t)spark_count;
	d->spark_length = (float)spark_len;
	d->spark_orbit_mult = (float)spark_orbit;
	d->spark_energy_resp = (float)spark_energy;
	d->spark_min_level = (float)spark_min_lvl;
	d->initialized = false;

	if (d->sparks.size() != d->spark_count) {
		magicsquare_rebuild_sparks(d);
	}

	if (s->frame_density < 80)
		s->frame_density = 80;
}

static void msq_square_param(float t, float half, float &x, float &y)
{
	float edge = t * 4.0f;
	int side = (int)std::floor(edge);
	float u = edge - (float)side;

	switch (side) {
	case 0:
	default:
		x = -half + 2.0f * half * u;
		y = -half;
		break;
	case 1:
		x = half;
		y = -half + 2.0f * half * u;
		break;
	case 2:
		x = half - 2.0f * half * u;
		y = half;
		break;
	case 3:
		x = -half;
		y = half - 2.0f * half * u;
		break;
	}
}

// ─────────────────────────────────────────────
// Drawing
// ─────────────────────────────────────────────

static void magicsquare_theme_draw(audio_wave_source *s, gs_eparam_t *color_param)
{
	if (!s || s->width <= 0 || s->height <= 0)
		return;

	if (s->wave.empty())
		audio_wave_build_wave(s);

	const size_t frames = s->wave.size();
	if (frames < 2)
		return;

	auto *d = static_cast<magicsquare_theme_data *>(s->theme_data);
	if (!d) {
		d = new magicsquare_theme_data{};
		s->theme_data = d;
	}

	const float w = (float)s->width;
	const float h = (float)s->height;
	const float cx = w * 0.5f;
	const float cy = h * 0.5f;

	const float min_dim = std::min(w, h);
	const float half_base = min_dim * 0.32f;
	const float core_half = half_base * 0.6f * d->core_size;

	const float audio_extent = min_dim * 0.20f;
	const float noise_extent = min_dim * 0.10f;

	const float orbit_base = half_base * d->spark_orbit_mult;

	const uint32_t segments = std::max(d->segments, 32u);

	const uint32_t col_core = audio_wave_get_color(s, 0, 0xFF99DD);
	const uint32_t col_ring = audio_wave_get_color(s, 1, s->color);
	const uint32_t col_spark = audio_wave_get_color(s, 2, 0xFFFFFF);

	std::vector<float> amp(segments);
	for (uint32_t i = 0; i < segments; ++i) {
		const float u = (float)i / (float)segments;
		const size_t idx = (size_t)(u * (float)(frames - 1));
		amp[i] = (idx < frames) ? s->wave[idx] : 0.0f;
	}

	float max_a = 0.0f;
	for (float v : amp)
		if (v > max_a)
			max_a = v;
	max_a = msq_clamp_float(max_a, 0.0f, 1.0f);

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

	if (d->prev_offset.size() != segments) {
		d->prev_offset.assign(segments, 0.0f);
		d->initialized = false;
	}
	std::vector<float> offset(segments);

	const float base_alpha = 0.05f;
	const float extra_alpha = 0.35f;
	const float alpha_time = base_alpha + extra_alpha * (1.0f - d->viscosity);

	d->phase += d->rot_speed * (float)M_PI / 180.0f;
	if (d->phase > 2.0f * (float)M_PI)
		d->phase -= 2.0f * (float)M_PI;

	const int noise_harmonics = 2 + (int)std::round(d->noise_amount * 3.0f);

	std::vector<float> base_x(segments), base_y(segments);
	std::vector<float> norm_x(segments), norm_y(segments);

	for (uint32_t i = 0; i < segments; ++i) {
		const float t = (float)i / (float)segments;
		float bx, by;
		msq_square_param(t, half_base, bx, by);
		base_x[i] = bx;
		base_y[i] = by;

		float len = std::sqrt(bx * bx + by * by);
		if (len <= 1e-6f) {
			norm_x[i] = 0.0f;
			norm_y[i] = -1.0f;
		} else {
			norm_x[i] = bx / len;
			norm_y[i] = by / len;
		}
	}

	for (uint32_t i = 0; i < segments; ++i) {
		float a = amp_smooth[i];
		a = msq_clamp_float(a, 0.0f, 1.0f);

		float v = audio_wave_apply_curve(s, a);

		float target = v * audio_extent;

		const float angle = d->phase + (float)i * 0.15f;
		float wobble = 0.0f;
		for (int h2 = 1; h2 <= noise_harmonics; ++h2) {
			float contrib = std::sin(angle * (float)h2 + (float)h2 * 0.7f);
			wobble += contrib * (1.0f / (float)h2);
		}
		wobble /= (float)noise_harmonics;
		target += wobble * d->noise_amount * noise_extent;

		target *= d->ring_size;

		if (!d->initialized)
			d->prev_offset[i] = target;

		float off_s = d->prev_offset[i] + alpha_time * (target - d->prev_offset[i]);
		d->prev_offset[i] = off_s;
		offset[i] = off_s;
	}

	d->initialized = true;

	std::vector<float> pos_x(segments), pos_y(segments);
	for (uint32_t i = 0; i < segments; ++i) {
		pos_x[i] = base_x[i] + norm_x[i] * offset[i];
		pos_y[i] = base_y[i] + norm_y[i] * offset[i];
	}

	gs_matrix_push();

	if (d->core_size > 0.0f && core_half > 0.0f) {
		if (color_param)
			audio_wave_set_solid_color(color_param, col_core);

		gs_render_start(true);

		float x0 = cx - core_half;
		float y0 = cy - core_half;
		float x1 = cx + core_half;
		float y1 = cy + core_half;

		gs_vertex2f(x0, y0);
		gs_vertex2f(x1, y0);
		gs_vertex2f(x1, y1);

		gs_vertex2f(x0, y0);
		gs_vertex2f(x1, y1);
		gs_vertex2f(x0, y1);

		gs_render_stop(GS_TRIS);
	}

	if (d->ring_size > 0.0f && d->ring_thickness > 0) {
		if (color_param)
			audio_wave_set_solid_color(color_param, col_ring);

		int thickness = d->ring_thickness;
		const float half = (float)(thickness - 1) * 0.5f;

		for (int t = 0; t < thickness; ++t) {
			const float offset_line = (float)t - half;

			gs_render_start(true);
			for (uint32_t i = 0; i <= segments; ++i) {
				uint32_t idx = (i == segments) ? 0 : i;

				float px = pos_x[idx] + norm_x[idx] * offset_line;
				float py = pos_y[idx] + norm_y[idx] * offset_line;

				gs_vertex2f(cx + px, cy + py);
			}
			gs_render_stop(GS_LINESTRIP);
		}
	}

	if (color_param)
		audio_wave_set_solid_color(color_param, col_spark);

	if (d->sparks.size() != d->spark_count)
		magicsquare_rebuild_sparks(d);

	const float energy_dt = 0.02f + max_a * 0.08f * d->spark_energy_resp;
	const float base_len = d->spark_length;

	gs_render_start(true);

	for (uint32_t i = 0; i < d->sparks.size(); ++i) {
		auto &sp = d->sparks[i];

		float pos_norm = sp.pos;
		pos_norm -= std::floor(pos_norm);
		if (pos_norm < 0.0f)
			pos_norm += 1.0f;

		uint32_t idx = (uint32_t)(pos_norm * (float)segments);
		if (idx >= segments)
			idx = segments - 1;

		float a_local = amp_smooth[idx];
		a_local = msq_clamp_float(a_local, 0.0f, 1.0f);
		float v_local = audio_wave_apply_curve(s, a_local);

		bool active = (v_local >= d->spark_min_level);

		sp.life += energy_dt * (0.3f + v_local * 1.7f);
		if (sp.life > sp.maxLife) {
			uint32_t seed = i * 97u + 17u;
			float r0 = msq_pseudo_rand01(seed);
			float r1 = msq_pseudo_rand01(seed * 3u + 11u);
			float r2 = msq_pseudo_rand01(seed * 5u + 23u);

			sp.pos = r0;
			sp.offset = 0.3f + r1 * 0.7f;
			sp.maxLife = 0.6f + r2 * 2.0f;
			sp.life = 0.0f;
			sp.speed = 0.3f + r1 * 1.5f;
		}

		const float speed_scale = (0.05f + v_local * d->spark_energy_resp);
		sp.pos += sp.speed * energy_dt * speed_scale;
		sp.pos -= std::floor(sp.pos);

		if (!active)
			continue;

		float phase = msq_clamp_float(sp.life / sp.maxLife, 0.0f, 1.0f);
		float life_intensity = (phase < 0.5f) ? (phase * 2.0f) : ((1.0f - phase) * 2.0f);
		life_intensity = msq_clamp_float(life_intensity, 0.0f, 1.0f);

		float intensity = life_intensity * (0.3f + 0.7f * v_local);
		intensity = msq_clamp_float(intensity, 0.0f, 1.0f);

		float len = base_len * (0.3f + 0.7f * intensity);

		uint32_t idx2 = (uint32_t)(sp.pos * (float)segments);
		if (idx2 >= segments)
			idx2 = segments - 1;

		float px_rel = pos_x[idx2];
		float py_rel = pos_y[idx2];

		float base_dist = std::sqrt(px_rel * px_rel + py_rel * py_rel);
		if (base_dist < 1e-4f)
			base_dist = orbit_base;

		float orbit_dist = base_dist + sp.offset * orbit_base * 0.4f;

		float nx = px_rel / base_dist;
		float ny = py_rel / base_dist;

		float sx = cx + nx * orbit_dist;
		float sy = cy + ny * orbit_dist;

		float ex = sx + nx * len;
		float ey = sy + ny * len;

		gs_vertex2f(sx, sy);
		gs_vertex2f(ex, ey);
	}

	gs_render_stop(GS_LINES);

	gs_matrix_pop();
}

static void magicsquare_theme_destroy_data(audio_wave_source *s)
{
	if (!s || !s->theme_data)
		return;

	auto *d = static_cast<magicsquare_theme_data *>(s->theme_data);
	delete d;
	s->theme_data = nullptr;
}

static const audio_wave_theme k_magicsquare_theme = {k_theme_id_magicsquare,
						     k_theme_name_magicsquare,
						     magicsquare_theme_add_properties,
						     magicsquare_theme_update,
						     magicsquare_theme_draw,
						     magicsquare_theme_destroy_data,
						     nullptr};

void audio_wave_register_magicsquare_theme()
{
	audio_wave_register_theme(&k_magicsquare_theme);
}
