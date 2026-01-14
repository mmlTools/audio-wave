#include "theme-stacked-columns.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(x) (void)(x)
#endif

static const char *k_theme_id = "stacked_columns";
static const char *k_theme_name = "Stacked Columns";

static const char *P_STYLE = "sc_style";
static const char *P_MIRROR = "sc_mirror";
static const char *P_DOUBLE = "sc_double_side";
static const char *P_COLUMNS = "sc_columns";
static const char *P_STACKS = "sc_stacks";
static const char *P_GAP = "sc_gap_ratio";

struct stacked_columns_data {
	int columns = 64;
	int stacks = 18;
	float gap_ratio = 0.18f; // fraction of block height
	bool double_side = true;
	bool mirror = false;
};

static void add_props(obs_properties_t *props)
{
	obs_property_t *style =
		obs_properties_add_list(props, P_STYLE, "Style", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(style, "Blocks", "blocks");
	obs_property_list_add_string(style, "Compact Blocks", "compact");

	obs_properties_add_bool(props, P_DOUBLE, "Double-Sided (centered)");
	obs_properties_add_bool(props, P_MIRROR, "Mirror horizontally");

	obs_properties_add_int_slider(props, P_COLUMNS, "Columns", 8, 256, 1);
	obs_properties_add_int_slider(props, P_STACKS, "Stacks per Column", 4, 48, 1);
	obs_properties_add_float_slider(props, P_GAP, "Gap", 0.0, 0.45, 0.01);
}

static void update(audio_wave_source *s, obs_data_t *settings)
{
	if (!s || !settings)
		return;

	const char *style_id = obs_data_get_string(settings, P_STYLE);
	if (!style_id || !*style_id)
		style_id = "blocks";
	s->theme_style_id = style_id;

	auto *d = static_cast<stacked_columns_data *>(s->theme_data);
	if (!d) {
		d = new stacked_columns_data{};
		s->theme_data = d;
	}

	d->double_side = obs_data_get_bool(settings, P_DOUBLE);
	d->mirror = obs_data_get_bool(settings, P_MIRROR);

	int cols = aw_get_int_default(settings, P_COLUMNS, 64);
	cols = std::clamp(cols, 8, 256);
	d->columns = cols;

	int stacks = aw_get_int_default(settings, P_STACKS, 18);
	stacks = std::clamp(stacks, 4, 48);
	d->stacks = stacks;

	float gap = aw_get_float_default(settings, P_GAP, 0.18f);
	gap = std::clamp(gap, 0.0f, 0.45f);
	d->gap_ratio = gap;
}

static void destroy(audio_wave_source *s)
{
	if (!s)
		return;
	auto *d = static_cast<stacked_columns_data *>(s->theme_data);
	delete d;
	s->theme_data = nullptr;
}

static inline float sc_db_from_amp(float a)
{
	if (a <= 1e-6f)
		return -120.0f;
	return 20.0f * log10f(a);
}

static inline float sample_wave(const audio_wave_source *s, float t)
{
	if (!s || s->wave.empty())
		return 0.0f;
	const size_t n = s->wave.size();
	const float pos = t * (float)(n - 1);
	const size_t i0 = (size_t)std::clamp((int)std::floor(pos), 0, (int)n - 1);
	const size_t i1 = std::min(i0 + 1, n - 1);
	const float a = s->wave[i0];
	const float b = s->wave[i1];
	const float f = pos - (float)i0;
	return a + (b - a) * f;
}

static void draw(audio_wave_source *s, gs_eparam_t *color_param)
{
	if (!s || !color_param)
		return;

	auto *d = static_cast<stacked_columns_data *>(s->theme_data);
	if (!d)
		return;

	const float w = (float)s->width;
	const float h = (float)s->height;

	const int cols = std::max(8, d->columns);
	const int stacks = std::max(4, d->stacks);

	const float col_w = w / (float)cols;
	const float block_w = col_w * 0.72f;
	const float x_pad = (col_w - block_w) * 0.5f;

	const float half_h = h * 0.5f;
	const float usable_h = d->double_side ? half_h : h;
	const float block_h = usable_h / (float)stacks;
	const float gap = std::clamp(d->gap_ratio, 0.0f, 0.45f) * block_h;
	const float bh = std::max(1.0f, block_h - gap);

	// Color batching: quantize gradient into 64 bins to reduce effect param churn.
	const int bins = 64;
	std::vector<std::vector<float>> bin_rects((size_t)bins); // x,y,w,h packed

	auto bin_for_t = [&](float t) -> int {
		int b = (int)std::floor(t * (float)(bins - 1) + 0.5f);
		return std::clamp(b, 0, bins - 1);
	};

	for (int c = 0; c < cols; ++c) {
		const float t = (cols <= 1) ? 0.0f : ((float)c / (float)(cols - 1));
		float amp = sample_wave(s, d->mirror ? (1.0f - t) : t);
		amp = std::clamp(amp, 0.0f, 1.0f);

		// Convert amplitude to dBFS and map to [0..1] using global React/Peak dB
		const float db = sc_db_from_amp(amp);
		float norm = 0.0f;
		if (db > s->react_db) {
			norm = (db - s->react_db) / (s->peak_db - s->react_db + 1e-3f);
			norm = std::clamp(norm, 0.0f, 1.0f);
		}
		norm = audio_wave_apply_curve(s, norm);

		// First row always visible
		int on = 1 + (int)std::lround(norm * (float)(stacks - 1));
		on = std::clamp(on, 1, stacks);

		const float x0 = (float)c * col_w + x_pad;
		const float x1 = x0 + block_w;

		const int b = bin_for_t(t);
		auto &rects = bin_rects[(size_t)b];

		if (d->double_side) {
			// build up from center line to top and bottom
			for (int k = 0; k < on; ++k) {
				const float y_top0 = half_h - (float)(k + 1) * block_h + gap * 0.5f;
				const float y_top1 = y_top0 + bh;
				rects.insert(rects.end(), {x0, y_top0, x1, y_top1});

				const float y_bot0 = half_h + (float)k * block_h + gap * 0.5f;
				const float y_bot1 = y_bot0 + bh;
				rects.insert(rects.end(), {x0, y_bot0, x1, y_bot1});
			}
		} else {
			// build up from bottom
			for (int k = 0; k < on; ++k) {
				const float y0 = h - (float)(k + 1) * block_h + gap * 0.5f;
				const float y1 = y0 + bh;
				rects.insert(rects.end(), {x0, y0, x1, y1});
			}
		}
	}

	// Emit rectangles per bin with one color each.
	for (int b = 0; b < bins; ++b) {
		auto &rects = bin_rects[(size_t)b];
		if (rects.empty())
			continue;

		const float t = (float)b / (float)(bins - 1);
		const uint32_t col = aw_gradient_color_at(s, t);
		audio_wave_set_solid_color(color_param, col);

		gs_render_start(true);
		for (size_t i = 0; i + 3 < rects.size(); i += 4) {
			const float x0 = rects[i + 0];
			const float y0 = rects[i + 1];
			const float x1 = rects[i + 2];
			const float y1 = rects[i + 3];

			// two triangles
			gs_vertex2f(x0, y0);
			gs_vertex2f(x1, y0);
			gs_vertex2f(x0, y1);

			gs_vertex2f(x1, y0);
			gs_vertex2f(x1, y1);
			gs_vertex2f(x0, y1);
		}
		gs_render_stop(GS_TRIS);
	}
}

void audio_wave_register_stacked_columns_theme()
{
	static audio_wave_theme theme = {};
	theme.id = k_theme_id;
	theme.display_name = k_theme_name;
	theme.add_properties = add_props;
	theme.update = update;
	theme.draw = draw;
	theme.destroy_data = destroy;
	theme.draw_background = nullptr;

	audio_wave_register_theme(&theme);
}
