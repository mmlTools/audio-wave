#include "theme-cosmic.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstring>

#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(x) (void)(x)
#endif

static const char *k_theme_id_cosmic = "cosmic";
static const char *k_theme_name_cosmic = "Cosmic Galaxy";

static const char *COSMIC_PROP_COLOR_CORE = "cos_color_core";
static const char *COSMIC_PROP_COLOR_SPIRAL = "cos_color_spiral";
static const char *COSMIC_PROP_COLOR_HALO = "cos_color_halo";
static const char *COSMIC_PROP_COLOR_STAR = "cos_color_star";

static const char *COSMIC_PROP_SEGMENTS = "cos_segments";
static const char *COSMIC_PROP_ARM_COUNT = "cos_arm_count";
static const char *COSMIC_PROP_ARM_STRENGTH = "cos_arm_strength";
static const char *COSMIC_PROP_HALO_WIDTH = "cos_halo_width";
static const char *COSMIC_PROP_THICK_SPIRAL = "cos_thickness_spiral";
static const char *COSMIC_PROP_STAR_THRESH = "cos_star_threshold";
static const char *COSMIC_PROP_STAR_LENGTH = "cos_star_length";
static const char *COSMIC_PROP_ROT_SPEED = "cos_rotation_speed";

struct cosmic_theme_data {
	std::vector<float> prev_r;
	bool initialized = false;

	uint32_t segments = 160;
	int arm_count = 3;
	float arm_strength = 0.4f;
	float halo_width = 60.0f;
	int thick_spiral = 3;
	float star_threshold = 0.3f;
	float star_length = 60.0f;
	float rot_speed = 0.5f;
	float phase = 0.0f;
};

// ─────────────────────────────────────────────
// Properties
// ─────────────────────────────────────────────

static void cosmic_theme_add_properties(obs_properties_t *props)
{
	obs_properties_add_color(props, COSMIC_PROP_COLOR_CORE, "Core Color");
	obs_properties_add_color(props, COSMIC_PROP_COLOR_SPIRAL, "Spiral Color");
	obs_properties_add_color(props, COSMIC_PROP_COLOR_HALO, "Halo Color");
	obs_properties_add_color(props, COSMIC_PROP_COLOR_STAR, "Stars Color");
	obs_properties_add_int_slider(props, COSMIC_PROP_SEGMENTS, "Shape Resolution", 48, 512, 8);
	obs_properties_add_int_slider(props, COSMIC_PROP_ARM_COUNT, "Spiral Arms", 1, 6, 1);
	obs_properties_add_float_slider(props, COSMIC_PROP_ARM_STRENGTH, "Arm Strength", 0.0, 1.0, 0.05);
	obs_properties_add_int_slider(props, COSMIC_PROP_HALO_WIDTH, "Halo Width (px)", 10, 300, 5);
	obs_properties_add_int_slider(props, COSMIC_PROP_THICK_SPIRAL, "Spiral Thickness", 1, 8, 1);
	obs_properties_add_float_slider(props, COSMIC_PROP_STAR_THRESH, "Stars Threshold (0..1)", 0.0, 1.0, 0.01);
	obs_properties_add_int_slider(props, COSMIC_PROP_STAR_LENGTH, "Stars Length (px)", 5, 200, 5);
	obs_properties_add_float_slider(props, COSMIC_PROP_ROT_SPEED, "Rotation Speed", 0.0, 5.0, 0.1);
}

