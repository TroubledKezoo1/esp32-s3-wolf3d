#!/bin/bash
search() {
    func=$1
    echo "--- $func ---"
    echo "ORIGINAL:"
    grep -rn "\b$func\b" original_wolf3d/WOLFSRC/*.C original_wolf3d/WOLFSRC/*.H 2>/dev/null | grep -i -E "void $func|int $func|boolean $func|void far $func" | head -1
    echo "PORT:"
    grep -rn "\b$func\b" src/wolf4sdl/*.cpp src/wolf4sdl/*.h src/*.cpp src/*.h 2>/dev/null | grep -i -E "void $func|int $func|boolean $func|void far $func" | head -1
}

search "US_ControlPanel"
search "CP_NewGame"
search "CP_ChangeView"
search "CP_Sound"
search "CP_LoadGame"
search "CP_ViewScores"
search "DemoLoop"
search "CP_Quit"
search "PollControls"
search "GunAttack"
search "TakeDamage"
search "OperateDoor"
search "T_Chase"
search "GetBonus"
search "ThreeDRefresh"
search "SD_StartMusic"
search "SD_PlaySound"
search "IN_ReadControl"
search "PM_Startup"
search "VL_MemToScreen"
search "VL_MemToScreenScaledCoord"
