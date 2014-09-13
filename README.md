
# Sauerbraten SDL2 Client

Based on SDOS sauerbraten client for windows/linux with build system
changes and some performance/compatibility improvements.

## Key Features

* All SDOS features, except multipoll (multipoll setting has no effect).
* SDL2 uses raw mouse input whenever is possible, so mouse movement
  has no any lags/acceleration.
* Native build system/libraries are used (no static linkage).
* VS instead of mingw for window to get better performance.
* Sauerbraten default sleep/timer to get even more performance
  and compatibility with old systems.
* Improved alt+tabbing.

