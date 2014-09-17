
# Sauerbraten SDL2 Client

SDOS based sauerbraten client for windows/linux with build system
changes and some performance/compatibility improvements.  Also some
additional visual options, that have been missing for years, are
provided. Use "extendedsettings" command to for configuring.

## Key Features

* All SDOS features, except multipoll (multipoll setting has no effect).
* SDL2 uses raw mouse input whenever is possible, so mouse movement
  has no any lags/acceleration.
* Native build system/libraries are used (no static linkage).
* VS instead of mingw for window to get better performance.
* Sauerbraten default sleep/timer to get even more performance
  and compatibility with old systems.
* Improved alt+tabbing.
* ignoreserver command to permanently ignore any server on the master
  list.
* Easy additional visual settings configuration via "extendedsettings"
  command.
* Accuracy/damage menu command "showplayerstats".
* More scoreboard statistics.

