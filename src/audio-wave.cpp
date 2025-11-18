#include "audio-wave.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#define BLOG(log_level, format, ...) \
	blog(log_level, "[audio-wave] " format, ##__VA_ARGS__)

static const char *kSourceId   = "audio_wave_source";
static const char *kSourceName = "Audio Wave (Simple)";

static const char *SETTING_AUDIO_SOURCE    = "audio_source";
static const char *SETTING_COLOR           = "wave_color";
static const char *SETTING_COLOR2          = "wave_color2";
static const char *SETTING_GRADIENT_ENABLE = "use_gradient";
static const char *SETTING_WIDTH           = "width";
static const char *SETTING_HEIGHT          = "height";
static const char *SETTING_MODE            = "draw_mode";
static const char *SETTING_AMPLITUDE       = "amplitude";
static const char *SETTING_MIRROR          = "mirror_wave";
static const char *SETTING_FRAME_RADIUS    = "frame_radius";
static const char *SETTING_FRAME_DENSITY   = "frame_density";

static struct obs_source_info audio_wave_source_info;

static void *audio_wave_create(obs_data_t *settings, obs_source_t *source);
static void  audio_wave_destroy(void *data);
static void  audio_wave_update(void *data, obs_data_t *settings);
static void  audio_wave_get_defaults(obs_data_t *settings);
static obs_properties_t *audio_wave_get_properties(void *data);
static void  audio_wave_show(void *data);
static void  audio_wave_hide(void *data);
static uint32_t audio_wave_get_width(void *data);
static uint32_t audio_wave_get_height(void *data);
static void  audio_wave_video_render(void *data, gs_effect_t *effect);

static bool gradient_modified(obs_properties_t *props,
                              obs_property_t *p,
                              obs_data_t *settings);
static bool mode_modified(obs_properties_t *props,
                          obs_property_t *p,
                          obs_data_t *settings);

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

static void audio_capture_cb(void *param, obs_source_t *, const struct audio_data *audio,
                             bool muted)
{
	auto *s = static_cast<audio_wave_source *>(param);
	if (!s || !audio)
		return;

	static uint64_t dbg_calls = 0;

	if (muted || audio->frames == 0 || !audio->data[0]) {
		if (dbg_calls < 10) {
			BLOG(LOG_INFO, "audio_capture_cb: muted=%d frames=%d (no data)",
			     (int)muted, (int)audio->frames);
			dbg_calls++;
		}
		return;
	}

	const size_t   frames = audio->frames;
	const uint8_t *data0  = audio->data[0];
	const uint8_t *data1  = audio->data[1];

	const float *left  = reinterpret_cast<const float *>(data0);
	const float *right = data1 ? reinterpret_cast<const float *>(data1) : nullptr;

	std::lock_guard<std::mutex> lock(s->audio_mutex);

	s->samples_left.resize(frames);
	s->samples_right.resize(frames);

	for (size_t i = 0; i < frames; ++i) {
		const float l = left[i];
		const float r = right ? right[i] : l;
		s->samples_left[i]  = l;
		s->samples_right[i] = r;
	}

	s->num_samples = frames;

	if (dbg_calls < 20) {
		BLOG(LOG_INFO, "audio_capture_cb: frames=%d firstL=%f firstR=%f",
		     (int)frames,
		     frames > 0 ? left[0] : 0.0f,
		     (frames > 0 && right) ? right[0] : left[0]);
		dbg_calls++;
	}
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
		BLOG(LOG_WARNING, "Audio source '%s' not found",
		     s->audio_source_name.c_str());
		return;
	}

	s->audio_weak = obs_source_get_weak_source(target);
	obs_source_add_audio_capture_callback(target, audio_capture_cb, s);

	obs_source_release(target);

	BLOG(LOG_INFO, "Attached to audio source '%s'",
	     s->audio_source_name.c_str());
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

static bool gradient_modified(obs_properties_t *props,
                              obs_property_t *p,
                              obs_data_t *settings)
{
	UNUSED_PARAMETER(p);

	bool enabled = obs_data_get_bool(settings, SETTING_GRADIENT_ENABLE);
	obs_property_t *c2 = obs_properties_get(props, SETTING_COLOR2);
	if (c2)
		obs_property_set_visible(c2, enabled);

	return true;
}

static bool mode_modified(obs_properties_t *props,
                          obs_property_t *p,
                          obs_data_t *settings)
{
	UNUSED_PARAMETER(p);

	const int mode = (int)obs_data_get_int(settings, SETTING_MODE);

	obs_property_t *mirror = obs_properties_get(props, SETTING_MIRROR);
	if (mirror)
		obs_property_set_visible(mirror, mode != 2);

	obs_property_t *radius  = obs_properties_get(props, SETTING_FRAME_RADIUS);
	obs_property_t *density = obs_properties_get(props, SETTING_FRAME_DENSITY);
	if (radius)
		obs_property_set_visible(radius, mode == 2);
	if (density)
		obs_property_set_visible(density, mode == 2);

	return true;
}

static obs_properties_t *audio_wave_get_properties(void *data)
{
	UNUSED_PARAMETER(data);

	obs_properties_t *props = obs_properties_create();

	obs_property_t *p_list = obs_properties_add_list(
		props, SETTING_AUDIO_SOURCE, "Audio Source",
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_enum_sources(enum_audio_sources, p_list);

	obs_property_t *mode =
		obs_properties_add_list(props, SETTING_MODE, "Display Mode",
		                        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(mode, "Wave", 0);
	obs_property_list_add_int(mode, "Bars", 1);
	obs_property_list_add_int(mode, "Rectangular Wave", 2);
	obs_property_set_modified_callback(mode, mode_modified);

	obs_property_t *grad =
		obs_properties_add_bool(props, SETTING_GRADIENT_ENABLE, "Use Gradient");
	obs_property_set_modified_callback(grad, gradient_modified);

	obs_properties_add_color(props, SETTING_COLOR,  "Wave Color");
	obs_property_t *c2 =
		obs_properties_add_color(props, SETTING_COLOR2, "Wave Color 2");
	obs_property_set_visible(c2, false);

	obs_properties_add_int_slider(props, SETTING_FRAME_RADIUS,
	                              "Corner Radius (%)", 0, 100, 1);
	obs_properties_add_int_slider(props, SETTING_FRAME_DENSITY,
	                              "Bar Density (%)", 10, 300, 5);

	obs_properties_add_int(props, SETTING_WIDTH,  "Width",  64, 4096, 1);
	obs_properties_add_int(props, SETTING_HEIGHT, "Height", 32, 2048, 1);

	obs_properties_add_int_slider(props, SETTING_AMPLITUDE,
	                              "Amplitude (%)", 10, 400, 10);

	obs_properties_add_bool(props, SETTING_MIRROR,
	                        "Mirror wave horizontally");

	return props;
}

static void audio_wave_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, SETTING_AUDIO_SOURCE, "");
	obs_data_set_default_int(settings, SETTING_WIDTH, 800);
	obs_data_set_default_int(settings, SETTING_HEIGHT, 200);
	obs_data_set_default_int(settings, SETTING_COLOR, 0xFFFFFF);
	obs_data_set_default_int(settings, SETTING_COLOR2, 0x00FF00);
	obs_data_set_default_bool(settings, SETTING_GRADIENT_ENABLE, false);
	obs_data_set_default_int(settings, SETTING_MODE, 0);
	obs_data_set_default_int(settings, SETTING_AMPLITUDE, 200);
	obs_data_set_default_bool(settings, SETTING_MIRROR, false);
	obs_data_set_default_int(settings, SETTING_FRAME_RADIUS, 0);
	obs_data_set_default_int(settings, SETTING_FRAME_DENSITY, 100);
}

static void audio_wave_update(void *data, obs_data_t *settings)
{
	auto *s = static_cast<audio_wave_source *>(data);
	if (!s)
		return;

	detach_from_audio_source(s);

	s->audio_source_name = obs_data_get_string(settings, SETTING_AUDIO_SOURCE);

	s->width   = (int)obs_data_get_int(settings, SETTING_WIDTH);
	s->height  = (int)obs_data_get_int(settings, SETTING_HEIGHT);
	s->color   = (uint32_t)obs_data_get_int(settings, SETTING_COLOR);
	s->color2  = (uint32_t)obs_data_get_int(settings, SETTING_COLOR2);
	s->mode    = (int)obs_data_get_int(settings, SETTING_MODE);
	s->use_gradient = obs_data_get_bool(settings, SETTING_GRADIENT_ENABLE);
	s->mirror  = obs_data_get_bool(settings, SETTING_MIRROR);

	// Mode 2 extras
	s->frame_radius  = (int)obs_data_get_int(settings, SETTING_FRAME_RADIUS);
	s->frame_density = (int)obs_data_get_int(settings, SETTING_FRAME_DENSITY);

	int amp_pct = (int)obs_data_get_int(settings, SETTING_AMPLITUDE);
	if (amp_pct < 10)  amp_pct = 10;
	if (amp_pct > 400) amp_pct = 400;
	s->gain = (float)amp_pct / 100.0f;

	if (s->width < 1)
		s->width = 1;
	if (s->height < 1)
		s->height = 1;

	attach_to_audio_source(s);
}

static void *audio_wave_create(obs_data_t *settings, obs_source_t *source)
{
	auto *s = new audio_wave_source{};
	s->self = source;

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

static void audio_wave_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);

	auto *s = static_cast<audio_wave_source *>(data);
	if (!s)
		return;

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	if (!solid)
		return;

	gs_eparam_t   *color_param = gs_effect_get_param_by_name(solid, "color");
	gs_technique_t *tech       = gs_effect_get_technique(solid, "Solid");
	if (!tech)
		return;

	size_t passes = gs_technique_begin(tech);
	for (size_t i = 0; i < passes; ++i) {
		gs_technique_begin_pass(tech, i);
		audio_wave_draw(s, color_param);
		gs_technique_end_pass(tech);
	}
	gs_technique_end(tech);
}

