#include "theme-hex.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(x) (void)(x)
#endif

static const char *k_theme_id_hex = "hexagon";
static const char *k_theme_name_hex = "Hexagon";
static const char *HEX_PROP_STYLE = "hex_style";
static const char *HEX_PROP_MIRROR = "hex_mirror";
static const char *P_DENSITY = "shape_density";

static bool hex_style_modified(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const char *style_id = obs_data_get_string(settings, HEX_PROP_STYLE);
	const bool is_rays = (style_id && strcmp(style_id, "rays") == 0);

	obs_property_t *mirror = obs_properties_get(props, HEX_PROP_MIRROR);
	if (mirror)
		obs_property_set_visible(mirror, is_rays);

	return true;
}

static void hex_theme_add_properties(obs_properties_t *props)
{
	obs_property_t *style =
		obs_properties_add_list(props, HEX_PROP_STYLE, "Style", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	obs_property_list_add_string(style, "Orbit", "orbit");
	obs_property_list_add_string(style, "Rays", "rays");
	obs_property_set_modified_callback(style, hex_style_modified);

	obs_property_t *mirror = obs_properties_add_bool(props, HEX_PROP_MIRROR, "Double-sided rays");
	obs_property_set_visible(mirror, false);
	obs_properties_add_int_slider(props, P_DENSITY, "Shape Density (%)", 10, 300, 5);
}

static void hex_theme_update(audio_wave_source *s, obs_data_t *settings)
{
	if (!s || !settings)
		return;

	const char *style_id = obs_data_get_string(settings, HEX_PROP_STYLE);
	if (!style_id || !*style_id)
		style_id = "orbit";

	s->theme_style_id = style_id;

	int density = aw_get_int_default(settings, P_DENSITY, 120);
	density = std::clamp(density, 10, 300);
	s->frame_density = density;

	s->mirror = (strcmp(style_id, "rays") == 0) ? obs_data_get_bool(settings, HEX_PROP_MIRROR) : false;
}

// ─────────────────────────────────────────────
// Geometry helpers for hexagon outline
// ─────────────────────────────────────────────
struct hex_vertex {
	float x;
	float y;
};

static void build_hex_vertices(const audio_wave_source *s, std::vector<hex_vertex> &verts)
{
	verts.clear();

	const float w = (float)s->width;
	const float h = (float)s->height;
	const float cx = w * 0.5f;
	const float cy = h * 0.5f;

	const float R = std::max(1.0f, std::min(w, h) * 0.5f - 1.0f);

	const int sides = 6;
	for (int i = 0; i < sides; ++i) {
		const float angle = (2.0f * (float)M_PI * (float)i) / (float)sides;
		const float x = cx + R * std::cos(angle);
		const float y = cy + R * std::sin(angle);
		verts.push_back({x, y});
	}

	if (verts.size() < 3) {
		verts.clear();
		verts.push_back({0.0f, 0.0f});
		verts.push_back({w - 1.0f, 0.0f});
		verts.push_back({w - 1.0f, h - 1.0f});
	}
}

static void sample_hex_outline(const audio_wave_source *s, const std::vector<hex_vertex> &verts, float u, float &x,
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

	const hex_vertex &v0 = verts[i];
	const hex_vertex &v1 = verts[(i + 1) % N];

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
// Hex drawing
// ─────────────────────────────────────────────

static void draw_hex_orbit(audio_wave_source *s, gs_eparam_t *color_param)
{
	const size_t frames = s->wave.size();
	if (!frames || s->width <= 0 || s->height <= 0)
		return;

	const float max_len = std::min((float)s->width, (float)s->height) * 0.20f;

	std::vector<hex_vertex> verts;
	build_hex_vertices(s, verts);
	if (verts.empty())
		return;

	float density_raw = (float)s->frame_density;
	if (!std::isfinite(density_raw))
		density_raw = 100.0f;

	uint32_t segments = (uint32_t)std::clamp(density_raw * 4.0f, 24.0f, 2048.0f);
	if (segments == 0)
		return;

	std::vector<float> base_x(segments), base_y(segments);
	std::vector<float> amp(segments), amp_smooth;

	for (uint32_t i = 0; i < segments; ++i) {
		const float u = (float)i / (float)segments;

		float x, y, nx_unused, ny_unused;
		sample_hex_outline(s, verts, u, x, y, nx_unused, ny_unused);
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

	std::vector<float> px(segments), py(segments);
	const float cx = (float)s->width * 0.5f;
	const float cy = (float)s->height * 0.5f;
	for (uint32_t i = 0; i < segments; ++i) {
		const float v_raw = get_amp(i);
		const float v = audio_wave_apply_curve(s, v_raw);
		const float len = v * max_len;
		float x = base_x[i];
		float y = base_y[i];
		float dx = x - cx;
		float dy = y - cy;
		float l = std::sqrt(dx * dx + dy * dy);
		if (l > 1e-4f) {
			dx /= l;
			dy /= l;
		} else {
			dx = 0.0f;
			dy = -1.0f;
		}
		px[i] = x + dx * len;
		py[i] = y + dy * len;
	}

	if (color_param && s->gradient_enabled) {
		const uint32_t bins = 64;
		for (uint32_t b = 0; b < bins; ++b) {
			const uint32_t i0 = (uint32_t)((uint64_t)b * segments / bins);
			const uint32_t i1 = (uint32_t)((uint64_t)(b + 1) * segments / bins);
			if (i1 <= i0)
				continue;
			const float tcol = (bins <= 1) ? 0.0f : ((float)b / (float)(bins - 1));
			audio_wave_set_solid_color(color_param, aw_gradient_color_at(s, tcol));
			gs_render_start(true);
			for (uint32_t i = i0; i < i1; ++i) {
				const uint32_t j = (i + 1) % segments;
				gs_vertex2f(px[i], py[i]);
				gs_vertex2f(px[j], py[j]);
			}
			gs_render_stop(GS_LINES);
		}
	} else {
		const uint32_t hex_color = audio_wave_get_color(s, 0, s->color);
		if (color_param)
			audio_wave_set_solid_color(color_param, hex_color);
		gs_render_start(true);
		for (uint32_t i = 0; i < segments; ++i) {
			const uint32_t j = (i + 1) % segments;
			gs_vertex2f(px[i], py[i]);
			gs_vertex2f(px[j], py[j]);
		}
		gs_render_stop(GS_LINES);
	}
}

static void draw_hex_rays(audio_wave_source *s, gs_eparam_t *color_param)
{
	const size_t frames = s->wave.size();
	if (!frames || s->width <= 0 || s->height <= 0)
		return;

	const float max_len = std::min((float)s->width, (float)s->height) * 0.25f;

	std::vector<hex_vertex> verts;
	build_hex_vertices(s, verts);
	if (verts.empty())
		return;

	float density_raw = (float)s->frame_density;
	if (!std::isfinite(density_raw))
		density_raw = 100.0f;

	uint32_t segments = (uint32_t)std::clamp(density_raw * 4.0f, 24.0f, 2048.0f);
	if (segments == 0)
		return;

	std::vector<float> base_x(segments), base_y(segments);
	std::vector<float> nx(segments), ny(segments);
	std::vector<float> amp(segments);

	for (uint32_t i = 0; i < segments; ++i) {
		const float u = (float)i / (float)segments;

		float x, y, nxx, nyy;
		sample_hex_outline(s, verts, u, x, y, nxx, nyy);
		base_x[i] = x;
		base_y[i] = y;
		nx[i] = nxx;
		ny[i] = nyy;

		const size_t idx = (size_t)(u * (float)(frames - 1));
		amp[i] = (idx < frames) ? s->wave[idx] : 0.0f;
	}

	if (color_param && s->gradient_enabled) {
		const uint32_t bins = 64;
		for (uint32_t b = 0; b < bins; ++b) {
			const uint32_t i0 = (uint32_t)((uint64_t)b * segments / bins);
			const uint32_t i1 = (uint32_t)((uint64_t)(b + 1) * segments / bins);
			if (i1 <= i0)
				continue;
			const float tcol = (bins <= 1) ? 0.0f : ((float)b / (float)(bins - 1));
			audio_wave_set_solid_color(color_param, aw_gradient_color_at(s, tcol));
			gs_render_start(true);
			for (uint32_t i = i0; i < i1; ++i) {
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
	} else {
		const uint32_t hex_color = audio_wave_get_color(s, 0, s->color);
		if (color_param)
			audio_wave_set_solid_color(color_param, hex_color);

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
}

static void hex_theme_draw(audio_wave_source *s, gs_eparam_t *color_param)
{
	if (!s || s->width <= 0 || s->height <= 0)
		return;

	const float w = (float)s->width;
	const float h = (float)s->height;
	const float mid_y = h * 0.5f;

	const size_t frames = s->wave.size();

	gs_matrix_push();

	if (frames < 2) {
		const uint32_t hex_color = audio_wave_get_color(s, 0, s->color);
		if (color_param)
			audio_wave_set_solid_color(color_param, hex_color);

		gs_render_start(true);
		for (uint32_t x = 0; x < (uint32_t)w; ++x)
			gs_vertex2f((float)x, mid_y);
		gs_render_stop(GS_LINESTRIP);

		gs_matrix_pop();
		return;
	}

	if (s->theme_style_id == std::string("rays")) {
		draw_hex_rays(s, color_param);
	} else {
		draw_hex_orbit(s, color_param);
	}

	gs_matrix_pop();
}

static void hex_theme_destroy_data(audio_wave_source *s)
{
	UNUSED_PARAMETER(s);
}

static const audio_wave_theme k_hex_theme = {
	k_theme_id_hex,   k_theme_name_hex, hex_theme_add_properties,
	hex_theme_update, hex_theme_draw,   hex_theme_destroy_data,
};

void audio_wave_register_hex_theme()
{
	audio_wave_register_theme(&k_hex_theme);
}
