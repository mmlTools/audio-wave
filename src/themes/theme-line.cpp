#include "theme-line.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstring>

#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(x) (void)(x)
#endif

static const char *k_theme_id_line = "line";
static const char *k_theme_name_line = "Line";
static const char *LINE_PROP_STYLE = "line_style";
static const char *LINE_PROP_MIRROR = "line_mirror";
static const char *LINE_PROP_CURVE_COUNT = "line_curve_count";
static const char *LINE_PROP_OUTLINE_THICK = "line_outline_thickness";

struct line_theme_data {
	std::vector<float> prev_y;
	bool initialized = false;
	int curve_count = 3;
	int outline_thickness = 2;
};

// ─────────────────────────────────────────────
// Style property modified callback
// ─────────────────────────────────────────────
static bool line_style_modified(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(property);

	const char *style_id = obs_data_get_string(settings, LINE_PROP_STYLE);
	const bool is_filled = (style_id && std::strcmp(style_id, "filled") == 0);
	obs_property_t *curve_cnt = obs_properties_get(props, LINE_PROP_CURVE_COUNT);
	obs_property_t *outline_th = obs_properties_get(props, LINE_PROP_OUTLINE_THICK);
	if (curve_cnt)
		obs_property_set_visible(curve_cnt, is_filled);
	if (outline_th)
		obs_property_set_visible(outline_th, is_filled);

	return true;
}

