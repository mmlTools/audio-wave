#include "theme-rounded-bars.hpp"

#include <algorithm>
#include <cmath>
#include <vector>
#include <cstring>
#include <obs-module.h>

#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(x) (void)(x)
#endif

static const char *k_theme_id_rounded_bars = "rounded_bars";
static const char *k_theme_name_rounded_bars = "Rounded Wobble Bars";

// ─────────────────────────────────────────────
// Property keys
// ─────────────────────────────────────────────

static const char *RB_PROP_BAR_COUNT = "rb_bar_count";
static const char *RB_PROP_WOBBLE_INT = "rb_wobble_intensity";
static const char *RB_PROP_MIRROR_VERT = "rb_mirror_vertical";

// ─────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────

static inline float rb_db_from_amp(float a)
{
	if (a <= 1e-6f)
		return -120.0f;
	return 20.0f * log10f(a);
}

static inline float rb_clamp01(float v)
{
	if (v < 0.0f)
		return 0.0f;
	if (v > 1.0f)
		return 1.0f;
	return v;
}

// Full pill (both ends rounded).
// y_bottom: bottom of bar
// height:   full height including caps
// barWidth: full width
static void rb_draw_rounded_bar(float cx, float y_bottom, float height, float barWidth, int capSegments)
{
	if (height <= 0.0f || barWidth <= 0.0f)
		return;

	const float radius = barWidth * 0.5f;
	const float minHeight = 2.0f * radius;

	if (height < minHeight)
		height = minHeight;

	const float y_top = y_bottom - height;

	const float y_top_center = y_top + radius;
	const float y_bottom_center = y_bottom - radius;

	const float left = cx - radius;
	const float right = cx + radius;

	const float y_rect_top = y_top_center;
	const float y_rect_bottom = y_bottom_center;

	if (y_rect_bottom > y_rect_top) {
		gs_render_start(true);
		gs_vertex2f(left, y_rect_bottom);
		gs_vertex2f(right, y_rect_bottom);
		gs_vertex2f(right, y_rect_top);
		gs_vertex2f(left, y_rect_bottom);
		gs_vertex2f(right, y_rect_top);
		gs_vertex2f(left, y_rect_top);
		gs_render_stop(GS_TRIS);
	}

	{
		const int segments = std::max(capSegments, 4);
		const float step = (float)M_PI / (float)segments;

		gs_render_start(true);
		for (int i = 0; i < segments; ++i) {
			const float theta0 = step * (float)i;
			const float theta1 = step * (float)(i + 1);

			const float cx0 = cx;
			const float cy0 = y_top_center;

			const float x1 = cx + cosf(theta0) * radius;
			const float y1 = y_top_center - sinf(theta0) * radius;

			const float x2 = cx + cosf(theta1) * radius;
			const float y2 = y_top_center - sinf(theta1) * radius;

			gs_vertex2f(cx0, cy0);
			gs_vertex2f(x1, y1);
			gs_vertex2f(x2, y2);
		}
		gs_render_stop(GS_TRIS);
	}

	{
		const int segments = std::max(capSegments, 4);
		const float step = (float)M_PI / (float)segments;

		gs_render_start(true);
		for (int i = 0; i < segments; ++i) {
			const float theta0 = step * (float)i;
			const float theta1 = step * (float)(i + 1);

			const float cx0 = cx;
			const float cy0 = y_bottom_center;

			const float x1 = cx + cosf(theta0) * radius;
			const float y1 = y_bottom_center + sinf(theta0) * radius;

			const float x2 = cx + cosf(theta1) * radius;
			const float y2 = y_bottom_center + sinf(theta1) * radius;

			gs_vertex2f(cx0, cy0);
			gs_vertex2f(x1, y1);
			gs_vertex2f(x2, y2);
		}
		gs_render_stop(GS_TRIS);
	}
}

// Half bar: flat at bottom (center line), rounded at top.
// centerY:  where the flat edge sits (center line).
// halfHeight: total height from center to top (including rounded cap).
static void rb_draw_rounded_bar_half_up(float cx, float centerY, float halfHeight, float barWidth, int capSegments)
{
	if (halfHeight <= 0.0f || barWidth <= 0.0f)
		return;

	const float radius = barWidth * 0.5f;
	const float minHeight = radius;
	if (halfHeight < minHeight)
		halfHeight = minHeight;

	const float y_bottom = centerY;
	const float y_top = centerY - halfHeight;

	const float left = cx - radius;
	const float right = cx + radius;

	const float y_cap_center = y_top + radius;
	const float y_rect_top = y_cap_center;
	const float y_rect_bottom = y_bottom;

	if (y_rect_bottom > y_rect_top) {
		gs_render_start(true);
		gs_vertex2f(left, y_rect_bottom);
		gs_vertex2f(right, y_rect_bottom);
		gs_vertex2f(right, y_rect_top);

		gs_vertex2f(left, y_rect_bottom);
		gs_vertex2f(right, y_rect_top);
		gs_vertex2f(left, y_rect_top);
		gs_render_stop(GS_TRIS);
	}

	{
		const int segments = std::max(capSegments, 4);
		const float step = (float)M_PI / (float)segments;

		gs_render_start(true);
		for (int i = 0; i < segments; ++i) {
			const float theta0 = step * (float)i;
			const float theta1 = step * (float)(i + 1);

			const float cx0 = cx;
			const float cy0 = y_cap_center;

			const float x1 = cx + cosf(theta0) * radius;
			const float y1 = y_cap_center - sinf(theta0) * radius;

			const float x2 = cx + cosf(theta1) * radius;
			const float y2 = y_cap_center - sinf(theta1) * radius;

			gs_vertex2f(cx0, cy0);
			gs_vertex2f(x1, y1);
			gs_vertex2f(x2, y2);
		}
		gs_render_stop(GS_TRIS);
	}
}

