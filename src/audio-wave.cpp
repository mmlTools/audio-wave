#include "audio-wave.hpp"
#include "audiowave-themes.hpp"
#include "config.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#define BLOG(log_level, format, ...) \
	blog(log_level, "[audio-wave] " format, ##__VA_ARGS__)

static const char *kSourceId = "audio_wave_source";
static const char *kSourceName = "Audio Wave";

// Local setting keys
static const char *SETTING_AUDIO_SOURCE = "audio_source";
static const char *SETTING_WIDTH = "width";
static const char *SETTING_HEIGHT = "height";
static const char *SETTING_AMPLITUDE = "amplitude";
static const char *SETTING_FRAME_DENSITY = "frame_density";
static const char *SETTING_CURVE = "curve_power";
static const char *SETTING_THEME = AW_SETTING_THEME;

// Property ids
static const char *PROP_THEME_GROUP = "theme_group";

static struct obs_source_info audio_wave_source_info;

#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(x) (void)(x)
#endif

// ─────────────────────────────────────────────
// Theme registry implementation
// ─────────────────────────────────────────────
static std::vector<const audio_wave_theme *> g_themes;
static bool g_themes_registered = false;

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

float audio_wave_apply_curve(const audio_wave_source *s, float v)
{
	if (v < 0.0f)
		v = 0.0f;
	if (v > 1.0f)
		v = 1.0f;

	float p = s ? s->curve_power : 1.0f;
	if (p <= 0.0f)
		p = 1.0f;

	return powf(v, p);
}

void audio_wave_build_wave(audio_wave_source *s)
{
	if (!s)
		return;

	std::lock_guard<std::mutex> lock(s->audio_mutex);

	const size_t frames = s->num_samples;
	if (!frames || s->samples_left.empty()) {
		s->wave.clear();
		return;
	}

	const auto &L = s->samples_left;
	const auto &R = s->samples_right;

	s->wave.resize(frames);

	for (size_t i = 0; i < frames; ++i) {
		const float l = (i < L.size()) ? L[i] : 0.0f;
		const float r = (i < R.size()) ? R[i] : l;
		float m = s->gain * 0.5f * (std::fabs(l) + std::fabs(r));
		if (m > 1.0f)
			m = 1.0f;
		s->wave[i] = m;
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

	if (muted || audio->frames == 0 || !audio->data[0])
		return;

	const size_t frames = audio->frames;

	const float *left = reinterpret_cast<const float *>(audio->data[0]);
	const float *right = audio->data[1] ? reinterpret_cast<const float *>(audio->data[1]) : nullptr;

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

static void clear_properties(obs_properties_t *props)
{
	if (!props)
		return;

	obs_property_t *p = obs_properties_first(props);
	while (p) {
		// Advance pointer using OBS API
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

	// Clear previous theme-specific props
	clear_properties(group);

	// Determine selected theme
	const char *theme_id = settings ? obs_data_get_string(settings, SETTING_THEME) : nullptr;
	const audio_wave_theme *theme = audio_wave_find_theme(theme_id);

	// Let theme populate its properties (styles, colors, mirror, etc.)
	if (theme && theme->add_properties)
		theme->add_properties(group);

	return true;
}

static obs_properties_t *audio_wave_get_properties(void *data)
{
	UNUSED_PARAMETER(data);

	audio_wave_register_builtin_themes();

	obs_properties_t *props = obs_properties_create();

	// Audio source
	obs_property_t *p_list = obs_properties_add_list(props, SETTING_AUDIO_SOURCE, "Audio Source",
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_enum_sources(enum_audio_sources, p_list);

	// Core visual settings
	obs_properties_add_int(props, SETTING_WIDTH, "Width", 64, 4096, 1);
	obs_properties_add_int(props, SETTING_HEIGHT, "Height", 32, 2048, 1);

	obs_properties_add_int_slider(props, SETTING_AMPLITUDE, "Amplitude (%)", 10, 400, 10);
	obs_properties_add_int_slider(props, SETTING_CURVE, "Curve Power (%)", 20, 300, 5);
	obs_properties_add_int_slider(props, SETTING_FRAME_DENSITY, "Shape Density (%)", 10, 300, 5);

	// Theme selection
	obs_property_t *theme_prop =
		obs_properties_add_list(props, SETTING_THEME, "Theme", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	const size_t theme_count = audio_wave_get_theme_count();
	for (size_t i = 0; i < theme_count; ++i) {
		const audio_wave_theme *t = audio_wave_get_theme_by_index(i);
		if (!t)
			continue;
		obs_property_list_add_string(theme_prop, t->display_name, t->id);
	}

	// Theme-specific options group
	obs_properties_t *theme_group_content = obs_properties_create();
	obs_property_t *theme_group = obs_properties_add_group(props, PROP_THEME_GROUP, "Theme Options",
							       OBS_GROUP_NORMAL, theme_group_content);
	UNUSED_PARAMETER(theme_group);

	// Populate group with default theme's properties
	on_theme_modified(props, theme_prop, nullptr);

	// Rebuild theme properties when theme changes
	obs_property_set_modified_callback(theme_prop, on_theme_modified);

	return props;
}

static void audio_wave_get_defaults(obs_data_t *settings)
{
	audio_wave_register_builtin_themes();

	obs_data_set_default_string(settings, SETTING_AUDIO_SOURCE, "");
	obs_data_set_default_int(settings, SETTING_WIDTH, 800);
	obs_data_set_default_int(settings, SETTING_HEIGHT, 200);

	obs_data_set_default_int(settings, SETTING_AMPLITUDE, 200);
	obs_data_set_default_int(settings, SETTING_CURVE, 100);
	obs_data_set_default_int(settings, SETTING_FRAME_DENSITY, 100);

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

	// Audio source
	s->audio_source_name = obs_data_get_string(settings, SETTING_AUDIO_SOURCE);

	// Core visual settings
	s->width = (int)aw_get_int_default(settings, SETTING_WIDTH, 800);
	s->height = (int)aw_get_int_default(settings, SETTING_HEIGHT, 400);

	s->frame_density = (int)aw_get_int_default(settings, SETTING_FRAME_DENSITY, 100);
	s->frame_density = std::clamp(s->frame_density, 10, 300);

	int amp_pct = (int)aw_get_int_default(settings, SETTING_AMPLITUDE, 200);
	amp_pct = std::clamp(amp_pct, 10, 400);
	s->gain = (float)amp_pct / 100.0f;

	int curve_pct = (int)aw_get_int_default(settings, SETTING_CURVE, 100);
	curve_pct = std::clamp(curve_pct, 20, 300);
	s->curve_power = (float)curve_pct / 100.0f;

	if (s->width < 1)
		s->width = 1;
	if (s->height < 1)
		s->height = 1;

	// Theme selection
	const char *theme_id = obs_data_get_string(settings, SETTING_THEME);
	const audio_wave_theme *new_theme = audio_wave_find_theme(theme_id);

	// If theme changed, clean up old theme_data
	if (s->theme && s->theme != new_theme && s->theme->destroy_data) {
		s->theme->destroy_data(s);
		s->theme_data = nullptr;
	}

	s->theme = new_theme;
	s->theme_id = theme_id ? theme_id : "";

	// Let theme read its own properties and configure s (color, mirror, style, etc.)
	if (s->theme && s->theme->update) {
		s->theme->update(s, settings);
	}

	attach_to_audio_source(s);
}

static void *audio_wave_create(obs_data_t *settings, obs_source_t *source)
{
	audio_wave_register_builtin_themes();

	auto *s = new audio_wave_source{};
	s->self = source;

	// default color in case theme doesn't override
	s->color = 0xFFFFFF;
	s->mirror = false;

	audio_wave_update(s, settings);

	BLOG(LOG_INFO, "Created Audio Wave source");

	return s;
}

static void audio_wave_destroy(void *data)
{
	auto *s = static_cast<audio_wave_source *>(data);
	if (!s)
		return;

	detach_from_audio_source(s);

	if (s->theme && s->theme->destroy_data) {
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
	if (!s || !s->theme || !s->theme->draw)
		return;

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	if (!solid)
		return;

	gs_eparam_t *color_param = gs_effect_get_param_by_name(solid, "color");
	gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");
	if (!tech)
		return;

	// Build audio wave from latest samples
	audio_wave_build_wave(s);

	size_t passes = gs_technique_begin(tech);
	for (size_t i = 0; i < passes; ++i) {
		gs_technique_begin_pass(tech, i);

		s->theme->draw(s, color_param);

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