static const char *audio_wave_get_name(void *)
{
	return kSourceName;
}

extern "C" void register_audio_wave_source(void)
{
	std::memset(&audio_wave_source_info, 0, sizeof(audio_wave_source_info));

	audio_wave_source_info.id           = kSourceId;
	audio_wave_source_info.type         = OBS_SOURCE_TYPE_INPUT;
	audio_wave_source_info.output_flags = OBS_SOURCE_VIDEO;

	audio_wave_source_info.get_name       = audio_wave_get_name;
	audio_wave_source_info.create         = audio_wave_create;
	audio_wave_source_info.destroy        = audio_wave_destroy;
	audio_wave_source_info.update         = audio_wave_update;
	audio_wave_source_info.get_defaults   = audio_wave_get_defaults;
	audio_wave_source_info.get_properties = audio_wave_get_properties;
	audio_wave_source_info.show           = audio_wave_show;
	audio_wave_source_info.hide           = audio_wave_hide;
	audio_wave_source_info.get_width      = audio_wave_get_width;
	audio_wave_source_info.get_height     = audio_wave_get_height;
	audio_wave_source_info.video_render   = audio_wave_video_render;

	obs_register_source(&audio_wave_source_info);

	BLOG(LOG_INFO, "Registered Audio Wave source as '%s'", kSourceId);
}
