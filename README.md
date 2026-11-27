# Space Map

A real-time viewer for Earth and space: about 15,800 satellites on a 3D globe, a zoomable live map with aircraft, ships, radar and weather, the sky from your location, the Solar System, and the Milky Way.

| View | Key | Highlights |
|---|---|---|
| Earth | `E` | NASA imagery with city lights at night, clouds, satellites, orbits, ground tracks |
| Map | `M` | Zoom to street level; live aircraft, ships, rain radar, clouds and wind |
| Sky | `S` | Stars, constellations, planets, the Moon and satellites above your horizon, plus a pass planner |
| Solar System | `P` | Planets, Halley's comet, asteroid and Kuiper belts |
| Milky Way | `W` | A spiral galaxy with 17,000 real stars, from megaparsecs down to the Sun's neighbourhood |

## Install

Download from the [Releases](../../releases) page:

- **Windows:** `*-windows-x64-setup.exe` installer, or `*-portable.zip`
- **Linux:** `*-linux-x64.deb` (`sudo apt install ./file.deb`), or `*.tar.gz`

Network features use the system `curl`. Settings and caches live next to the executable when that folder is writable, otherwise in `%LOCALAPPDATA%\SpaceMap` (Windows) or `~/.local/share/spacemap` (Linux).

## Controls

Drag to rotate, pan or look around; wheel to zoom; click an object for details; right-click to set your ground station. Press `F1` in the app for the full list.

| Key | Action |
|---|---|
| `Tab`, `E` `M` `S` `P` `W` | Switch view |
| `/` | Search |
| `A` `B` `D` `K` `Z` `X` | Map layers: aircraft, ships, radar, clouds, wind, weather |
| Space, `,` `.` `T` `N` `J` | Pause, speed, reverse, now, jump to a date |
| `F1` `F2` `F12` | Help, settings, screenshot |

## Build

Needs CMake 3.16+ and a C++17 compiler (raylib is downloaded automatically).

```sh
# Debian / Ubuntu
sudo apt install build-essential cmake git curl libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/spacemap
```

On Windows, use MinGW-w64 or Visual Studio with the same `cmake` commands. Pushing a `v*` tag builds both platforms and publishes a release (see `.github/workflows/build.yml`).

## Data sources

Satellite elements: CelesTrak. Stars: HYG database (CC BY-SA 4.0). Constellations: Stellarium. Earth imagery, clouds and MODIS: NASA GIBS. Base map: OpenStreetMap contributors. Aircraft: adsb.lol and OpenSky Network. Ships: Digitraffic (Baltic and Finnish waters; set `ais_nmea=host:port` in `spacemap.cfg` to add your own AIS stream). Radar: RainViewer. Weather: Open-Meteo. Planet positions: JPL approximate elements.

Layers fetch data only while switched on, and only for the area in view.
