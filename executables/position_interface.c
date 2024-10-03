#include "position_interface.h"

extern uint16_t NB_UE_INST;
static position_t **positionArray = 0;

void config_position_coordinates(int Mod_id)
{
  AssertFatal(Mod_id < NB_UE_INST, "Mod_id must be less than NB_UE_INST. Mod_id:%d NB_UE_INST:%d", Mod_id, NB_UE_INST);
  if (positionArray == NULL) {
    positionArray = (position_t **)calloc(1, sizeof(position_t *) * NB_UE_INST);
  }
  if (!positionArray[Mod_id]) {
    char positionName[64];
    sprintf(positionName, "position%d", Mod_id);
    positionArray[Mod_id] = (void *) init_position_coordinates(positionName);
  }
}

position_t *init_position_coordinates(char *sectionName)
{
  position_t *position = (position_t *)calloc(sizeof(position_t), 1);
  paramdef_t position_params[] = POSITION_CONFIG_PARAMS_DEF;
  int ret = config_get(config_get_if(), position_params, sizeofArray(position_params), sectionName);
  AssertFatal(ret >= 0, "configuration couldn't be performed for position name: %s", sectionName);
  
  return position;
}

position_t *get_position_coordinates(int Mod_id)
{
  return positionArray[Mod_id];
}