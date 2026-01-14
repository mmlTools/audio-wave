#include "theme-circle.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(x) (void)(x)
#endif

static const char *k_theme_id_circle = "circle";
static const char *k_theme_name_circle = "Circle";

static const char *CIRCLE_PROP_STYLE = "circle_style";
static const char *CIRCLE_PROP_MIRROR = "circle_mirror";
static const char *P_DENSITY = "shape_density";

static bool circle_style_modified(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const char *style_id = obs_data_get_string(settings, CIRCLE_PROP_STYLE);
	const bool is_rays = (style_id && strcmp(style_id, "rays") == 0);

	obs_property_t *mirror = obs_properties_get(props, CIRCLE_PROP_MIRROR);
	if (mirror)
		obs_property_set_visible(mirror, is_rays);

	return true;
}

static void circle_theme_add_properties(obs_properties_t *props)
{
	obs_property_t *style = obs_properties_add_list(props, CIRCLE_PROP_STYLE, "Style", OBS_COMBO_TYPE_LIST,
							OBS_COMBO_FORMAT_STRING);

	obs_property_list_add_string(style, "Orbit", "orbit");
	obs_property_list_add_string(style, "Rays", "rays");
	obs_property_set_modified_callback(style, circle_style_modified);

	obs_property_t *mirror = obs_properties_add_bool(props, CIRCLE_PROP_MIRROR, "Double-sided rays");
	obs_property_set_visible(mirror, false);
	obs_properties_add_int_slider(props, P_DENSITY, "Shape Density (%)", 10, 300, 5);
}

static void circle_theme_update(audio_wave_source *s, obs_data_t *settings)
{
	if (!s || !settings)
		return;

	const char *style_id = obs_data_get_string(settings, CIRCLE_PROP_STYLE);
	if (!style_id || !*style_id)
		style_id = "orbit";

	s->theme_style_id = style_id;

	int density = aw_get_int_default(settings, P_DENSITY, 120);
	density = std::clamp(density, 10, 300);
	s->frame_density = density;

	s->mirror = (strcmp(style_id, "rays") == 0) ? obs_data_get_bool(settings, CIRCLE_PROP_MIRROR) : false;
}

// ─────────────────────────────────────────────
// Drawing helpers
// ─────────────────────────────────────────────