// Half bar: flat at top (center line), rounded at bottom.
// centerY:    where the flat edge sits (center line).
// halfHeight: total height from center to bottom (including rounded cap).
static void rb_draw_rounded_bar_half_down(float cx, float centerY, float halfHeight, float barWidth, int capSegments)
{
	if (halfHeight <= 0.0f || barWidth <= 0.0f)
		return;

	const float radius = barWidth * 0.5f;
	const float minHeight = radius;
	if (halfHeight < minHeight)
		halfHeight = minHeight;

	const float y_top = centerY;
	const float y_bottom = centerY + halfHeight;

	const float left = cx - radius;
	const float right = cx + radius;

	const float y_cap_center = y_bottom - radius;
	const float y_rect_top = y_top;
	const float y_rect_bottom = y_cap_center;

	if (y_rect_bottom > y_rect_top) {
		gs_render_start(true);
		gs_vertex2f(left, y_rect_bottom);
		gs_vertex2f(right, y_rect_bottom);
		gs_vertex2f(right, y_rect_top);

		gs_vertex2f(left, y_rect_bottom);
		gs_vertex2f(right, y_rect_top);
		gs_vertex2f(left, y_rect_top);
		gs_render_stop(GS_TRIS);
	}

	{
		const int segments = std::max(capSegments, 4);
		const float step = (float)M_PI / (float)segments;

		gs_render_start(true);
		for (int i = 0; i < segments; ++i) {
			const float theta0 = step * (float)i;
			const float theta1 = step * (float)(i + 1);

			const float cx0 = cx;
			const float cy0 = y_cap_center;

			const float x1 = cx + cosf(theta0) * radius;
			const float y1 = y_cap_center + sinf(theta0) * radius;

			const float x2 = cx + cosf(theta1) * radius;
			const float y2 = y_cap_center + sinf(theta1) * radius;

			gs_vertex2f(cx0, cy0);
			gs_vertex2f(x1, y1);
			gs_vertex2f(x2, y2);
		}
		gs_render_stop(GS_TRIS);
	}
}

// ─────────────────────────────────────────────
// Theme data
// ─────────────────────────────────────────────

struct rounded_bars_theme_data {
	std::vector<float> value;
	std::vector<float> velocity;
	bool initialized = false;

	uint32_t bars = 32;

	float wobble_stiffness = 0.20f;
	float wobble_damping = 0.80f;

	bool mirror_vertical = false;
};

// ─────────────────────────────────────────────
// Properties
// ─────────────────────────────────────────────

static void rounded_bars_theme_add_properties(obs_properties_t *props)
{
	obs_properties_add_int_slider(props, RB_PROP_BAR_COUNT, "Bars", 8, 128, 1);

	obs_properties_add_int_slider(props, RB_PROP_WOBBLE_INT, "Wobble Intensity", 0, 100, 1);

	obs_properties_add_bool(props, RB_PROP_MIRROR_VERT, "Mirror Vertically");
}

static void rounded_bars_theme_update(audio_wave_source *s, obs_data_t *settings)
{
	if (!s || !settings)
		return;

	s->theme_style_id = "default";

	auto *d = static_cast<rounded_bars_theme_data *>(s->theme_data);
	if (!d) {
		d = new rounded_bars_theme_data{};
		s->theme_data = d;
	}

	const int bars_i = (int)obs_data_get_int(settings, RB_PROP_BAR_COUNT);
	const int wobble_i = (int)obs_data_get_int(settings, RB_PROP_WOBBLE_INT);
	const bool mirror_v = obs_data_get_bool(settings, RB_PROP_MIRROR_VERT);

	const int bars_clamped = std::max(8, std::min(128, bars_i));
	d->bars = (uint32_t)bars_clamped;
	d->mirror_vertical = mirror_v;

	const float t = std::clamp((float)wobble_i / 100.0f, 0.0f, 1.0f);
	d->wobble_stiffness = 0.35f + (0.08f - 0.35f) * t;
	d->wobble_damping = 0.55f + (0.92f - 0.55f) * t;

	d->initialized = false;
}

