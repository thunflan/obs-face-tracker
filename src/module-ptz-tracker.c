#include <obs-module.h>
#include <util/config-file.h>
#include <obs-frontend-api.h>
#include "plugin-macros.generated.h"

#define PTZ_PLUGIN_NAME "obs-ptz-tracker"
#define CONFIG_SECTION_NAME "ptz-tracker"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PTZ_PLUGIN_NAME, "en-US")

void register_face_tracker_ptz(bool hide_ptz);

static void ptz_tracker_check_proc(void *data, calldata_t *cd)
{
	UNUSED_PARAMETER(data);
	calldata_set_bool(cd, "installed", true);
}

bool obs_module_load(void)
{
	blog(LOG_INFO, "[obs-ptz-tracker] Carregando plugin PTZ Tracker (versão %s)", PLUGIN_VERSION);

#if LIBOBS_API_VER < MAKE_SEMANTIC_VERSION(31, 0, 0)
	config_t *cfg = obs_frontend_get_global_config();
#else
	config_t *cfg = obs_frontend_get_app_config();
#endif

	if (cfg) {
		config_set_default_bool(cfg, CONFIG_SECTION_NAME, "ShowPTZ", true);
	}

	bool show_ptz = cfg ? config_get_bool(cfg, CONFIG_SECTION_NAME, "ShowPTZ") : true;

	register_face_tracker_ptz(!show_ptz);

	// Registra procedimento para que outros plugins (como o joystick) saibam que o PTZ está instalado
	proc_handler_t *ph = obs_get_proc_handler();
	if (ph) {
		proc_handler_add(ph, "void ptz_tracker_is_installed(out bool installed)", ptz_tracker_check_proc, NULL);
		blog(LOG_INFO, "[obs-ptz-tracker] Procedimento 'ptz_tracker_is_installed' registrado com sucesso.");
	}

	return true;
}

void obs_module_unload(void)
{
	blog(LOG_INFO, "[obs-ptz-tracker] Descarregando plugin PTZ Tracker.");
}
