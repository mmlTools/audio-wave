#include <obs-module.h>
#include "config.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

extern "C" void register_audio_wave_source(void);

bool obs_module_load(void)
{
	blog(LOG_INFO, "[%s] plugin loaded successfully (version %s)", PLUGIN_NAME, PLUGIN_VERSION);

	register_audio_wave_source();

	return true;
}

void obs_module_unload(void)
{
	blog(LOG_INFO, "[%s] plugin unloaded", PLUGIN_NAME);
}