static void cosmic_theme_update(audio_wave_source *s, obs_data_t *settings)
{
	if (!s || !settings)
		return;

	s->theme_style_id = "default";

	uint32_t col_core = (uint32_t)aw_get_int_default(settings, COSMIC_PROP_COLOR_CORE, 0);
	uint32_t col_spiral = (uint32_t)aw_get_int_default(settings, COSMIC_PROP_COLOR_SPIRAL, 0);
	uint32_t col_halo = (uint32_t)aw_get_int_default(settings, COSMIC_PROP_COLOR_HALO, 0);
	uint32_t col_star = (uint32_t)aw_get_int_default(settings, COSMIC_PROP_COLOR_STAR, 0);

	if (col_core == 0)
		col_core = 0xFFFFFF;
	if (col_spiral == 0)
		col_spiral = 0x66CCFF;
	if (col_halo == 0)
		col_halo = 0x101037;
	if (col_star == 0)
		col_star = 0xFFFF99;

	s->color = col_spiral;

	s->colors.clear();
	s->colors.push_back(audio_wave_named_color{"core", col_core});
	s->colors.push_back(audio_wave_named_color{"spiral", col_spiral});
	s->colors.push_back(audio_wave_named_color{"halo", col_halo});
	s->colors.push_back(audio_wave_named_color{"stars", col_star});

	int seg = aw_get_int_default(settings, COSMIC_PROP_SEGMENTS, 160);
	seg = std::clamp(seg, 48, 512);

	int arm_cnt = aw_get_int_default(settings, COSMIC_PROP_ARM_COUNT, 3);
	arm_cnt = std::clamp(arm_cnt, 1, 6);

	double arm_str = aw_get_float_default(settings, COSMIC_PROP_ARM_STRENGTH, 0.4f);
	arm_str = std::clamp(arm_str, 0.0, 1.0);

	int halo_w = aw_get_int_default(settings, COSMIC_PROP_HALO_WIDTH, 60);
	halo_w = std::clamp(halo_w, 10, 300);

	int thick = aw_get_int_default(settings, COSMIC_PROP_THICK_SPIRAL, 3);
	thick = std::clamp(thick, 1, 8);

	double thr = aw_get_float_default(settings, COSMIC_PROP_STAR_THRESH, 0.3f);
	thr = std::clamp(thr, 0.0, 1.0);

	int star_len = aw_get_int_default(settings, COSMIC_PROP_STAR_LENGTH, 60);
	star_len = std::clamp(star_len, 5, 200);

	double rot = aw_get_float_default(settings, COSMIC_PROP_ROT_SPEED, 0.5f);
	rot = std::clamp(rot, 0.0, 5.0);

	auto *d = static_cast<cosmic_theme_data *>(s->theme_data);
	if (!d) {
		d = new cosmic_theme_data{};
		s->theme_data = d;
	}

	d->segments = (uint32_t)seg;
	d->arm_count = arm_cnt;
	d->arm_strength = (float)arm_str;
	d->halo_width = (float)halo_w;
	d->thick_spiral = thick;
	d->star_threshold = (float)thr;
	d->star_length = (float)star_len;
	d->rot_speed = (float)rot;

	d->initialized = false;
}

// ─────────────────────────────────────────────
// Drawing
// ─────────────────────────────────────────────

