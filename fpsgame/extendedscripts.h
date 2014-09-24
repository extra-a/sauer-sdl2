#ifndef __EXTENDEDSCRIPTS_H__
#define __EXTENDEDSCRIPTS_H__


const char *extended_settings_gui =
"newgui extended_settings [\n"

    "guicheckbox \"No sparks for hitscan weapons\" reducesparks\n"
    "guicheckbox \"No explosion animation\" reduceexplosions\n"
    "guicheckbox \"No explosion debris\" removeexplosionsdebris\n"
    "guicheckbox \"Colored health\" coloredhealth\n"
    "guicheckbox \"Colored ammo\" coloredammo\n"
    "guicheckbox \"Use following player team\" usefollowingplayerteam\n"
    "guicheckbox \"Log players stats on game end\" dumpstatsongameend\n"
    "guistrut 1\n"

    "guitext \"Smoke density (for rockets and grenades):\" 0\n"
    "guislider smokefps\n"

    "guitab \"Scoreboard\"\n"
    "guicheckbox \"Show flags scored\" showflags\n"
    "guilist [\n"
    "guicheckbox \"Show frags in all modes\" showfrags\n"
    "guibar\n"
    "guicheckbox \"Show net in frags field\" shownetfrags\n"
    "guibar\n"
    "guicheckbox \"Net colors\" netfragscolors\n"
    "]\n"
    "guilist [\n"
    "guicheckbox \"Show damage dealt\" showdamagedealt\n"
    "guibar\n"
    "guicheckbox \"Show net in damage field\" shownetdamage\n"
    "guibar\n"
    "guicheckbox \"Net colors\" netdamagecolors\n"
    "]\n"
    "guicheckbox \"Show accuracy\" showacc\n"

    "guitab \"Clock\"\n"

    "guilist [\n"
    "guicheckbox \"Show clock\" gameclock\n"
    "guibar\n"
    "guicheckbox \"Disable with GUI\" gameclockdisablewithgui\n"
    "]\n"
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
    "guistrut 1\n"

    "guitext \"Color (^f3R^f~/^f0G^f~/^f1B^f~/^f4A^f~):\" 0\n"
    "guislider gameclockcolor_r\n"
    "guislider gameclockcolor_g\n"
    "guislider gameclockcolor_b\n"
    "guislider gameclockcolor_a\n"

    "guitab \"HudScores\"\n"

    "guilist [\n"
    "guicheckbox \"Show hud scores\" hudscores\n"
    "guibar\n"
    "guicheckbox \"Disable with GUI\" hudscoresdisablewithgui\n"
    "]\n"

    "guitext \"Size:\" 0\n"
    "guislider hudscoressize\n"
    "guistrut 1\n"

    "guitext \"Offset (X/Y) ^f4(radar absent)^f~:\" 0\n"
    "guislider hudscoresoffset_x\n"
    "guislider hudscoresoffset_y\n"
    "guistrut 1\n"

    "guitext \"Offset (X/Y) ^f4(radar present)^f~:\" 0\n"
    "guislider hudscoresoffset_x_withradar\n"
    "guislider hudscoresoffset_y_withradar\n"
    "guistrut 1\n"

    "guitab \"Scores Colors\"\n"

    "guitext \"Player color (^f3R^f~/^f0G^f~/^f1B^f~/^f4A^f~):\" 0\n"
    "guislider hudscoresplayercolor_r\n"
    "guislider hudscoresplayercolor_g\n"
    "guislider hudscoresplayercolor_b\n"
    "guislider hudscoresplayercolor_a\n"
    "guistrut 1\n"

    "guitext \"Enemy color (^f3R^f~/^f0G^f~/^f1B^f~/^f4A^f~):\" 0\n"
    "guislider hudscoresenemycolor_r\n"
    "guislider hudscoresenemycolor_g\n"
    "guislider hudscoresenemycolor_b\n"
    "guislider hudscoresenemycolor_a\n"

    "guitab \"Ammobar\"\n"

    "guilist [\n"
    "guicheckbox \"Show ammobar\" ammobar\n"
    "guibar\n"
    "guicheckbox \"Disable with GUI\" ammobardisablewithgui\n"
    "]\n"
    "guicheckbox \"Horizontal ammobar\" ammobarhorizontal\n"
    "guistrut 1\n"

    "guitext \"Size:\" 0\n"
    "guislider ammobarsize\n"
    "guistrut 1\n"
    
    "guitext \"Offset (X/Y):\" 0\n"
    "guislider ammobaroffset_x\n"
    "guislider ammobaroffset_y\n"
    "guistrut 1\n"

    "guicheckbox \"Draw selected gun background\" ammobarselectedbg\n"
    "guitext \"Selected gun background (^f3R^f~/^f0G^f~/^f1B^f~/^f4A^f~):\" 0\n"
    "guislider ammobarselectedcolor_r\n"
    "guislider ammobarselectedcolor_g\n"
    "guislider ammobarselectedcolor_b\n"
    "guislider ammobarselectedcolor_a\n"

"] \"Settings\"\n";

const char *game_scripts[] = { extended_settings_gui, 0 };


#endif
