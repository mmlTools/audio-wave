#include <obs-module.h>
#include "includes/config.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

extern "C" void register_audio_shader_source(void);
extern "C" void register_audio_pulse_filter(void);

bool obs_module_load(void)
{
	blog(LOG_INFO, "[%s] plugin loaded successfully (version %s)", PLUGIN_NAME, PLUGIN_VERSION);
	register_audio_shader_source();
	register_audio_pulse_filter();
	return true;
}

void obs_module_unload(void)
{
	blog(LOG_INFO, "[%s] plugin unloaded", PLUGIN_NAME);
}