static void cosmic_theme_draw(audio_wave_source *s, gs_eparam_t *color_param)
{
	if (!s || s->width <= 0 || s->height <= 0)
		return;

	const size_t frames = s->wave.size();
	if (frames < 2)
		return;

	auto *d = static_cast<cosmic_theme_data *>(s->theme_data);
	if (!d) {
		d = new cosmic_theme_data{};
		s->theme_data = d;
	}

	const float w = (float)s->width;
	const float h = (float)s->height;
	const float cx = w * 0.5f;
	const float cy = h * 0.5f;

	const float base_radius = std::min(w, h) * 0.25f;
	const float audio_radius = std::min(w, h) * 0.20f;
	const uint32_t segments = std::max(d->segments, 48u);

	const uint32_t col_core = audio_wave_get_color(s, 0, s->color);
	const uint32_t col_spiral = audio_wave_get_color(s, 1, col_core);
	const uint32_t col_halo = audio_wave_get_color(s, 2, col_spiral);
	const uint32_t col_star = audio_wave_get_color(s, 3, col_halo);

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
		d->prev_r.assign(segments, base_radius);
		d->initialized = false;
	}

	std::vector<float> radius(segments);
	const float alpha_time = 0.30f;

	d->phase += d->rot_speed * (float)M_PI / 180.0f;
	if (d->phase > 2.0f * (float)M_PI)
		d->phase -= 2.0f * (float)M_PI;

	for (uint32_t i = 0; i < segments; ++i) {
		float a = amp_smooth[i];
		if (a < 0.0f)
			a = 0.0f;
		if (a > 1.0f)
			a = 1.0f;

		float v = audio_wave_apply_curve(s, a);

		const float angle = ((float)i / (float)segments) * 2.0f * (float)M_PI;
		const float arm_mod = std::sin(d->arm_count * angle + d->phase);

		float mod = 1.0f + d->arm_strength * arm_mod;
		if (mod < 0.2f)
			mod = 0.2f;

		float r_target = base_radius + v * audio_radius * mod;

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

	std::vector<float> halo_r(segments);
	for (uint32_t i = 0; i < segments; ++i) {
		halo_r[i] = radius[i] + d->halo_width;
	}

	std::vector<float> x_sp(segments), y_sp(segments);
	std::vector<float> x_halo(segments), y_halo(segments);

	for (uint32_t i = 0; i < segments; ++i) {
		x_sp[i] = cx + cos_t[i] * radius[i];
		y_sp[i] = cy + sin_t[i] * radius[i];
		x_halo[i] = cx + cos_t[i] * halo_r[i];
		y_halo[i] = cy + sin_t[i] * halo_r[i];
	}

	gs_matrix_push();

	if (color_param)
		audio_wave_set_solid_color(color_param, col_halo);

	gs_render_start(true);
	for (uint32_t i = 0; i < segments; ++i) {
		uint32_t next = (i + 1) % segments;

		gs_vertex2f(x_sp[i], y_sp[i]);
		gs_vertex2f(x_halo[i], y_halo[i]);
		gs_vertex2f(x_halo[next], y_halo[next]);

		gs_vertex2f(x_sp[i], y_sp[i]);
		gs_vertex2f(x_halo[next], y_halo[next]);
		gs_vertex2f(x_sp[next], y_sp[next]);
	}
	gs_render_stop(GS_TRIS);

	if (color_param)
		audio_wave_set_solid_color(color_param, col_core);

	const int core_segments = 32;
	const float core_radius = base_radius * 0.4f;

	gs_render_start(true);
	for (int i = 0; i < core_segments; ++i) {
		const float a0 = ((float)i / (float)core_segments) * 2.0f * (float)M_PI;
		const float a1 = ((float)(i + 1) / (float)core_segments) * 2.0f * (float)M_PI;

		const float x0 = cx + std::cos(a0) * core_radius;
		const float y0 = cy + std::sin(a0) * core_radius;
		const float x1 = cx + std::cos(a1) * core_radius;
		const float y1 = cy + std::sin(a1) * core_radius;

		gs_vertex2f(cx, cy);
		gs_vertex2f(x0, y0);
		gs_vertex2f(x1, y1);
	}
	gs_render_stop(GS_TRIS);

	if (color_param)
		audio_wave_set_solid_color(color_param, col_spiral);

	int thick = d->thick_spiral;
	if (thick < 1)
		thick = 1;

	{
		const float half = (float)(thick - 1) * 0.5f;
		for (int t = 0; t < thick; ++t) {
			const float offset = (float)t - half;

			gs_render_start(true);
			for (uint32_t i = 0; i <= segments; ++i) {
				uint32_t idx = (i == segments) ? 0 : i;

				float rad = radius[idx] + offset;
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
		audio_wave_set_solid_color(color_param, col_star);

	gs_render_start(true);
	for (uint32_t i = 0; i < segments; ++i) {
		float a = amp_smooth[i];
		if (a < 0.0f)
			a = 0.0f;
		if (a > 1.0f)
			a = 1.0f;

		float v = audio_wave_apply_curve(s, a);
		if (v < d->star_threshold)
			continue;

		float extra = (v - d->star_threshold) / std::max(0.001f, 1.0f - d->star_threshold);
		if (extra < 0.0f)
			extra = 0.0f;
		if (extra > 1.0f)
			extra = 1.0f;

		float length = d->star_length * extra;

		const float r_start = halo_r[i] + 2.0f;
		const float r_end = r_start + length;

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

static void cosmic_theme_destroy_data(audio_wave_source *s)
{
	if (!s || !s->theme_data)
		return;

	auto *d = static_cast<cosmic_theme_data *>(s->theme_data);
	delete d;
	s->theme_data = nullptr;
}

static const audio_wave_theme k_cosmic_theme = {
	k_theme_id_cosmic,   k_theme_name_cosmic, cosmic_theme_add_properties,
	cosmic_theme_update, cosmic_theme_draw,   cosmic_theme_destroy_data,
};

void audio_wave_register_cosmic_theme()
{
	audio_wave_register_theme(&k_cosmic_theme);
}
