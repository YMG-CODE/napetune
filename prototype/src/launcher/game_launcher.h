#pragma once

enum class LauncherSelection
{
    NONE,
    TUNING_MODE,
    MECHA_PROTOCOL,
    SHOWCASE,
    IZ_LOUNGE,
    HANGAR
};

void launcher_begin();
LauncherSelection launcher_update();
void launcher_draw();
