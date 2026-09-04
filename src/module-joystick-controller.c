#include <obs-module.h>
#include <util/config-file.h>
#include <obs-frontend-api.h>
#include "plugin-macros.generated.h"

#define JOYSTICK_PLUGIN_NAME "obs-joystick-controller"
#define CONFIG_SECTION_NAME "joystick-controller"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(JOYSTICK_PLUGIN_NAME, "en-US")

void gamepad_dock_init(void);
void gamepad_dock_release(void);

bool obs_module_load(void)
{
	blog(LOG_INFO, "[obs-joystick-controller] Carregando plugin Controle Joystick & Mesa de Corte (versão %s)...", PLUGIN_VERSION);
	gamepad_dock_init();
	return true;
}

void obs_module_unload(void)
{
	blog(LOG_INFO, "[obs-joystick-controller] Descarregando plugin Controle Joystick & Mesa de Corte.");
	gamepad_dock_release();
}