// ─────────────────────────────────────────────
// Drawing (centered, true vertical mirror, no background)
// ─────────────────────────────────────────────

static void rounded_bars_theme_draw(audio_wave_source *s, gs_eparam_t *color_param)
{
	if (!s || s->width <= 0 || s->height <= 0)
		return;

	const size_t frames = s->wave.size();
	if (frames < 2)
		return;

	auto *d = static_cast<rounded_bars_theme_data *>(s->theme_data);
	if (!d) {
		d = new rounded_bars_theme_data{};
		s->theme_data = d;
	}

	const float w = (float)s->width;
	const float h = (float)s->height;

	const uint32_t bars = std::max(d->bars, 8u);

		if (d->value.size() != bars) {
		d->value.assign(bars, 0.0f);
		d->velocity.assign(bars, 0.0f);
		d->initialized = false;
	}

	const float marginX = w * 0.05f;
	const float usableW = w - marginX * 2.0f;
	const float gapRatio = 0.20f;

	const float slotW = usableW / (float)bars;
	float barWidth = slotW * (1.0f - gapRatio);
	if (barWidth < 1.0f)
		barWidth = 1.0f;

	const float gap = slotW - barWidth;
	const float totalW = (float)bars * (barWidth + gap) - gap;
	const float startX = (w - totalW) * 0.5f;

	const float centerY = h * 0.5f;
	const float maxHalfHeight = h * 0.4f;
	const float baseFraction = 0.20f;
	const float baseHalfHeight = maxHalfHeight * baseFraction;
	const float maxExtraHalf = maxHalfHeight - baseHalfHeight;

	gs_matrix_push();

	std::vector<float> targetExtraHalf(bars);

	for (uint32_t i = 0; i < bars; ++i) {
		float u = (float)i / (float)(bars - 1);
		size_t idx = (size_t)(u * (float)(frames - 1));

		float a = (idx < frames) ? s->wave[idx] : 0.0f;
		if (a < 1e-6f)
			a = 0.0f;

		float db = rb_db_from_amp(a);
		float extraNorm = 0.0f;
		const float react_db = s->react_db;
		const float peak_db = s->peak_db;

		if (db > react_db) {
			float t = (db - react_db) / (peak_db - react_db + 1e-3f);
			t = rb_clamp01(t);
			extraNorm = t;
		}

		extraNorm = audio_wave_apply_curve(s, extraNorm);

		targetExtraHalf[i] = extraNorm * maxExtraHalf;
	}

	for (uint32_t i = 0; i < bars; ++i) {
		float value = d->value[i];
		float vel = d->velocity[i];
		float target = targetExtraHalf[i];

		if (!d->initialized) {
			value = target;
			vel = 0.0f;
		}

		float acc = (target - value) * d->wobble_stiffness;
		vel = vel * d->wobble_damping + acc;
		value += vel;

		if (value < 0.0f)
			value = 0.0f;
		if (value > maxExtraHalf)
			value = maxExtraHalf;

		d->value[i] = value;
		d->velocity[i] = vel;
	}

	d->initialized = true;

	const int capSegments = 12;

	for (uint32_t i = 0; i < bars; ++i) {
		if (color_param) {
			const float tcol = (bars <= 1) ? 0.0f : ((float)i / (float)(bars - 1));
			audio_wave_set_solid_color(color_param, aw_gradient_color_at(s, tcol));
		}
		const float centerX = startX + (float)i * (barWidth + gap) + barWidth * 0.5f;
		const float extraHalf = d->value[i];
		const float halfHeightSide = baseHalfHeight + extraHalf;

		if (halfHeightSide <= 0.5f)
			continue;

		if (!d->mirror_vertical) {
			const float upperBottomY = centerY;
			const float fullHeight = halfHeightSide;
			rb_draw_rounded_bar(centerX, upperBottomY, fullHeight, barWidth, capSegments);
		} else {
			rb_draw_rounded_bar_half_up(centerX, centerY, halfHeightSide, barWidth, capSegments);
			rb_draw_rounded_bar_half_down(centerX, centerY, halfHeightSide, barWidth, capSegments);
		}
	}

	gs_matrix_pop();
}

// ─────────────────────────────────────────────
// Destroy
// ─────────────────────────────────────────────

static void rounded_bars_theme_destroy_data(audio_wave_source *s)
{
	if (!s || !s->theme_data)
		return;

	auto *d = static_cast<rounded_bars_theme_data *>(s->theme_data);
	delete d;
	s->theme_data = nullptr;
}

// ─────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────

static const audio_wave_theme k_rounded_bars_theme = {
	k_theme_id_rounded_bars,   k_theme_name_rounded_bars, rounded_bars_theme_add_properties,
	rounded_bars_theme_update, rounded_bars_theme_draw,   rounded_bars_theme_destroy_data,
};

void audio_wave_register_rounded_bars_theme()
{
	audio_wave_register_theme(&k_rounded_bars_theme);
}
