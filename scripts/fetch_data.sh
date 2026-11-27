#!/bin/sh
# Refresh satellite orbital elements from CelesTrak (run from repo root).
mkdir -p assets/tle
curl -sL -o assets/tle/stations.txt "https://celestrak.org/NORAD/elements/gp.php?GROUP=stations&FORMAT=tle"
curl -sL -o assets/tle/active.txt   "https://celestrak.org/NORAD/elements/gp.php?GROUP=active&FORMAT=tle"
