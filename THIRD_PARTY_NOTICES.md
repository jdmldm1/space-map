# Third-Party Notices

Space Map's own source code is licensed under the [MIT License](LICENSE). It uses the
libraries and data listed below, each under its own terms. This file does not change
the license of Space Map's own code; follow the upstream links for the exact terms.

## Libraries

| Component | License | Source |
|---|---|---|
| [raylib](https://www.raylib.com) (statically linked; includes GLFW and stb) | zlib/libpng | https://github.com/raysan5/raylib/blob/master/LICENSE |
| [curl](https://curl.se) (system binary, run as a separate process) | curl license | https://curl.se/docs/copyright.html |
| SGP4 propagator (`src/sgp4.hpp`) | Re-implementation of the published algorithm | Hoots & Roehrich, Spacetrack Report #3; Vallado et al., AIAA 2006-6753 |

## Bundled data (`assets/`)

| Data | Source / terms |
|---|---|
| Star catalogue (`stars.csv`) | [HYG database v4](https://github.com/astronexus/HYG-Database), CC BY-SA 4.0 |
| Constellation figures (`constellations.txt`) | [Stellarium](https://github.com/Stellarium/stellarium) "modern" sky culture line data, with star positions from HYG |
| Earth map (`earth.jpg`) | Blue Marble imagery, NASA Earth Observatory |
| City names (`cities.csv`) | [Natural Earth](https://www.naturalearthdata.com) populated places, public domain |
| Orbital elements (`tle/`) | [CelesTrak](https://celestrak.org) (snapshot; refreshed at run time) |

## Fetched at run time (only while the layer is on)

| Service | Used for | Terms |
|---|---|---|
| [NASA GIBS](https://earthdata.nasa.gov/gibs) | Blue Marble, VIIRS city lights, MODIS, GOES / Himawari imagery | NASA open data policy |
| [OpenStreetMap](https://www.openstreetmap.org/copyright) tile server | Base map | ODbL, attribution required; [tile usage policy](https://operations.osmfoundation.org/policies/tiles/) |
| [adsb.lol](https://adsb.lol) | Aircraft | ODbL |
| [OpenSky Network](https://opensky-network.org) | Aircraft (zoomed out) | Non-commercial / research use |
| [Digitraffic](https://www.digitraffic.fi/en/) | Ship traffic (Baltic) | CC BY 4.0 |
| [RainViewer](https://www.rainviewer.com/api.html) | Rain radar | Attribution required; see their API terms |
| [Open-Meteo](https://open-meteo.com) | Weather and wind | CC BY 4.0, non-commercial free tier |
| JPL approximate planetary positions | Solar System | NASA/JPL, public data |

Check each service's terms before commercial use or heavy automated use.