static void line_theme_add_properties(obs_properties_t *props)
{
	obs_property_t *style =
		obs_properties_add_list(props, LINE_PROP_STYLE, "Style", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	obs_property_list_add_string(style, "Linear", "linear");
	obs_property_list_add_string(style, "Bars", "bars");
	obs_property_list_add_string(style, "Filled Area", "filled");

	obs_property_set_modified_callback(style, line_style_modified);

	obs_property_t *mirror = obs_properties_add_bool(props, LINE_PROP_MIRROR, "Mirror vertically");

	obs_property_t *curve_cnt =
		obs_properties_add_int_slider(props, LINE_PROP_CURVE_COUNT, "Curve Count", 1, 16, 1);

	obs_property_t *outline_th =
		obs_properties_add_int_slider(props, LINE_PROP_OUTLINE_THICK, "Outline Thickness", 1, 8, 1);
	obs_property_set_visible(curve_cnt, false);
	obs_property_set_visible(outline_th, false);

	UNUSED_PARAMETER(mirror);
}

static void line_theme_update(audio_wave_source *s, obs_data_t *settings)
{
	if (!s || !settings)
		return;

	const char *style_id = obs_data_get_string(settings, LINE_PROP_STYLE);
	if (!style_id || !*style_id)
		style_id = "linear";

	s->theme_style_id = style_id;

	s->mirror = obs_data_get_bool(settings, LINE_PROP_MIRROR);

	int curve_count = aw_get_int_default(settings, LINE_PROP_CURVE_COUNT, 3);
	curve_count = std::clamp(curve_count, 1, 16);

	int thick = aw_get_int_default(settings, LINE_PROP_OUTLINE_THICK, 2);
	thick = std::clamp(thick, 1, 8);

	auto *d = static_cast<line_theme_data *>(s->theme_data);
	if (!d) {
		d = new line_theme_data{};
		s->theme_data = d;
	}

	d->curve_count = curve_count;
	d->outline_thickness = thick;
	d->initialized = false;
}

static inline size_t sample_index_for_x(const line_theme_data *d, uint32_t x, uint32_t width_u, size_t frames)
{
	if (width_u <= 1 || frames <= 1)
		return 0;

	const float u = (float)x / (float)(width_u - 1);
	const int curve_count = d ? std::max(d->curve_count, 1) : 1;
	const float pos = u * (float)curve_count;
	const float frac = pos - std::floor(pos);
	const float f_idx = frac * (float)(frames - 1);

	size_t idx = (size_t)f_idx;
	if (idx >= frames)
		idx = frames - 1;

	return idx;
}

// ─────────────────────────────────────────────
// Draw helpers
// ─────────────────────────────────────────────

static void draw_line_linear(audio_wave_source *s, gs_eparam_t *color_param, bool smooth)
{
	const size_t frames = s->wave.size();
	if (!frames || s->width <= 0 || s->height <= 0)
		return;

	const float w = (float)s->width;
	const float h = (float)s->height;
	const float mid_y = h * 0.5f;
	const float top_margin = 2.0f;

	const uint32_t width_u = (uint32_t)w;
	if (width_u == 0)
		return;

	std::vector<float> amp(width_u);
	for (uint32_t x = 0; x < width_u; ++x) {
		const size_t idx = (size_t)((double)x * (double)(frames - 1) / (double)std::max(1.0f, w - 1.0f));
		amp[x] = (idx < frames) ? s->wave[idx] : 0.0f;
	}

	std::vector<float> amp_smooth;
	if (smooth && !amp.empty()) {
		amp_smooth.resize(amp.size());
		float prev = amp[0];
		amp_smooth[0] = prev;
		const float alpha = 0.15f;
		for (size_t i = 1; i < amp.size(); ++i) {
			prev = prev + alpha * (amp[i] - prev);
			amp_smooth[i] = prev;
		}
	}

	auto get_amp = [&](uint32_t x) -> float {
		return smooth ? amp_smooth[x] : amp[x];
	};

	if (color_param)
		audio_wave_set_solid_color(color_param, aw_gradient_color_at(s, 0.5f));
gs_render_start(true);
	for (uint32_t x = 0; x < width_u; ++x) {
		const float v_raw = get_amp(x);
		const float v = audio_wave_apply_curve(s, v_raw);
		const float y = mid_y - v * (mid_y - top_margin);
		gs_vertex2f((float)x, y);
	}
	gs_render_stop(GS_LINESTRIP);

	if (!s->mirror)
		return;

	gs_render_start(true);
	for (uint32_t x = 0; x < width_u; ++x) {
		const float v_raw = get_amp(x);
		const float v = audio_wave_apply_curve(s, v_raw);
		const float y = mid_y - v * (mid_y - top_margin);
		const float y_m = mid_y + (mid_y - y);
		gs_vertex2f((float)x, y_m);
	}
	gs_render_stop(GS_LINESTRIP);
}

static void draw_line_bars(audio_wave_source *s, gs_eparam_t *color_param)
{
	const size_t frames = s->wave.size();
	if (!frames || s->width <= 0 || s->height <= 0)
		return;

	const float w = (float)s->width;
	const float h = (float)s->height;
	const float mid_y = h * 0.5f;

	const uint32_t width_u = (uint32_t)w;
	if (width_u == 0)
		return;

	std::vector<float> amp(width_u);
	for (uint32_t x = 0; x < width_u; ++x) {
		const size_t idx = (size_t)((double)x * (double)(frames - 1) / (double)std::max(1.0f, w - 1.0f));
		amp[x] = (idx < frames) ? s->wave[idx] : 0.0f;
	}

	if (color_param)
		audio_wave_set_solid_color(color_param, aw_gradient_color_at(s, 0.5f));
const uint32_t step = 3;

	gs_render_start(true);
	for (uint32_t x = 0; x < width_u; x += step) {
		const float v_raw = amp[x];
		const float v = audio_wave_apply_curve(s, v_raw);
		const float y = mid_y - v * (mid_y - 4.0f);

		gs_vertex2f((float)x, mid_y);
		gs_vertex2f((float)x, y);

		if (s->mirror) {
			const float y_m = mid_y + (mid_y - y);
			gs_vertex2f((float)x, mid_y);
			gs_vertex2f((float)x, y_m);
		}
	}
	gs_render_stop(GS_LINES);
}

static void draw_line_filled(audio_wave_source *s, gs_eparam_t *color_param)
{
	const size_t frames = s->wave.size();
	if (!frames || s->width <= 0 || s->height <= 0)
		return;

	const float w = (float)s->width;
	const float h = (float)s->height;
	const float baseline = h;
	const float top_margin = 2.0f;
	const float mid_y = h * 0.5f;

	const uint32_t width_u = (uint32_t)w;
	if (width_u == 0)
		return;

	auto *d = static_cast<line_theme_data *>(s->theme_data);
	if (!d) {
		d = new line_theme_data{};
		s->theme_data = d;
	}

	std::vector<float> amp(width_u);
	for (uint32_t x = 0; x < width_u; ++x) {
		const size_t idx = sample_index_for_x(d, x, width_u, frames);
		amp[x] = (idx < frames) ? s->wave[idx] : 0.0f;
	}

	std::vector<float> amp_smooth(width_u);
	float prev = amp[0];
	amp_smooth[0] = prev;
	const float alpha_space = 0.20f;
	for (size_t i = 1; i < amp.size(); ++i) {
		prev = prev + alpha_space * (amp[i] - prev);
		amp_smooth[i] = prev;
	}

	if (d->prev_y.size() != width_u) {
		d->prev_y.assign(width_u, baseline);
		d->initialized = false;
	}

	std::vector<float> ys(width_u);

	const float alpha_time = 0.30f;

	for (uint32_t x = 0; x < width_u; ++x) {
		const float a_raw = amp_smooth[x];

		float v = audio_wave_apply_curve(s, a_raw);
		if (v < 0.0f)
			v = 0.0f;
		if (v > 1.0f)
			v = 1.0f;

		const float y_current = top_margin + (1.0f - v) * (h - top_margin);

		float y_prev = d->initialized ? d->prev_y[x] : y_current;
		float y_smooth = y_prev + alpha_time * (y_current - y_prev);

		d->prev_y[x] = y_smooth;
		ys[x] = y_smooth;
	}

	d->initialized = true;

	if (color_param)
		audio_wave_set_solid_color(color_param, aw_gradient_color_at(s, 0.5f));

	gs_render_start(true);
	for (uint32_t x = 0; x + 1 < width_u; ++x) {
		const float y1 = ys[x];
		const float y2 = ys[x + 1];

		gs_vertex2f((float)x, baseline);
		gs_vertex2f((float)x, y1);
		gs_vertex2f((float)(x + 1), baseline);

		gs_vertex2f((float)(x + 1), baseline);
		gs_vertex2f((float)x, y1);
		gs_vertex2f((float)(x + 1), y2);

		if (s->mirror) {
			float y1m = 2.0f * mid_y - y1;
			float y2m = 2.0f * mid_y - y2;

			y1m = std::clamp(y1m, 0.0f, h);
			y2m = std::clamp(y2m, 0.0f, h);

			gs_vertex2f((float)x, 0.0f);
			gs_vertex2f((float)x, y1m);
			gs_vertex2f((float)(x + 1), 0.0f);

			gs_vertex2f((float)(x + 1), 0.0f);
			gs_vertex2f((float)x, y1m);
			gs_vertex2f((float)(x + 1), y2m);
		}
	}
	gs_render_stop(GS_TRIS);

	if (color_param)
		audio_wave_set_solid_color(color_param, aw_gradient_color_at(s, 0.5f));

	int thick = d->outline_thickness;
	if (thick < 1)
		thick = 1;

	const float half = (float)(thick - 1) * 0.5f;

	for (int t = 0; t < thick; ++t) {
		const float offset = (float)t - half;

		gs_render_start(true);
		for (uint32_t x = 0; x < width_u; ++x) {
			float y = ys[x] + offset;
			y = std::clamp(y, 0.0f, h);
			gs_vertex2f((float)x, y);
		}
		gs_render_stop(GS_LINESTRIP);

		if (s->mirror) {
			gs_render_start(true);
			for (uint32_t x = 0; x < width_u; ++x) {
				float y_m = 2.0f * mid_y - ys[x] + offset;
				y_m = std::clamp(y_m, 0.0f, h);
				gs_vertex2f((float)x, y_m);
			}
			gs_render_stop(GS_LINESTRIP);
		}
	}
}

static void line_theme_draw(audio_wave_source *s, gs_eparam_t *color_param)
{
	if (!s || s->width <= 0 || s->height <= 0)
		return;

	const float w = (float)s->width;
	const float h = (float)s->height;
	const float mid_y = h * 0.5f;

	const size_t frames = s->wave.size();

	gs_matrix_push();

	if (frames < 2) {
		if (color_param)
		audio_wave_set_solid_color(color_param, aw_gradient_color_at(s, 0.5f));
gs_render_start(true);
		for (uint32_t x = 0; x < (uint32_t)w; ++x)
			gs_vertex2f((float)x, mid_y);
		gs_render_stop(GS_LINESTRIP);

		gs_matrix_pop();
		return;
	}

	if (s->theme_style_id == "bars") {
		draw_line_bars(s, color_param);
	} else if (s->theme_style_id == "filled") {
		draw_line_filled(s, color_param);
	} else {
		draw_line_linear(s, color_param, true);
	}

	gs_matrix_pop();
}

static void line_theme_destroy_data(audio_wave_source *s)
{
	if (!s || !s->theme_data)
		return;

	auto *d = static_cast<line_theme_data *>(s->theme_data);
	delete d;
	s->theme_data = nullptr;
}

static const audio_wave_theme k_line_theme = {
	k_theme_id_line,   k_theme_name_line, line_theme_add_properties,
	line_theme_update, line_theme_draw,   line_theme_destroy_data,
};

void audio_wave_register_line_theme()
{
	audio_wave_register_theme(&k_line_theme);
}
