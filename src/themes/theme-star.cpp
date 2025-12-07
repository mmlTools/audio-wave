#include "theme-star.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

static const char *k_theme_id_star = "star";
static const char *k_theme_name_star = "Star";
static const char *STAR_PROP_STYLE = "star_style";
static const char *STAR_PROP_COLOR = "star_color";
static const char *STAR_PROP_MIRROR = "star_mirror";

static void star_theme_add_properties(obs_properties_t *props)
{
	obs_property_t *style =
		obs_properties_add_list(props, STAR_PROP_STYLE, "Style", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	obs_property_list_add_string(style, "Linear Orbit", "linear");
	obs_property_list_add_string(style, "Rays", "rays");

	obs_properties_add_color(props, STAR_PROP_COLOR, "Color");
	obs_properties_add_bool(props, STAR_PROP_MIRROR, "Double-sided rays");
}

static void star_theme_update(audio_wave_source *s, obs_data_t *settings)
{
	if (!s || !settings)
		return;

	const char *style_id = obs_data_get_string(settings, STAR_PROP_STYLE);
	if (!style_id || !*style_id)
		style_id = "linear";

	s->theme_style_id = style_id;

	uint32_t color = (uint32_t)aw_get_int_default(settings, STAR_PROP_COLOR, 0);
	if (color == 0)
		color = 0xFFFFFF;

	s->color = color;

	s->colors.clear();
	s->colors.push_back(audio_wave_named_color{"star", color});

	if (s->frame_density < 80)
		s->frame_density = 80;

	s->mirror = obs_data_get_bool(settings, STAR_PROP_MIRROR);
}

// ─────────────────────────────────────────────
// Geometry helpers for star outline
// ─────────────────────────────────────────────
struct star_vertex {
	float x;
	float y;
};

static void build_star_vertices(const audio_wave_source *s, std::vector<star_vertex> &verts)
{
	verts.clear();

	const float w = (float)s->width;
	const float h = (float)s->height;
	const float cx = w * 0.5f;
	const float cy = h * 0.5f;

	const float R_outer = std::max(1.0f, std::min(w, h) * 0.5f - 1.0f);
	const float R_inner = R_outer * 0.5f;

	const int points = 5;
	for (int i = 0; i < points * 2; ++i) {
		const float angle = (float)i * (float)M_PI / (float)points;
		const float r = (i % 2 == 0) ? R_outer : R_inner;
		const float x = cx + r * std::cos(angle - (float)M_PI / 2.0f);
		const float y = cy + r * std::sin(angle - (float)M_PI / 2.0f);
		verts.push_back({x, y});
	}

	if (verts.size() < 3) {
		verts.clear();
		verts.push_back({0.0f, 0.0f});
		verts.push_back({w - 1.0f, 0.0f});
		verts.push_back({w - 1.0f, h - 1.0f});
	}
}

static void sample_star_outline(const audio_wave_source *s, const std::vector<star_vertex> &verts, float u, float &x,
				float &y, float &nx, float &ny)
{
	if (verts.empty()) {
		x = y = nx = ny = 0.0f;
		return;
	}

	const float w = (float)s->width;
	const float h = (float)s->height;
	const float cx = w * 0.5f;
	const float cy = h * 0.5f;

	float t = u - std::floor(u);
	if (t < 0.0f)
		t += 1.0f;

	const size_t N = verts.size();
	const float pos = t * (float)N;
	const size_t i = (size_t)std::floor(pos) % N;
	const float f = pos - std::floor(pos);

	const star_vertex &v0 = verts[i];
	const star_vertex &v1 = verts[(i + 1) % N];

	x = v0.x + (v1.x - v0.x) * f;
	y = v0.y + (v1.y - v0.y) * f;

	float dx = x - cx;
	float dy = y - cy;
	float len = std::sqrt(dx * dx + dy * dy);
	if (len > 1e-4f) {
		nx = dx / len;
		ny = dy / len;
	} else {
		nx = 0.0f;
		ny = -1.0f;
	}
}

// ─────────────────────────────────────────────
// Star drawing
// ─────────────────────────────────────────────
static void draw_star_linear(audio_wave_source *s, gs_eparam_t *color_param)
{
	const size_t frames = s->wave.size();
	if (!frames || s->width <= 0 || s->height <= 0)
		return;

	const float max_len = std::min((float)s->width, (float)s->height) * 0.2f;

	std::vector<star_vertex> verts;
	build_star_vertices(s, verts);
	if (verts.empty())
		return;

	float density_raw = (float)s->frame_density;
	if (!std::isfinite(density_raw))
		density_raw = 100.0f;

	uint32_t segments = (uint32_t)std::clamp(density_raw * 4.0f, 32.0f, 2048.0f);
	if (segments == 0)
		return;

	std::vector<float> base_x(segments), base_y(segments);
	std::vector<float> amp(segments), amp_smooth;

	for (uint32_t i = 0; i < segments; ++i) {
		const float u = (float)i / (float)segments;

		float x, y, nx_unused, ny_unused;
		sample_star_outline(s, verts, u, x, y, nx_unused, ny_unused);
		base_x[i] = x;
		base_y[i] = y;

		const size_t idx = (size_t)(u * (float)(frames - 1));
		amp[i] = (idx < frames) ? s->wave[idx] : 0.0f;
	}

	if (segments > 0) {
		amp_smooth.resize(segments);
		float prev = amp[0];
		amp_smooth[0] = prev;
		const float alpha = 0.15f;
		for (uint32_t i = 1; i < segments; ++i) {
			prev = prev + alpha * (amp[i] - prev);
			amp_smooth[i] = prev;
		}
	}

	auto get_amp = [&](uint32_t i) -> float {
		return amp_smooth[i];
	};

	const uint32_t star_color = audio_wave_get_color(s, 0, s->color);
	if (color_param)
		audio_wave_set_solid_color(color_param, star_color);

	gs_render_start(true);
	for (uint32_t i = 0; i < segments; ++i) {
		const float v_raw = get_amp(i);
		const float v = audio_wave_apply_curve(s, v_raw);
		const float len = v * max_len;
		const float x = base_x[i];
		const float y = base_y[i];
		const float cx = (float)s->width * 0.5f;
		const float cy = (float)s->height * 0.5f;
		float dx = x - cx;
		float dy = y - cy;
		const float l = std::sqrt(dx * dx + dy * dy);
		if (l > 1e-4f) {
			dx /= l;
			dy /= l;
		} else {
			dx = 0.0f;
			dy = -1.0f;
		}

		const float x2 = x + dx * len;
		const float y2 = y + dy * len;

		gs_vertex2f(x2, y2);
	}
	gs_render_stop(GS_LINESTRIP);
}

static void draw_star_rays(audio_wave_source *s, gs_eparam_t *color_param)
{
	const size_t frames = s->wave.size();
	if (!frames || s->width <= 0 || s->height <= 0)
		return;

	const float max_len = std::min((float)s->width, (float)s->height) * 0.2f;

	std::vector<star_vertex> verts;
	build_star_vertices(s, verts);
	if (verts.empty())
		return;

	float density_raw = (float)s->frame_density;
	if (!std::isfinite(density_raw))
		density_raw = 100.0f;

	uint32_t segments = (uint32_t)std::clamp(density_raw * 4.0f, 32.0f, 2048.0f);
	if (segments == 0)
		return;

	std::vector<float> base_x(segments), base_y(segments);
	std::vector<float> nx(segments), ny(segments);
	std::vector<float> amp(segments);

	for (uint32_t i = 0; i < segments; ++i) {
		const float u = (float)i / (float)segments;

		float x, y, nxx, nyy;
		sample_star_outline(s, verts, u, x, y, nxx, nyy);
		base_x[i] = x;
		base_y[i] = y;
		nx[i] = nxx;
		ny[i] = nyy;

		const size_t idx = (size_t)(u * (float)(frames - 1));
		amp[i] = (idx < frames) ? s->wave[idx] : 0.0f;
	}

	const uint32_t star_color = audio_wave_get_color(s, 0, s->color);
	if (color_param)
		audio_wave_set_solid_color(color_param, star_color);

	gs_render_start(true);
	for (uint32_t i = 0; i < segments; ++i) {
		const float v_raw = amp[i];
		const float v = audio_wave_apply_curve(s, v_raw);
		const float len = v * max_len;

		const float x1 = base_x[i];
		const float y1 = base_y[i];
		const float x2 = x1 + nx[i] * len;
		const float y2 = y1 + ny[i] * len;

		gs_vertex2f(x1, y1);
		gs_vertex2f(x2, y2);

		if (s->mirror) {
			const float x3 = x1 - nx[i] * len;
			const float y3 = y1 - ny[i] * len;
			gs_vertex2f(x1, y1);
			gs_vertex2f(x3, y3);
		}
	}
	gs_render_stop(GS_LINES);
}

static void star_theme_draw(audio_wave_source *s, gs_eparam_t *color_param)
{
	if (!s || s->width <= 0 || s->height <= 0)
		return;

	const float w = (float)s->width;
	const float h = (float)s->height;
	const float mid_y = h * 0.5f;

	const size_t frames = s->wave.size();

	gs_matrix_push();

	if (frames < 2) {
		const uint32_t star_color = audio_wave_get_color(s, 0, s->color);
		if (color_param)
			audio_wave_set_solid_color(color_param, star_color);

		gs_render_start(true);
		for (uint32_t x = 0; x < (uint32_t)w; ++x)
			gs_vertex2f((float)x, mid_y);
		gs_render_stop(GS_LINESTRIP);

		gs_matrix_pop();
		return;
	}

	if (s->theme_style_id == "rays") {
		draw_star_rays(s, color_param);
	} else {
		draw_star_linear(s, color_param);
	}

	gs_matrix_pop();
}

static void star_theme_destroy_data(audio_wave_source *s)
{
	(void)s;
}

static const audio_wave_theme k_star_theme = {
	k_theme_id_star,   k_theme_name_star, star_theme_add_properties,
	star_theme_update, star_theme_draw,   star_theme_destroy_data,
};

void audio_wave_register_star_theme()
{
	audio_wave_register_theme(&k_star_theme);
}
