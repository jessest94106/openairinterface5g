#ifndef __POSITION_INTERFACE__H__
#define __POSITION_INTERFACE__H__

#include <executables/nr-softmodem-common.h>
#include <executables/softmodem-common.h>
#include "PHY/defs_nr_UE.h"
#include "executables/nr-uesoftmodem.h"

/* position configuration parameters name */
#define CONFIG_STRING_POSITION_X "x"
#define CONFIG_STRING_POSITION_Y "y"
#define CONFIG_STRING_POSITION_Z "z"

#define HELP_STRING_POSITION "Postion coordinates (x, y, z) config params"

// Position parameters config
/*----------------------------------------------------------------------------------------------------------------------------------------------------*/
/*                                            position configuration          parameters */
/*   optname                                         helpstr            paramflags    XXXptr              defXXXval type numelt */
/*----------------------------------------------------------------------------------------------------------------------------------------------------*/
// clang-format off
#define POSITION_CONFIG_PARAMS_DEF { \
  {CONFIG_STRING_POSITION_X,  HELP_STRING_POSITION, 0,        .dblptr=&(position->positionX),         .defuintval=0,           TYPE_DOUBLE,     0},    \
  {CONFIG_STRING_POSITION_Y,  HELP_STRING_POSITION, 0,        .dblptr=&(position->positionY),         .defuintval=0,           TYPE_DOUBLE,     0},    \
  {CONFIG_STRING_POSITION_Z,  HELP_STRING_POSITION, 0,        .dblptr=&(position->positionZ),         .defuintval=0,           TYPE_DOUBLE,     0}     \
}
// clang-format on

typedef struct {
  double positionX;
  double positionY;
  double positionZ;
} position_t;

void config_position_coordinates(int Mod_id);
position_t *init_position_coordinates(char *sectionName);
position_t *get_position(int Mod_id);
#endif