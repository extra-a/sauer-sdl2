#ifndef __EXTENDEDSCRIPTS_H__
#define __EXTENDEDSCRIPTS_H__


const char *extended_settings_gui =
"newgui extended_settings [\n"

    "guicheckbox \"Reduce sparks\" reducesparks\n"
    "guistrut 1\n"

    "guicheckbox \"Show explosions\" explosions\n"
    "guistrut 1\n"

    "guitext \"Smoke density:\" 0\n"
    "guislider smokefps\n"


    "guitab \"Game Clock\"\n"

    "guicheckbox \"Show clock\" gameclock\n"
    "guistrut 1\n"

    "guitext \"Size:\" 0\n"
    "guislider gameclocksize\n"
    "guistrut 1\n"

    "guitext \"Offset (X/Y) ^f4(radar absent)^f~:\" 0\n"
    "guislider gameclockoffset_x\n"
    "guislider gameclockoffset_y\n"
    "guistrut 1\n"

    "guitext \"Offset (X/Y) ^f4(radar present)^f~:\" 0\n"
    "guislider gameclockoffset_x_withradar\n"
    "guislider gameclockoffset_y_withradar\n"

"] \"Extended settings\"\n";

const char *game_scripts[] = { extended_settings_gui, 0 };


#endif
