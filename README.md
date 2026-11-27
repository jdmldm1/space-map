# Space Map

Real-time 3D Earth with ~15,800 satellites (SGP4), a zoomable live map (aircraft, ships, radar, clouds, wind), the Solar System, and the Milky Way.
C++17 + [raylib](https://www.raylib.com) (OpenGL 3.3). Builds on Windows and Debian/Ubuntu.

| View | Key | What you get |
|---|---|---|
| Globe | `E` | Lit Earth from NASA imagery (5120x2560 Blue Marble, **city lights on the night side**), drifting clouds, ocean glint, atmosphere, Moon, a *real* star sky with **constellation figures**, satellites with full catalogue metadata, orbits, ground tracks, passes |
| Sky | `S` | **The sky from your ground station**: real stars, 88 constellations, planets, the Moon with its phase, the Sun and twilight colours, every satellite above your horizon (dimmed when in Earth's shadow), and a **pass planner** for the ISS, Tiangong and Hubble; drag to look around, wheel to zoom |
| Map | `M` | Web-mercator map, zoom to street level, live terminator; base maps (Blue Marble / OSM dark / OSM / NASA MODIS daily); live **aircraft** (`A`), **ships** (`B`), **rain radar** (`D`), **clouds** (`K`), **wind** (`Z`), **weather** at the map centre (`X`) |
| Solar System | `P` | All planets + Pluto (JPL ephemerides), Halley's comet, asteroid belt, Jupiter's Trojans, Kuiper belt; time up to 160 million x |
| Milky Way | `W` | Four-arm spiral galaxy, the Sun at 8.18 kpc, 17,000 real stars, Magellanic Clouds, Andromeda, Triangulum; zoom from Mpc to 2 pc |

`Tab` cycles the views. The toolbar mirrors the keys; **Layers** (overlays, base map, filters), **Settings** (units, time zone, marker size, filters; persisted to `spacemap.cfg` in the data folder)
and **Help** (`F1`) are one click away. Search (`/`) works in every view.

## Install

Download the latest build from the [Releases](../../releases) page:

| Platform | File | Notes |
|---|---|---|
| Windows | `SpaceMap-<version>-windows-x64-setup.exe` | Installer with Start-menu entry and optional desktop shortcut |
| Windows | `SpaceMap-<version>-windows-x64-portable.zip` | Unzip anywhere and run `spacemap.exe` |
| Linux (Debian / Ubuntu) | `SpaceMap-<version>-linux-x64.deb` | `sudo apt install ./SpaceMap-<version>-linux-x64.deb`; adds a desktop entry and icon |
| Linux (any) | `SpaceMap-<version>-linux-x64.tar.gz` | Extract and run `usr/bin/spacemap` |

The network features use the system `curl` (included with Windows 10+ and installed as a dependency of the `.deb`).

Settings, caches (map tiles, imagery) and refreshed satellite data are stored next to the executable when that folder is writable (portable / development use), otherwise in
`%LOCALAPPDATA%\SpaceMap` on Windows or `~/.local/share/spacemap` on Linux. Satellite orbital elements are refreshed from CelesTrak in the background when more than 12 hours old and
used at the next launch.

## Build

Requirements: CMake 3.16+, a C++17 compiler, and (on Linux) the X11/OpenGL development packages. raylib is downloaded and built automatically on first configure (needs internet).

### Debian / Ubuntu
```sh
sudo apt install build-essential cmake git curl libgl1-mesa-dev libx11-dev libxrandr-dev \
                 libxinerama-dev libxcursor-dev libxi-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/spacemap
```

### Windows
MinGW-w64 (the same toolchain the release builds use) or Visual Studio 2019+ with "Desktop development with C++":
```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\spacemap.exe
```
Pass `-DSPACEMAP_GUI_SUBSYSTEM=ON` with MinGW to build without a console window (as the release does).

### Packages
```sh
cd build && cpack                    # Linux: .deb and .tar.gz
iscc /DAppVersion=1.0.0 installer/spacemap.iss   # Windows installer (needs Inno Setup 6)
```

### Releases
Pushing a tag such as `v1.0.1` runs `.github/workflows/build.yml`, which builds Windows and Linux, creates the installers and packages above, and attaches them to a GitHub release.

## Controls
| Key / mouse | Action |
|---|---|
| Drag / wheel | rotate and zoom (globe, solar system, galaxy); pan and zoom about the cursor (map) |
| Click | select an object; the info panel shows its metadata (satellites: NORAD id, launch, orbit class, passes; aircraft: airline, type, registration country, climb state, squawk; ships: flag, type, status, destination, ETA, size, draught; planets; stars) |
| Right-click | place your ground station on the globe or map (saved to `spacemap_station.txt`) |
| `Tab`, `E` `M` `S` `P` `W` | next view / Earth / map / Sky / Solar System / Milky Way |
| `J` | jump to any date and time (UTC): type it, or step by hours and days |
| `Q` | constellation lines (globe, sky and solar-system backdrops) |
| `/` | search; Up/Down + Enter jumps to and follows the result |
| `F` / `H` / `R` | follow the selection / home (station on the map, the Sun in the galaxy) / reset view |
| `A` `B` `D` `K` `Z` `X` | map layers: aircraft, ships, rain radar, cloud imagery, wind vectors, weather readout |
| `Y` | cycle the map base layer |
| `U` | cycle units (metric / imperial / aviation-marine) |
| Space, `,` `.` `T` `1` `N` | pause, slower/faster time, reverse, real time, jump to now |
| `G` `L` `O` `C` | ground track, labels, orbit lines, LEO constellations |
| `F1` `F2` `F12` | help, settings, screenshot (`spacemap.png`) |
| Esc | close panels -> release follow -> deselect -> quit |

`spacemap --shot <prefix>` runs a scripted tour of every view (including live data) and saves screenshots; it is how the app is tested.

## Data
- `assets/stars.csv` - 17k stars from the HYG database v4.0 (astronexus/HYG-Database, CC BY-SA 4.0; mag <= 7, named stars, and everything within 20 pc).
- `assets/constellations.txt` - 88 constellation figures (Stellarium "modern" sky culture, line data from index.json; star positions from HYG).
- `assets/earth.jpg` – equirectangular Earth map (replace with any 2:1 map, e.g. a higher-res Blue Marble). If missing, a procedural Earth is generated.
- `assets/tle/stations.txt`, `assets/tle/active.txt` – CelesTrak TLEs (~15.8k satellites). Refresh with `scripts/fetch_data.ps1` or `scripts/fetch_data.sh`.
  If no TLE files are found, a built-in demo scene is used.
- The sim clock starts at real UTC (`N` jumps back to now). Near-Earth objects (period < 225 min, ~95% of the catalogue) are propagated
  with **SGP4** (`src/sgp4.hpp`, verified against the published 00005 reference vectors). Deep-space objects fall back to two-body Kepler + J2.
  Accuracy still degrades as the TLE epoch gets old (the info panel shows element age and which model is in use).
- Ground-station passes: right-click to place a station; the panel lists the next passes and marks those that are *visible*
  (object sunlit, observer in twilight/night).
## Network features (all optional, on demand)
Everything below is fetched through the system `curl`, only while the layer is on, and only for what you are looking at.
- **Aircraft (`A`)**: [adsb.lol](https://adsb.lol) when zoomed in (circle around the map centre, <= 250 nm, every 5 s) and the [OpenSky Network](https://opensky-network.org) when zoomed out
  (viewport box, anonymous rate limits apply, so it backs off on errors). Only the map viewport (centre and radius) is sent.
- **Ships (`B`)**: [Digitraffic](https://www.digitraffic.fi/en/marine-traffic/) AIS (open data, no key) - covers the **Baltic and Finnish waters** (~600 vessels). Worldwide AIS needs a keyed
  service, so there is an escape hatch: set `ais_nmea=host:port` in `spacemap.cfg` to ingest any raw NMEA AIS stream (your own receiver, or a public stream); the built-in decoder
  handles message types 1-3, 5, 18, 19 and 24 (verified against a published reference sentence).
- **Rain radar (`D`)**: [RainViewer](https://www.rainviewer.com/api.html) animated radar (13 frames, 10-minute steps, zoom <= 7).
- **Clouds (`K`)**: NASA [GIBS](https://earthdata.nasa.gov/gibs) infrared imagery from GOES-East/West and Himawari (Americas and Asia-Pacific); **NASA MODIS** daily true-colour is a base map.
- **Wind and weather (`Z`, `X`)**: [Open-Meteo](https://open-meteo.com) (current conditions at the map centre and a wind-vector grid over the view).
- **Earth imagery**: on first run the app builds two NASA [GIBS](https://earthdata.nasa.gov/gibs) maps in the background (Blue Marble shaded relief + bathymetry, and VIIRS Black Marble city lights, ~3 MB, cached as `earth_hd.jpg` / `earth_night.jpg` next to the executable) and swaps them in live; until then the bundled 2K map is used.
- **Base maps**: OpenStreetMap's standard tile server, requested on demand, cached under `tiles/`, at most 2 connections, identified by a `spacemap/1.0` User-Agent, per the
  [tile usage policy](https://operations.osmfoundation.org/policies/tiles/). (c) OpenStreetMap contributors. Please keep usage light.
- Without internet the app still works (Blue Marble map, all satellite and astronomy views); layers show an "unavailable" status.

## Layout / extending
- `res/` - application icon (`spacemap.ico` for the .exe, `app.rc` resource script, `make_icon.py` regenerates the icon from `assets/earth.jpg` and re-embeds it as `src/icon_png.hpp`).
- `src/sgp4.hpp` – SGP4 near-Earth propagator.
- `src/solar.hpp` - JPL planetary elements + Halley. `src/galaxy.hpp` - star catalogue, galactic coordinates, procedural Milky Way.
- `src/adsb.hpp`, `src/ais.hpp`, `src/weather.hpp`, `src/tiles.hpp`, `src/net.hpp` - live feeds (aircraft, AIS + NMEA decoder, radar/weather), multi-layer map-tile cache, curl helper.
- `src/ui.hpp`, `src/meta.hpp` - UI toolkit (font, options, widgets, panels) and airline / aircraft-type / registration / satellite-group lookup tables.
- `src/orbit.hpp` – `Orbit`: Keplerian + J2, optionally backed by SGP4, `mu` is per-orbit so other parent bodies work.
- `src/main.cpp` – `Body` (Earth today; add planets/moons by appending Bodies and giving `Entity::parent` an index),
  `buildScene()` defines all objects, shaders for day/night + atmosphere, camera, picking, HUD.
- Scene units: 1 unit = 1000 km, +Y = Earth's spin axis.
