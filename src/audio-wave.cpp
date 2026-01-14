#include "audio-wave.hpp"
#include "audiowave-themes.hpp"
#include "config.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include <util/platform.h>

#define BLOG(log_level, format, ...) \
	blog(log_level, "[audio-wave] " format, ##__VA_ARGS__)

static const char *kSourceId = "audio_wave_source";
static const char *kSourceName = "Audio Wave";
static const char *SETTING_AUDIO_SOURCE = "audio_source";
static const char *SETTING_WIDTH = "width";
static const char *SETTING_HEIGHT = "height";
static const char *SETTING_INSET = "inset_ratio";
static const char *SETTING_COLOR = "color";
static const char *SETTING_GRADIENT_ENABLED = "gradient_enabled";
static const char *SETTING_GRADIENT_COLOR1 = "gradient_color1";
static const char *SETTING_GRADIENT_COLOR2 = "gradient_color2";
static const char *SETTING_GRADIENT_COLOR3 = "gradient_color3";
static const char *SETTING_REACT_DB = "react_db";
static const char *SETTING_PEAK_DB = "peak_db";
static const char *SETTING_ATTACK_MS = "attack_ms";
static const char *SETTING_RELEASE_MS = "release_ms";
static const char *SETTING_THEME = AW_SETTING_THEME;
static const char *PROP_THEME_GROUP = "theme_group";
static struct obs_source_info audio_wave_source_info;
static std::vector<const audio_wave_theme *> g_themes;

void audio_wave_register_theme(const audio_wave_theme *theme)
{
	if (!theme || !theme->id || !theme->display_name)
		return;

	for (auto *t : g_themes) {
		if (std::strcmp(t->id, theme->id) == 0)
			return;
	}
	g_themes.push_back(theme);
}

size_t audio_wave_get_theme_count()
{
	return g_themes.size();
}

const audio_wave_theme *audio_wave_get_theme_by_index(size_t index)
{
	if (index >= g_themes.size())
		return nullptr;
	return g_themes[index];
}

const audio_wave_theme *audio_wave_get_default_theme()
{
	return g_themes.empty() ? nullptr : g_themes[0];
}

const audio_wave_theme *audio_wave_find_theme(const char *id)
{
	if (!id || !*id)
		return audio_wave_get_default_theme();

	for (auto *t : g_themes) {
		if (std::strcmp(t->id, id) == 0)
			return t;
	}
	return audio_wave_get_default_theme();
}

// ─────────────────────────────────────────────
// Helpers exposed to themes
// ─────────────────────────────────────────────

void audio_wave_set_solid_color(gs_eparam_t *param, uint32_t color)
{
	if (!param)
		return;

	vec4 c;
	const uint8_t r = color & 0xFF;
	const uint8_t g = (color >> 8) & 0xFF;
	const uint8_t b = (color >> 16) & 0xFF;

	c.x = r / 255.0f;
	c.y = g / 255.0f;
	c.z = b / 255.0f;
	c.w = 1.0f;

	gs_effect_set_vec4(param, &c);
}

static uint32_t parse_hex_color_0xRRGGBB(const char *s, bool *ok)
{
	*ok = false;
	if (!s) return 0;
	while (*s == ' ' || *s == '\t' || *s == ',') ++s;
	if (*s == '#') ++s;
	if (!*s) return 0;

	unsigned v = 0;
	int digits = 0;
	for (; *s; ++s) {
		char c = *s;
		if (c == ' ' || c == '\t' || c == ',')
			break;
		unsigned d = 0;
		if (c >= '0' && c <= '9') d = (unsigned)(c - '0');
		else if (c >= 'a' && c <= 'f') d = 10u + (unsigned)(c - 'a');
		else if (c >= 'A' && c <= 'F') d = 10u + (unsigned)(c - 'A');
		else return 0;
		v = (v << 4) | d;
		++digits;
		if (digits > 8) return 0;
	}

	if (digits == 6) {
		*ok = true;
		return (uint32_t)v;
	}
	if (digits == 8) {
		*ok = true;
		return (uint32_t)(v & 0xFFFFFFu);
	}
	return 0;
}

static inline uint32_t lerp_color(uint32_t a, uint32_t b, float t)
{
	const uint8_t ar = (uint8_t)(a & 0xFF);
	const uint8_t ag = (uint8_t)((a >> 8) & 0xFF);
	const uint8_t ab = (uint8_t)((a >> 16) & 0xFF);

	const uint8_t br = (uint8_t)(b & 0xFF);
	const uint8_t bg = (uint8_t)((b >> 8) & 0xFF);
	const uint8_t bb = (uint8_t)((b >> 16) & 0xFF);

	const float r = ar + (br - ar) * t;
	const float g = ag + (bg - ag) * t;
	const float bl = ab + (bb - ab) * t;

	return ((uint32_t)std::lround(bl) << 16) | ((uint32_t)std::lround(g) << 8) | (uint32_t)std::lround(r);
}

static void build_gradient_lut(audio_wave_source *s, uint32_t c1, uint32_t c2, uint32_t c3, bool enabled)
{
	if (!s)
		return;

	if (!enabled) {
		s->gradient_enabled = false;
		for (size_t i = 0; i < s->gradient_lut.size(); ++i)
			s->gradient_lut[i] = s->color;
		return;
	}

	if ((c1 & 0xFFFFFFu) == 0)
		c1 = s->color;
	if ((c2 & 0xFFFFFFu) == 0)
		c2 = s->color;
	if ((c3 & 0xFFFFFFu) == 0)
		c3 = s->color;

	for (int i = 0; i < 256; ++i) {
		const float t = (float)i / 255.0f;
		if (t <= 0.5f) {
			const float u = t / 0.5f;
			s->gradient_lut[(size_t)i] = lerp_color(c1, c2, u);
		} else {
			const float u = (t - 0.5f) / 0.5f;
			s->gradient_lut[(size_t)i] = lerp_color(c2, c3, u);
		}
	}

	s->gradient_enabled = true;
}

float audio_wave_apply_curve(const audio_wave_source * /*s*/, float v)
{
	if (v < 0.0f) v = 0.0f;
	if (v > 1.0f) v = 1.0f;
	return v;
}

void audio_wave_build_wave(audio_wave_source *s)
{
	if (!s)
		return;

	std::lock_guard<std::mutex> lock(s->audio_mutex);

	const size_t frames = s->num_samples;
	if (!frames || s->samples_left.empty()) {
		s->wave_raw.clear();
		s->wave.clear();
		return;
	}

	const auto &L = s->samples_left;
	const auto &R = s->samples_right;

	s->wave_raw.resize(frames);

	for (size_t i = 0; i < frames; ++i) {
		const float l = (i < L.size()) ? L[i] : 0.0f;
		const float r = (i < R.size()) ? R[i] : l;
		const float lin = 0.5f * (std::fabs(l) + std::fabs(r));

		float db = -120.0f;
		if (lin > 0.000001f)
			db = 20.0f * log10f(lin);

		const float react = s->react_db;
		const float peak = std::max(s->peak_db, react + 0.1f);
		float n = (db - react) / (peak - react);
		if (n < 0.0f) n = 0.0f;
		if (n > 1.0f) n = 1.0f;

		s->wave_raw[i] = n;
	}
}

static void audio_wave_smooth_wave(audio_wave_source *s)
{
	if (!s)
		return;
	if (s->wave_raw.empty()) {
		s->wave.clear();
		s->last_wave_ts_ns = 0;
		return;
	}

	const uint64_t now = os_gettime_ns();
	float dt = 1.0f / 60.0f;
	if (s->last_wave_ts_ns != 0 && now > s->last_wave_ts_ns) {
		dt = (float)((now - s->last_wave_ts_ns) / 1e9);
		dt = std::clamp(dt, 0.0f, 0.25f);
	}
	s->last_wave_ts_ns = now;

	if (s->wave.size() != s->wave_raw.size()) {
		s->wave = s->wave_raw;
		return;
	}

	const float attack_s = std::max(0.0f, s->attack_ms) / 1000.0f;
	const float release_s = std::max(0.0f, s->release_ms) / 1000.0f;

	for (size_t i = 0; i < s->wave.size(); ++i) {
		const float target = s->wave_raw[i];
		float v = s->wave[i];
		const bool rising = target > v;
		const float tau = rising ? attack_s : release_s;
		if (tau <= 0.000001f) {
			v = target;
		} else {
			const float a = 1.0f - std::exp(-dt / tau);
			v = v + (target - v) * a;
		}
		if (v < 0.0f)
			v = 0.0f;
		if (v > 1.0f)
			v = 1.0f;
		s->wave[i] = v;
	}
}

// ─────────────────────────────────────────────
// Audio capture wiring
// ─────────────────────────────────────────────

static void release_audio_weak(audio_wave_source *s)
{
	if (!s || !s->audio_weak)
		return;

	obs_weak_source_release(s->audio_weak);
	s->audio_weak = nullptr;
}

static void attach_to_audio_source(audio_wave_source *s);

static bool enum_audio_sources(void *data, obs_source_t *source)
{
	obs_property_t *prop = (obs_property_t *)data;

	const char *id = obs_source_get_id(source);
	if (id && std::strcmp(id, kSourceId) == 0)
		return true;

	if (!obs_source_audio_active(source))
		return true;

	const char *name = obs_source_get_name(source);
	if (!name)
		return true;

	obs_property_list_add_string(prop, name, name);
	return true;
}

static void audio_capture_cb(void *param, obs_source_t *, const struct audio_data *audio, bool muted)
{
auto *s = static_cast<audio_wave_source *>(param);
if (!s || !audio)
	return;

if (!s->alive.load(std::memory_order_acquire))
	return;

s->audio_cb_inflight.fetch_add(1, std::memory_order_acq_rel);

if (!s->alive.load(std::memory_order_acquire)) {
	s->audio_cb_inflight.fetch_sub(1, std::memory_order_acq_rel);
	return;
}

if (muted || audio->frames == 0 || !audio->data[0]) {
	s->audio_cb_inflight.fetch_sub(1, std::memory_order_acq_rel);
	return;
}

const size_t frames = audio->frames;

const float *left = reinterpret_cast<const float *>(audio->data[0]);
const float *right = audio->data[1] ? reinterpret_cast<const float *>(audio->data[1]) : nullptr;

{
	std::lock_guard<std::mutex> lock(s->audio_mutex);

	if (s->samples_left.size() != frames)
		s->samples_left.resize(frames);
	if (s->samples_right.size() != frames)
		s->samples_right.resize(frames);

	std::memcpy(s->samples_left.data(), left, frames * sizeof(float));
	if (right)
		std::memcpy(s->samples_right.data(), right, frames * sizeof(float));
	else
		std::memcpy(s->samples_right.data(), left, frames * sizeof(float));

	s->num_samples = frames;
}

s->audio_cb_inflight.fetch_sub(1, std::memory_order_acq_rel);
}

static void attach_to_audio_source(audio_wave_source *s)
{
	if (!s)
		return;

	release_audio_weak(s);

	if (s->audio_source_name.empty())
		return;

	obs_source_t *target = obs_get_source_by_name(s->audio_source_name.c_str());
	if (!target) {
		BLOG(LOG_WARNING, "Audio source '%s' not found", s->audio_source_name.c_str());
		return;
	}

	s->audio_weak = obs_source_get_weak_source(target);
	obs_source_add_audio_capture_callback(target, audio_capture_cb, s);

	obs_source_release(target);

	BLOG(LOG_INFO, "Attached to audio source '%s'", s->audio_source_name.c_str());
}

static void detach_from_audio_source(audio_wave_source *s)
{
	if (!s || !s->audio_weak)
		return;

	obs_source_t *target = obs_weak_source_get_source(s->audio_weak);
	if (target) {
		obs_source_remove_audio_capture_callback(target, audio_capture_cb, s);
		obs_source_release(target);
	}

	release_audio_weak(s);
}

// ─────────────────────────────────────────────
// Properties / UI
// ─────────────────────────────────────────────

#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(x) (void)(x)
#endif

static void clear_properties(obs_properties_t *props)
{
	if (!props)
		return;

	obs_property_t *p = obs_properties_first(props);
	while (p) {
		obs_property_t *next = p;
		if (!obs_property_next(&next)) {
			next = nullptr;
		}

		const char *name = obs_property_name(p);
		if (name) {
			obs_properties_remove_by_name(props, name);
		}

		p = next;
	}
}

static bool on_theme_modified(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(property);

	audio_wave_register_builtin_themes();

	obs_property_t *group_prop = obs_properties_get(props, PROP_THEME_GROUP);
	if (!group_prop)
		return true;

	obs_properties_t *group = obs_property_group_content(group_prop);
	if (!group)
		return true;

	clear_properties(group);

	const char *theme_id = settings ? obs_data_get_string(settings, SETTING_THEME) : nullptr;
	const audio_wave_theme *theme = audio_wave_find_theme(theme_id);

	if (theme && theme->add_properties)
		theme->add_properties(group);

	return true;
}

static bool on_gradient_modified(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(property);
	UNUSED_PARAMETER(settings);

	const bool use_grad = obs_data_get_bool(settings, SETTING_GRADIENT_ENABLED);

	obs_property_t *p_color = obs_properties_get(props, SETTING_COLOR);
	obs_property_t *p_g1 = obs_properties_get(props, SETTING_GRADIENT_COLOR1);
	obs_property_t *p_g2 = obs_properties_get(props, SETTING_GRADIENT_COLOR2);
	obs_property_t *p_g3 = obs_properties_get(props, SETTING_GRADIENT_COLOR3);

	if (p_color)
		obs_property_set_visible(p_color, !use_grad);
	if (p_g1)
		obs_property_set_visible(p_g1, use_grad);
	if (p_g2)
		obs_property_set_visible(p_g2, use_grad);
	if (p_g3)
		obs_property_set_visible(p_g3, use_grad);

	return true;
}

static obs_properties_t *audio_wave_get_properties(void *data)
{
	UNUSED_PARAMETER(data);

	audio_wave_register_builtin_themes();

	obs_properties_t *props = obs_properties_create();

	obs_property_t *p_list = obs_properties_add_list(props, SETTING_AUDIO_SOURCE, "Audio Source",
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_enum_sources(enum_audio_sources, p_list);

	obs_properties_add_int(props, SETTING_WIDTH, "Width", 64, 4096, 1);
	obs_properties_add_int(props, SETTING_HEIGHT, "Height", 32, 2048, 1);

	obs_properties_add_float_slider(props, SETTING_INSET, "Inset (relative to canvas)", 0.0, 0.4, 0.01);

	obs_property_t *p_use_grad = obs_properties_add_bool(props, SETTING_GRADIENT_ENABLED, "Use Gradient");
	obs_properties_add_color(props, SETTING_COLOR, "Color");
	obs_properties_add_color(props, SETTING_GRADIENT_COLOR1, "Gradient Color 1");
	obs_properties_add_color(props, SETTING_GRADIENT_COLOR2, "Gradient Color 2");
	obs_properties_add_color(props, SETTING_GRADIENT_COLOR3, "Gradient Color 3");
	{
		obs_property_t *pc = obs_properties_get(props, SETTING_COLOR);
		obs_property_t *pg1 = obs_properties_get(props, SETTING_GRADIENT_COLOR1);
		obs_property_t *pg2 = obs_properties_get(props, SETTING_GRADIENT_COLOR2);
		obs_property_t *pg3 = obs_properties_get(props, SETTING_GRADIENT_COLOR3);
		if (pc)
			obs_property_set_visible(pc, true);
		if (pg1)
			obs_property_set_visible(pg1, false);
		if (pg2)
			obs_property_set_visible(pg2, false);
		if (pg3)
			obs_property_set_visible(pg3, false);
	}
	if (p_use_grad)
		obs_property_set_modified_callback(p_use_grad, on_gradient_modified);

	obs_properties_add_float_slider(props, SETTING_REACT_DB, "React at (dB)", -80.0, -1.0, 1.0);
	obs_properties_add_float_slider(props, SETTING_PEAK_DB, "Peak at (dB)", -60.0, 0.0, 1.0);

	obs_properties_add_int_slider(props, SETTING_ATTACK_MS, "Smoothing Attack (ms)", 0, 500, 1);
	obs_properties_add_int_slider(props, SETTING_RELEASE_MS, "Smoothing Release (ms)", 0, 1500, 1);

	obs_property_t *theme_prop =
		obs_properties_add_list(props, SETTING_THEME, "Theme", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	const size_t theme_count = audio_wave_get_theme_count();
	for (size_t i = 0; i < theme_count; ++i) {
		const audio_wave_theme *t = audio_wave_get_theme_by_index(i);
		if (!t)
			continue;
		obs_property_list_add_string(theme_prop, t->display_name, t->id);
	}

	obs_properties_t *theme_group_content = obs_properties_create();
	obs_property_t *theme_group = obs_properties_add_group(props, PROP_THEME_GROUP, "Theme Options",
							       OBS_GROUP_NORMAL, theme_group_content);
	UNUSED_PARAMETER(theme_group);

	on_theme_modified(props, theme_prop, nullptr);

	obs_property_set_modified_callback(theme_prop, on_theme_modified);

	return props;
}

static void audio_wave_get_defaults(obs_data_t *settings)
{
	audio_wave_register_builtin_themes();

	obs_data_set_default_string(settings, SETTING_AUDIO_SOURCE, "");
	obs_data_set_default_int(settings, SETTING_WIDTH, 800);
	obs_data_set_default_int(settings, SETTING_HEIGHT, 200);

	obs_data_set_default_double(settings, SETTING_INSET, 0.08);

	obs_data_set_default_int(settings, SETTING_COLOR, 0xFFFFFF);
	obs_data_set_default_bool(settings, SETTING_GRADIENT_ENABLED, false);
	obs_data_set_default_int(settings, SETTING_GRADIENT_COLOR1, 0x00D2FF);
	obs_data_set_default_int(settings, SETTING_GRADIENT_COLOR2, 0x9D50BB);
	obs_data_set_default_int(settings, SETTING_GRADIENT_COLOR3, 0xFF3CAC);
	obs_data_set_default_double(settings, SETTING_REACT_DB, -50.0);
	obs_data_set_default_double(settings, SETTING_PEAK_DB, -6.0);
	obs_data_set_default_int(settings, SETTING_ATTACK_MS, 35);
	obs_data_set_default_int(settings, SETTING_RELEASE_MS, 180);

	const audio_wave_theme *def = audio_wave_get_default_theme();
	if (def) {
		obs_data_set_default_string(settings, SETTING_THEME, def->id);
	}
}

// ─────────────────────────────────────────────
// Create / update / destroy
// ─────────────────────────────────────────────

static void audio_wave_update(void *data, obs_data_t *settings)
{
	auto *s = static_cast<audio_wave_source *>(data);
	if (!s)
		return;

	audio_wave_register_builtin_themes();

	detach_from_audio_source(s);

	s->audio_source_name = obs_data_get_string(settings, SETTING_AUDIO_SOURCE);

	{
		std::lock_guard<std::mutex> g(s->render_mutex);

		s->width = (int)aw_get_int_default(settings, SETTING_WIDTH, 800);
		s->height = (int)aw_get_int_default(settings, SETTING_HEIGHT, 200);

		double inset = aw_get_float_default(settings, SETTING_INSET, 0.08f);
		inset = std::clamp(inset, 0.0, 0.4);
		s->inset_ratio = (float)inset;

		double react = aw_get_float_default(settings, SETTING_REACT_DB, -50.0f);
		double peak = aw_get_float_default(settings, SETTING_PEAK_DB, -6.0f);
		react = std::clamp(react, -80.0, -1.0);
		peak = std::clamp(peak, -60.0, 0.0);
		if (peak <= react)
			peak = react + 0.1;

		s->react_db = (float)react;
		s->peak_db = (float)peak;

		int attack_ms = aw_get_int_default(settings, SETTING_ATTACK_MS, 35);
		int release_ms = aw_get_int_default(settings, SETTING_RELEASE_MS, 180);
		attack_ms = std::clamp(attack_ms, 0, 500);
		release_ms = std::clamp(release_ms, 0, 1500);
		s->attack_ms = (float)attack_ms;
		s->release_ms = (float)release_ms;

		if (s->width < 1)
			s->width = 1;
		if (s->height < 1)
			s->height = 1;

		uint32_t color = (uint32_t)aw_get_int_default(settings, SETTING_COLOR, 0xFFFFFF);
		if ((color & 0xFFFFFFu) == 0)
			color = 0xFFFFFF;
		s->color = (color & 0xFFFFFFu);

		const bool use_grad = obs_data_get_bool(settings, SETTING_GRADIENT_ENABLED);
		uint32_t g1 = (uint32_t)aw_get_int_default(settings, SETTING_GRADIENT_COLOR1, 0);
		uint32_t g2 = (uint32_t)aw_get_int_default(settings, SETTING_GRADIENT_COLOR2, 0);
		uint32_t g3 = (uint32_t)aw_get_int_default(settings, SETTING_GRADIENT_COLOR3, 0);
		if (use_grad) {
			build_gradient_lut(s, g1, g2, g3, true);
		} else {
			s->gradient_enabled = false;
			for (size_t i = 0; i < s->gradient_lut.size(); ++i)
				s->gradient_lut[i] = s->color;
		}

		const char *theme_id = obs_data_get_string(settings, SETTING_THEME);
		const audio_wave_theme *new_theme = audio_wave_find_theme(theme_id);

		if (s->theme && s->theme != new_theme && s->theme->destroy_data) {
			s->theme->destroy_data(s);
			s->theme_data = nullptr;
		}

		s->theme = new_theme;
		s->theme_id = theme_id ? theme_id : "";

		if (s->theme && s->theme->update) {
			s->theme->update(s, settings);
		}
	}

	attach_to_audio_source(s);
}

static void *audio_wave_create(obs_data_t *settings, obs_source_t *source)
{
	audio_wave_register_builtin_themes();

	auto *s = new audio_wave_source{};
	s->self = source;
	s->color = 0xFFFFFF;
	s->mirror = false;
	s->gradient_enabled = false;
	for (size_t i = 0; i < s->gradient_lut.size(); ++i)
		s->gradient_lut[i] = s->color;

	s->inset_ratio = 0.08f;
	s->react_db = -50.0f;
	s->peak_db = -6.0f;

	audio_wave_update(s, settings);

	BLOG(LOG_INFO, "Created Audio Wave source");

	return s;
}

static void audio_wave_destroy(void *data)
{
auto *s = static_cast<audio_wave_source *>(data);
if (!s)
	return;

s->alive.store(false, std::memory_order_release);

detach_from_audio_source(s);

for (int i = 0; i < 2000; ++i) {
	if (s->audio_cb_inflight.load(std::memory_order_acquire) == 0)
		break;
	os_sleep_ms(1);
}

if (s->theme && s->theme->destroy_data) {
	std::lock_guard<std::mutex> g(s->render_mutex);
	s->theme->destroy_data(s);
	s->theme_data = nullptr;
}

{
	std::lock_guard<std::mutex> lock(s->audio_mutex);
	s->samples_left.clear();
	s->samples_right.clear();
	s->wave.clear();
	s->num_samples = 0;
}

delete s;
}

static void audio_wave_show(void *data)
{
	auto *s = static_cast<audio_wave_source *>(data);
	if (!s)
		return;

	detach_from_audio_source(s);
	attach_to_audio_source(s);
}

static void audio_wave_hide(void *data)
{
	auto *s = static_cast<audio_wave_source *>(data);
	if (!s)
		return;

	detach_from_audio_source(s);
}

static uint32_t audio_wave_get_width(void *data)
{
	auto *s = static_cast<audio_wave_source *>(data);
	return s ? (uint32_t)s->width : 0;
}

static uint32_t audio_wave_get_height(void *data)
{
	auto *s = static_cast<audio_wave_source *>(data);
	return s ? (uint32_t)s->height : 0;
}

// ─────────────────────────────────────────────
// Render
// ─────────────────────────────────────────────

static void audio_wave_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);

	auto *s = static_cast<audio_wave_source *>(data);
	if (!s)
		return;

	std::lock_guard<std::mutex> g(s->render_mutex);

	if (!s->theme || !s->theme->draw)
		return;

	audio_wave_build_wave(s);
	audio_wave_smooth_wave(s);

	const float w = (float)s->width;
	const float h = (float)s->height;
	const float min_dim = std::min(w, h);

	const float inset_px = std::max(0.0f, s->inset_ratio) * min_dim;
	const float inner_w = std::max(1.0f, w - 2.0f * inset_px);
	const float inner_h = std::max(1.0f, h - 2.0f * inset_px);
	const float sx = inner_w / w;
	const float sy = inner_h / h;

	if (s->theme->draw_background) {
		gs_matrix_push();
		if (inset_px > 0.0f) {
			gs_matrix_translate3f(inset_px, inset_px, 0.0f);
			gs_matrix_scale3f(sx, sy, 1.0f);
		}
		s->theme->draw_background(s);
		gs_matrix_pop();
	}

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	if (!solid)
		return;

	gs_eparam_t *color_param = gs_effect_get_param_by_name(solid, "color");
	gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");
	if (!tech)
		return;

	size_t passes = gs_technique_begin(tech);
	for (size_t i = 0; i < passes; ++i) {
		gs_technique_begin_pass(tech, i);

		gs_matrix_push();
		if (inset_px > 0.0f) {
			gs_matrix_translate3f(inset_px, inset_px, 0.0f);
			gs_matrix_scale3f(sx, sy, 1.0f);
		}

		s->theme->draw(s, color_param);

		gs_matrix_pop();

		gs_technique_end_pass(tech);
	}
	gs_technique_end(tech);
}

static const char *audio_wave_get_name(void *)
{
	return kSourceName;
}

// ─────────────────────────────────────────────
// OBS registration
// ─────────────────────────────────────────────

extern "C" void register_audio_wave_source(void)
{
	std::memset(&audio_wave_source_info, 0, sizeof(audio_wave_source_info));

	audio_wave_source_info.id = kSourceId;
	audio_wave_source_info.type = OBS_SOURCE_TYPE_INPUT;
	audio_wave_source_info.output_flags = OBS_SOURCE_VIDEO;

	audio_wave_source_info.get_name = audio_wave_get_name;
	audio_wave_source_info.icon_type = OBS_ICON_TYPE_PROCESS_AUDIO_OUTPUT;
	audio_wave_source_info.create = audio_wave_create;
	audio_wave_source_info.destroy = audio_wave_destroy;
	audio_wave_source_info.update = audio_wave_update;
	audio_wave_source_info.get_defaults = audio_wave_get_defaults;
	audio_wave_source_info.get_properties = audio_wave_get_properties;
	audio_wave_source_info.show = audio_wave_show;
	audio_wave_source_info.hide = audio_wave_hide;
	audio_wave_source_info.get_width = audio_wave_get_width;
	audio_wave_source_info.get_height = audio_wave_get_height;
	audio_wave_source_info.video_render = audio_wave_video_render;

	obs_register_source(&audio_wave_source_info);

	BLOG(LOG_INFO, "Registered Audio Wave source as '%s'", kSourceId);
}