static void draw_circle_orbit(audio_wave_source *s, gs_eparam_t *color_param)
{
	const size_t frames = s->wave.size();
	if (!frames || s->width <= 0 || s->height <= 0)
		return;

	const float w = (float)s->width;
	const float h = (float)s->height;
	const float cx = w * 0.5f;
	const float cy = h * 0.5f;

	const float R_base = std::min(w, h) * 0.35f;
	const float L_max = std::min(w, h) * 0.25f;

	float density_raw = (float)s->frame_density;
	if (!std::isfinite(density_raw))
		density_raw = 100.0f;

	uint32_t segments = (uint32_t)std::clamp(density_raw * 4.0f, 32.0f, 2048.0f);
	if (segments == 0)
		return;

	std::vector<float> amp(segments), amp_smooth(segments);

	for (uint32_t i = 0; i < segments; ++i) {
		const float u = (float)i / (float)segments;
		const size_t idx = (size_t)(u * (float)(frames - 1));
		amp[i] = (idx < frames) ? s->wave[idx] : 0.0f;
	}

	if (!amp.empty()) {
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
	for (uint32_t i = 0; i < segments; ++i) {
		const float u = (float)i / (float)segments;
		const float a = u * 2.0f * (float)M_PI;
		const float v_raw = get_amp(i);
		const float v = audio_wave_apply_curve(s, v_raw);
		const float L = v * L_max;
		const float R = R_base + L;
		px[i] = cx + std::cos(a) * R;
		py[i] = cy + std::sin(a) * R;
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
		if (color_param)
			audio_wave_set_solid_color(color_param, s->color);
		gs_render_start(true);
		for (uint32_t i = 0; i < segments; ++i) {
			const uint32_t j = (i + 1) % segments;
			gs_vertex2f(px[i], py[i]);
			gs_vertex2f(px[j], py[j]);
		}
		gs_render_stop(GS_LINES);
	}
}

static void draw_circle_rays(audio_wave_source *s, gs_eparam_t *color_param)
{
	const size_t frames = s->wave.size();
	if (!frames || s->width <= 0 || s->height <= 0)
		return;

	const float w = (float)s->width;
	const float h = (float)s->height;
	const float cx = w * 0.5f;
	const float cy = h * 0.5f;

	const float R_base = std::min(w, h) * 0.30f;
	const float L_max = std::min(w, h) * 0.30f;

	float density_raw = (float)s->frame_density;
	if (!std::isfinite(density_raw))
		density_raw = 100.0f;

	uint32_t segments = (uint32_t)std::clamp(density_raw * 4.0f, 32.0f, 2048.0f);
	if (segments == 0)
		return;

	std::vector<float> amp(segments);

	for (uint32_t i = 0; i < segments; ++i) {
		const float u = (float)i / (float)segments;
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
				const float u = (float)i / (float)segments;
				const float a = u * 2.0f * (float)M_PI;

				const float v_raw = amp[i];
				const float v = audio_wave_apply_curve(s, v_raw);
				const float L = v * L_max;

				const float nx = std::cos(a);
				const float ny = std::sin(a);

				const float x1 = cx + nx * R_base;
				const float y1 = cy + ny * R_base;
				const float x2 = x1 + nx * L;
				const float y2 = y1 + ny * L;

				gs_vertex2f(x1, y1);
				gs_vertex2f(x2, y2);

				if (s->mirror) {
					gs_vertex2f(x1, y1);
					gs_vertex2f(x1 - nx * L, y1 - ny * L);
				}
			}
			gs_render_stop(GS_LINES);
		}
	} else {
		if (color_param)
			audio_wave_set_solid_color(color_param, s->color);
		gs_render_start(true);
		for (uint32_t i = 0; i < segments; ++i) {
			const float u = (float)i / (float)segments;
			const float a = u * 2.0f * (float)M_PI;

			const float v_raw = amp[i];
			const float v = audio_wave_apply_curve(s, v_raw);
			const float L = v * L_max;

			const float nx = std::cos(a);
			const float ny = std::sin(a);

			const float x1 = cx + nx * R_base;
			const float y1 = cy + ny * R_base;
			const float x2 = x1 + nx * L;
			const float y2 = y1 + ny * L;

			gs_vertex2f(x1, y1);
			gs_vertex2f(x2, y2);

			if (s->mirror) {
				const float x3 = x1 - nx * L;
				const float y3 = y1 - ny * L;
				gs_vertex2f(x1, y1);
				gs_vertex2f(x3, y3);
			}
		}
		gs_render_stop(GS_LINES);
	}
}

static void circle_theme_draw(audio_wave_source *s, gs_eparam_t *color_param)
{
	if (!s || s->width <= 0 || s->height <= 0)
		return;

	const float w = (float)s->width;
	const float h = (float)s->height;
	const float mid_y = h * 0.5f;

	const size_t frames = s->wave.size();

	gs_matrix_push();

	if (frames < 2) {
		const uint32_t circ_color = audio_wave_get_color(s, 0, s->color);
		if (color_param)
			audio_wave_set_solid_color(color_param, circ_color);

		gs_render_start(true);
		for (uint32_t x = 0; x < (uint32_t)w; ++x)
			gs_vertex2f((float)x, mid_y);
		gs_render_stop(GS_LINESTRIP);

		gs_matrix_pop();
		return;
	}

	if (s->theme_style_id == std::string("rays")) {
		draw_circle_rays(s, color_param);
	} else {
		draw_circle_orbit(s, color_param);
	}

	gs_matrix_pop();
}

static void circle_theme_destroy_data(audio_wave_source *s)
{
	UNUSED_PARAMETER(s);
}

static const audio_wave_theme k_circle_theme = {
	k_theme_id_circle,   k_theme_name_circle, circle_theme_add_properties,
	circle_theme_update, circle_theme_draw,   circle_theme_destroy_data,
};

void audio_wave_register_circle_theme()
{
	audio_wave_register_theme(&k_circle_theme);
}
