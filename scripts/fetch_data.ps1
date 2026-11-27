# Refresh satellite orbital elements from CelesTrak (run from repo root).
New-Item -ItemType Directory -Force assets/tle | Out-Null
curl.exe -sL -o assets/tle/stations.txt "https://celestrak.org/NORAD/elements/gp.php?GROUP=stations&FORMAT=tle"
curl.exe -sL -o assets/tle/active.txt   "https://celestrak.org/NORAD/elements/gp.php?GROUP=active&FORMAT=tle"
