#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "adsb.hpp"
#include "catalog.hpp"
#include "cities.hpp"
#include "earthhd.hpp"
#include "formatting.hpp"
#include "galaxy.hpp"
#include "icon_png.hpp"
#include "meta.hpp"
#include "observer.hpp"
#include "orbit.hpp"
#include "paths.hpp"
#include "render.hpp"
#include "shaders.hpp"
#include "solar.hpp"
#include "tiles.hpp"
#include "tle_update.hpp"
#include "ui.hpp"
#include "views.hpp"
#include "weather.hpp"
#include "world_data.hpp"

static void traceFilter(int level, const char* text, va_list args) {
    char buf[1200];
    vsnprintf(buf, sizeof buf, text, args);
    if (level == LOG_INFO && (std::strstr(buf, "FILEIO") || std::strstr(buf, "TEXTURE:") || std::strstr(buf, "FONT:") || std::strstr(buf, "SHADER:"))) return;
    std::printf("%s\n", buf);
}

int main(int argc, char** argv) {
    std::string shotPrefix;
    bool benchMode = false;
    for (int i = 1; i < argc; ++i) { if (i + 1 < argc && std::string(argv[i]) == "--shot") shotPrefix = argv[i + 1]; if (std::string(argv[i]) == "--bench") benchMode = true; }

    SetTraceLogCallback(traceFilter);
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI);
    InitWindow(1280, 720, "Space Map");
    {
        Image icon = LoadImageFromMemory(".png", kIconPng, (int)sizeof(kIconPng));
        if (icon.data) { SetWindowIcon(icon); UnloadImage(icon); }
    }
    SetExitKey(KEY_NULL);
    SetTargetFPS(0);
    Options opts;
    opts.load();
    uiLoadFont();
    if (!opts.vsync) ClearWindowState(FLAG_VSYNC_HINT);
    Ui ui;

    std::vector<Body> bodies;
    bodies.push_back({ "Earth", 6371.0, 398600.4418, 7.2921159e-5, { 0, 0, 0 }, { 60, 130, 255, 255 } });
    const Body& earth = bodies[0];
    const float earthR = (float)(earth.radiusKm * kSceneUnitsPerKm);

    TleUpdater tleUpdater;
    tleUpdater.start(paths::dataPath("tle/"));

    std::vector<Entity> entities = loadSatelliteCatalog(earth);

    Shader earthSh = LoadShaderFromMemory(VS, FS_EARTH);
    Shader atmoSh = LoadShaderFromMemory(VS, FS_ATMO);
    Shader moonSh = LoadShaderFromMemory(VS, FS_MOON);
    Shader mapSh = LoadShaderFromMemory(VS2D, FS_MERC);
    Shader tileSh = LoadShaderFromMemory(VS2DC, FS_TILE);
    const int tileDarkLoc = GetShaderLocation(tileSh, "dark");
    const int mapSunDirLoc = GetShaderLocation(mapSh, "sunEcef"), mapCenterLoc = GetShaderLocation(mapSh, "center"), mapSpanLoc = GetShaderLocation(mapSh, "span");
    int earthSunDirLoc = GetShaderLocation(earthSh, "sunDir"), earthViewPosLoc = GetShaderLocation(earthSh, "viewPos");
    const int earthSunObjectLoc = GetShaderLocation(earthSh, "sunObj"), earthCloudTimeLoc = GetShaderLocation(earthSh, "cloudT"), earthCloudAmountLoc = GetShaderLocation(earthSh, "cloudAmt"), earthSpecularLoc = GetShaderLocation(earthSh, "specAmt");
    int atmosphereSunDirLoc = GetShaderLocation(atmoSh, "sunDir"), atmosphereViewPosLoc = GetShaderLocation(atmoSh, "viewPos");
    int moonSunDirLoc = GetShaderLocation(moonSh, "sunDir");

    Texture2D earthTex = loadEarthTexture();
    GenTextureMipmaps(&earthTex);
    SetTextureFilter(earthTex, TEXTURE_FILTER_TRILINEAR);

    Model earthModel = LoadModelFromMesh(makeSphere(earthR, 96, 192));
    earthModel.materials[0].shader = earthSh;
    SetMaterialTexture(&earthModel.materials[0], MATERIAL_MAP_DIFFUSE, earthTex);
    Texture2D nightTex;
    {
        Image bi = GenImageColor(1, 1, BLACK);
        nightTex = LoadTextureFromImage(bi);
        UnloadImage(bi);
    }
    SetMaterialTexture(&earthModel.materials[0], MATERIAL_MAP_SPECULAR, nightTex);
    const int earthNightLightsLoc = GetShaderLocation(earthSh, "nightAmt");
    const int mapNightLightsLoc = GetShaderLocation(mapSh, "nightAmt"), mapNightTextureLoc = GetShaderLocation(mapSh, "texture1");
    bool hdDay = false, hdNight = false;
    EarthImagery imagery;
    imagery.start(paths::dataDirectory());

    const ConstellationFigures constellationFigures = buildConstellationFigures();
    const std::vector<ConstellationSegment>& consSegs = constellationFigures.segments;
    const std::vector<ConstellationLabel>& consLabels = constellationFigures.labels;
    Model atmoModel = LoadModelFromMesh(makeSphere(earthR * 1.035f, 64, 128));
    atmoModel.materials[0].shader = atmoSh;
    Model moonModel = LoadModelFromMesh(makeSphere(1.7374f, 32, 64));
    moonModel.materials[0].shader = moonSh;

    const std::vector<StarRec> catalog = loadStars(paths::assetPath("assets/stars.csv"));
    TraceLog(LOG_INFO, "Star catalogue: %zu stars", catalog.size());
    const std::vector<City> cities = loadCities(paths::assetPath("assets/cities.csv"));
    const StarSky starSky = buildStarSky(catalog);
    const std::vector<SkyStar>& sky = starSky.stars;
    const std::vector<std::pair<Vector3, Color>>& band = starSky.milkyWayBand;
    const GalaxyScene galaxyScene = buildGalaxyScene(catalog);
    const std::vector<GalPoint>& galaxy = galaxyScene.points;
    const std::vector<Vector3>& catPos = galaxyScene.starPositions;
    const std::vector<Color>& catCol = galaxyScene.starColors;
    const std::vector<unsigned char>& catSz = galaxyScene.starSizes;
    const std::vector<GalObj>& gal = galaxyScene.objects;
    const Vector3 sunGal = galaxyScene.sunPosition;
    const int galSunIdx = galaxyScene.sunIndex, galGcIdx = galaxyScene.centreIndex;
    const std::vector<Rock> rocks = buildSmallBodyField();
    const SmallBody halleyBody = halley();

    const int BODY_HALLEY = NPLANETS, BODY_SUN = NPLANETS + 1;
    std::vector<Vec3d> bodyPos(NPLANETS + 2);

    TileCache tiles(paths::dataPath("tiles"));
    bool tilesStarted = false;
    AdsbFeed adsb(paths::dataPath("adsb_tmp.json"));
    adsb.start();
    const std::string appDir = paths::dataDirectory();
    AisFeed ais(appDir, opts.aisNmea);
    ais.start();
    RadarFeed radarFeed(appDir + "radar_tmp.json");
    radarFeed.start();
    WeatherFeed wx(appDir);
    wx.start();
    std::vector<Vessel> ships;
    unsigned shipVer = 0;
    std::vector<Vector2> shipScreen;
    std::vector<std::pair<double, double>> shipLatLon;
    int selectedShip = 0, hoveredShip = -1;
    bool shipsOn = false, wxProbe = false;
    std::vector<RadarFrame> radarFrames;
    std::vector<int> radarLayers;
    unsigned radarVer = 0;
    int radarIdx = 0, radarShown = 0;
    bool radarPlay = true;
    double radarNext = 0;
    const int osmLayer = tiles.addLayer("osm", "https://tile.openstreetmap.org/{z}/{x}/{y}.png", ".png");
    int modisLayer = -1, irLayer = -1;
    std::string irKey;
    double layerCheck = -1000;
    bool settingsOpen = false, helpOpen = false, layersOpen = false;
    std::string pendingShot;
    auto toggleShips = [&]() { shipsOn = !shipsOn; ais.setEnabled(shipsOn); };
    auto toggleRadar = [&]() { opts.radarOn = !opts.radarOn; radarFeed.setEnabled(opts.radarOn); };
    std::vector<Aircraft> aircraftList;
    unsigned acVer = 0;
    std::vector<Vector2> aircraftScreen;
    std::vector<std::pair<double, double>> aircraftLatLon;
    std::string selectedAc;
    int hoveredAc = -1;

    float yaw = 0.8f, pitch = 0.45f, yawV = 0.0f, pitchV = 0.0f;
    float dist = 22.0f, distGoal = 22.0f;
    Vector3 camTarget{ 0, 0, 0 }, lastGoal{ 0, 0, 0 };
    float followBlend = 0.0f;
    bool follow = false;
    Cam solarCam, galCam;
    PointCanvas canvas;
    solarCam.dist = solarCam.goal = 11.0; solarCam.pitch = 0.55f;
    galCam.dist = galCam.goal = 32000.0; galCam.pitch = 0.95f; galCam.yaw = 0.3f;
    double mapCentreU = 0.5, mapCentreV = 0.42, mapWidthPx = 1280.0;
    bool mapInit = false;
    Mode mode = Mode::Globe;

    double simTime = nowUnix(), timeScale = 1.0;
    bool paused = false, showLabels = true, showOrbits = false, showConstellation = true, showTrack = true;
    int selected = -1, hovered = -1, selBody = -1, hovBody = -1, selGal = -1, hovGal = -1;
    float dragDist = 0.0f;
    bool followBody = false, followGal = false;

    Station station;
    int stationVer = 0;
    const std::string stationFile = paths::dataPath("spacemap_station.txt");
    {
        std::ifstream f(stationFile);
        double la, lo;
        if (f >> la >> lo) { station = { true, la, lo }; stationVer = 1; }
    }
    auto setStation = [&](double lat, double lon) {
        station = { true, lat, lon }; ++stationVer;
        std::ofstream f(stationFile);
        f << lat << " " << lon << "\n";
    };
    std::vector<Pass> passes;
    int passSel = -2, passVer = -1;
    double passT = 0;
    const unsigned nThreads = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));

    bool searching = false;
    std::string query;
    std::vector<int> matches;
    int matchSel = 0;
    auto candCount = [&]() -> size_t { return mode == Mode::Solar ? (size_t)NPLANETS + 2 : mode == Mode::Galaxy ? gal.size() : entities.size(); };
    auto candName = [&](size_t i) -> std::string {
        if (mode == Mode::Solar) return i < (size_t)NPLANETS ? PLANETS[i].name : i == (size_t)BODY_HALLEY ? "Halley" : "Sun";
        if (mode == Mode::Galaxy) return gal[i].name;
        return entities[i].name;
    };
    auto candColor = [&](size_t i) -> Color {
        if (mode == Mode::Solar) return i < (size_t)NPLANETS ? Color{ PLANETS[i].r, PLANETS[i].g, PLANETS[i].b, 255 } : Color{ 255, 230, 150, 255 };
        if (mode == Mode::Galaxy) return gal[i].col;
        return entities[i].color;
    };
    auto recompute = [&]() {
        matches.clear(); matchSel = 0;
        const std::string q = lowerStr(query);
        if (q.empty()) return;
        std::vector<int> prefix, other;
        const size_t n = candCount();
        for (size_t i = 0; i < n; ++i) {
            const std::string nm = lowerStr(candName(i));
            size_t pos = nm.find(q);
            if (pos == std::string::npos && mode != Mode::Solar && mode != Mode::Galaxy && entities[i].norad) pos = std::to_string(entities[i].norad).find(q);
            if (pos == 0) prefix.push_back((int)i); else if (pos != std::string::npos) other.push_back((int)i);
        }
        matches = prefix;
        matches.insert(matches.end(), other.begin(), other.end());
    };
    float skyAz = (float)PI, skyEl = 0.6f, skyFov = 110.0f;
    bool skyInit = false;
    auto setMode = [&](Mode m) {
        if (m == mode) return;
        mode = m; follow = false; followBody = false; followGal = false; searching = false;
        yawV = pitchV = 0; solarCam.yawV = solarCam.pitchV = galCam.yawV = galCam.pitchV = 0;
        if (m == Mode::Map && !mapInit) { mapInit = true; mapWidthPx = (double)GetScreenWidth(); }
        if (m == Mode::Sky && !skyInit) { skyInit = true; skyAz = (float)PI; skyEl = 0.6f; skyFov = 110.0f; }
    };
    auto focus = [&](int idx) {
        if (mode == Mode::Solar) {
            selBody = idx; followBody = true;
            solarCam.goal = idx == BODY_SUN ? 0.08 : idx == BODY_HALLEY ? 0.5 : std::max(0.01, PLANETS[idx].radiusKm / AU_KM * 40.0);
        } else if (mode == Mode::Galaxy) {
            selGal = idx; followGal = true; galCam.tgoal = gal[idx].pos;
            galCam.goal = gal[idx].kind == 2 ? gal[idx].sizePc * 4.0 : idx == galSunIdx ? 40.0 : idx == galGcIdx ? 6000.0 : 12.0;
        } else { selected = idx; follow = true; distGoal = 0.6f; yawV = pitchV = 0; }
    };

    std::vector<Vector2> screenPositions(entities.size());
    std::vector<char> vis(entities.size());
    std::vector<char> skyLit(entities.size(), 1);
    struct SkyPass { int ent; Pass ps; };
    std::vector<SkyPass> skyPasses;
    int skyPassVer = -1;
    double skyPassT = 0;
    bool skyVisOnly = false;
    std::vector<int> notable;
    for (size_t i = 0; i < entities.size(); ++i) if (entities[i].labeled && entities[i].name.find("ZVEZDA") == std::string::npos) notable.push_back((int)i);

    bool timeOpen = false;
    std::string timeText;
    int frame = 0;
    int scriptStep = 0;
    double scriptT = 0;
    auto orbitOn = [&](const Entity& e) { return e.oclass == 0 ? opts.satLEO : e.oclass == 1 ? opts.satMEO : e.oclass == 2 ? opts.satGEO : opts.satHEO; };

    float frameSeconds = 0.0f, screenWidth = 0.0f, screenHeight = 0.0f, wheelDelta = 0.0f, nightAmt = 0.0f, nearP = 0.01f;
    bool satMode = false, dragging = false, pickOk = false, showQuickToggles = false;
    double daysSinceJ2000 = 0.0, earthRotationAngle = 0.0, steadyNow = 0.0;
    Vec3d sunDirection{}, sunDirectionEarthFixed{};
    Vector3 sunSceneDirection{}, moonScenePosition{}, fwd{ 0, 0, -1 };
    Vector2 mouse{};
    Camera3D cam{};
    Matrix vp = MatrixIdentity();
    Station skyObs;
    Vec3d skyE{ 1, 0, 0 }, skyN{ 0, 1, 0 }, skyU{ 0, 0, 1 }, obsPos{ 0, 0, 0 };
    double sunAltDeg = 0.0, skyK = 1.0, skyP[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    double fx = 0, fy = 1, fz = 0, rxv = 1, ryv = 0, ux = 0, uy = 0, uz = 1;
    std::vector<Vector2> bodyScr, galScr;
    float bx = 8.0f, rx = 0.0f, layersX = 8.0f;
    int readoutBottomY = 0;
    std::string hoverTitle, hoverDetail;
    const Color mutedColor{ 150, 158, 175, 255 }, positiveColor{ 90, 255, 140, 255 }, textColor{ 235, 238, 245, 255 };
    const float toolbarHeight = 34.0f;

    auto applyOrbitDrag = [&](float& y, float& p, float& yv, float& pv) {
        if (dragging) {
            const Vector2 d = GetMouseDelta();
            y -= d.x * 0.005f;
            p = Clamp(p + d.y * 0.005f, -1.5f, 1.5f);
            if (frameSeconds > 0.0f) { yv = Lerp(yv, -d.x * 0.005f / frameSeconds, 0.5f); pv = Lerp(pv, d.y * 0.005f / frameSeconds, 0.5f); }
        } else {
            y += yv * frameSeconds;
            p = Clamp(p + pv * frameSeconds, -1.5f, 1.5f);
            const float k = std::exp(-3.5f * frameSeconds);
            yv *= k; pv *= k;
        }
    };

    auto projectToScreen = [&](Vector3 p, float* wOut = nullptr) -> Vector2 {
        const float x = vp.m0 * p.x + vp.m4 * p.y + vp.m8 * p.z + vp.m12;
        const float y = vp.m1 * p.x + vp.m5 * p.y + vp.m9 * p.z + vp.m13;
        const float w = vp.m3 * p.x + vp.m7 * p.y + vp.m11 * p.z + vp.m15;
        if (wOut) *wOut = w;
        return { (x / w + 1.0f) * 0.5f * screenWidth, (-y / w + 1.0f) * 0.5f * screenHeight };
    };

    auto applyCamera = [&](Vector3 position, Vector3 target, double nearP, double farP) {
        cam.position = position; cam.target = target; cam.up = { 0, 1, 0 }; cam.fovy = 45.0f; cam.projection = CAMERA_PERSPECTIVE;
        vp = MatrixMultiply(MatrixLookAt(position, target, cam.up), MatrixPerspective((double)cam.fovy * DEG, (double)screenWidth / screenHeight, nearP, farP));
    };

    auto projectSkyDirection = [&](double e, double n, double u, Vector2& out) -> bool {
        const double z = e * fx + n * fy + u * fz;
        if (z <= -0.96) return false;
        const double x = e * rxv + n * ryv, y = e * ux + n * uy + u * uz;
        out = { (float)(screenWidth * 0.5 + 2.0 * x / (1.0 + z) * skyK), (float)(screenHeight * 0.5 - 2.0 * y / (1.0 + z) * skyK) };
        return true;
    };

    auto precessToDate = [&](const Vec3d& v) { return Vec3d{ skyP[0][0] * v.x + skyP[0][1] * v.y + skyP[0][2] * v.z, skyP[1][0] * v.x + skyP[1][1] * v.y + skyP[1][2] * v.z, skyP[2][0] * v.x + skyP[2][1] * v.y + skyP[2][2] * v.z }; };

    auto toHorizontalFrame = [&](const Vec3d& v, double& e, double& n, double& u) {
        e = v.x * skyE.x + v.y * skyE.y + v.z * skyE.z; n = v.x * skyN.x + v.y * skyN.y + v.z * skyN.z; u = v.x * skyU.x + v.y * skyU.y + v.z * skyU.z;
    };

    auto mercatorWorldY = [](double latDeg) { const double s = std::sin(std::min(std::max(latDeg, -85.0511), 85.0511) * DEG); return 0.5 - std::log((1.0 + s) / (1.0 - s)) / (4.0 * PI_D); };

    auto latLonToScreen = [&](double latDeg, double lonDeg) -> Vector2 {
        double du2 = (lonDeg + 180.0) / 360.0 - mapCentreU; du2 -= std::floor(du2 + 0.5);
        return { (float)(screenWidth * 0.5 + du2 * mapWidthPx), (float)(screenHeight * 0.5 + (mercatorWorldY(latDeg) - mapCentreV) * mapWidthPx) };
    };

    auto screenToLatLon = [&](double px, double py, double& lat, double& lon) {
        const double u = mapCentreU + (px - screenWidth * 0.5) / mapWidthPx, v = mapCentreV + (py - screenHeight * 0.5) / mapWidthPx;
        lon = (u - std::floor(u)) * 360.0 - 180.0;
        lat = std::atan(std::sinh(PI_D * (1.0 - 2.0 * v))) / DEG;
    };

    auto passesAircraftFilter = [&](const Aircraft& a) {
        static const double minAlt[] = { 0, 10000, 20000, 30000 };
        if (a.ground) return opts.acGround;
        return !(a.hasAlt && a.altFt < minAlt[opts.acMinAlt]);
    };

    auto passesShipFilter = [&](const Vessel& v) {
        const int c = ais::category(v.shipType);
        const bool catOn = c == 0 ? opts.shipCargo : c == 1 ? opts.shipTanker : c == 2 ? opts.shipPass : c == 3 ? opts.shipFish : opts.shipOther;
        return catOn && !(opts.shipMoving && v.sog < 0.5);
    };

    auto solarRelativePosition = [&](const Vec3d& p) { return Vector3Subtract(Vector3{ (float)p.x, (float)p.z, (float)-p.y }, solarCam.target); };

    auto drawStarBackdrop = [&](const Vector3& centre) {
        for (const auto& p : band) {
            if (Vector3DotProduct(p.first, fwd) <= 0.0f) continue;
            const Vector2 s = projectToScreen(Vector3Add(centre, Vector3Scale(p.first, 100.0f)));
            DrawPixelV(s, p.second);
        }
        if (opts.showConst)
            for (const ConstellationSegment& g : consSegs) {
                const Vector3 a{ (float)g.from.x, (float)g.from.z, (float)-g.from.y }, b{ (float)g.to.x, (float)g.to.z, (float)-g.to.y };
                if (Vector3DotProduct(a, fwd) <= 0.0f || Vector3DotProduct(b, fwd) <= 0.0f) continue;
                DrawLineV(projectToScreen(Vector3Add(centre, Vector3Scale(a, 100.0f))), projectToScreen(Vector3Add(centre, Vector3Scale(b, 100.0f))), Fade({ 95, 140, 225, 255 }, 0.30f));
            }
        for (const auto& s : sky) {
            if (Vector3DotProduct(s.direction, fwd) <= 0.0f) continue;
            const Vector2 p = projectToScreen(Vector3Add(centre, Vector3Scale(s.direction, 100.0f)));
            const Color sc{ (unsigned char)std::min(255, (int)(s.color.r * opts.starBright)), (unsigned char)std::min(255, (int)(s.color.g * opts.starBright)), (unsigned char)std::min(255, (int)(s.color.b * opts.starBright)), 255 };
            if (s.pixelSize == 1) DrawPixelV(p, sc);
            else DrawRectangle((int)p.x - s.pixelSize / 2, (int)p.y - s.pixelSize / 2, s.pixelSize, s.pixelSize, sc);
        }
    };

    auto addLeftToolbarButton = [&](const char* label, bool active, const char* tip) -> bool {
        const float w = (float)UiMeasure(label, 13) + 22.0f;
        const bool c = ui.button({ bx, 5, w, 24 }, label, active, tip);
        bx += w + 4.0f;
        return c;
    };

    auto addRightToolbarButton = [&](const char* label, bool active, const char* tip) -> bool {
        const float w = (float)UiMeasure(label, 13) + 22.0f;
        rx -= w;
        const bool c = ui.button({ rx, 5, w, 24 }, label, active, tip);
        rx -= 4.0f;
        return c;
    };

    auto drawPopupSectionTitle = [&](float x, float& y, const char* title) {
        UiText(title, (int)x + 12, (int)y, 11, { 120, 130, 150, 255 });
        DrawLine((int)x + 20 + UiMeasure(title, 11), (int)y + 7, (int)x + 276, (int)y + 7, Fade(WHITE, 0.09f));
        y += 19;
    };

    float viewFade = 0.0f;
    auto faceGlobeTowards = [&](double latitudeDeg, double longitudeDeg) {
        const double lat = latitudeDeg * DEG, inertialLongitude = longitudeDeg * DEG + earthRotationAngle;
        const double x = std::cos(lat) * std::cos(inertialLongitude), y = std::cos(lat) * std::sin(inertialLongitude), z = std::sin(lat);
        pitch = (float)std::asin(z);
        yaw = (float)std::atan2(x, -y);
        yawV = pitchV = 0.0f;
    };
    auto switchGlobeToMap = [&]() {
        const Vector3 direction{ std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw) };
        const double latitude = std::asin(direction.y) / DEG;
        double longitude = (std::atan2(-direction.z, direction.x) - earthRotationAngle) / DEG;
        longitude = std::fmod(std::fmod(longitude + 180.0, 360.0) + 360.0, 360.0) - 180.0;
        const double altitudeKm = std::max(0.05, (double)dist - earthR) * 1000.0;
        const double visibleWidthKm = 2.0 * altitudeKm * std::tan(22.5 * DEG) * screenWidth / screenHeight;
        const double spanDegrees = visibleWidthKm / (111.32 * std::max(0.1, std::cos(latitude * DEG)));
        setMode(Mode::Map);
        mapCentreU = (longitude + 180.0) / 360.0;
        mapCentreV = mercatorWorldY(latitude);
        mapWidthPx = std::min(std::max(screenWidth * 360.0 / std::max(spanDegrees, 0.01), (double)screenWidth), 256.0 * std::pow(2.0, 19));
        viewFade = 0.85f;
    };
    auto switchMapToGlobe = [&]() {
        double latitude, longitude;
        screenToLatLon(screenWidth * 0.5, screenHeight * 0.5, latitude, longitude);
        faceGlobeTowards(latitude, longitude);
        setMode(Mode::Globe);
        dist = earthR + 0.6f;
        distGoal = 14.0f;
        viewFade = 0.85f;
    };

    auto beginFrame = [&]() {
        frameSeconds = std::min(GetFrameTime(), 0.1f);
        ++frame;
        screenWidth = (float)GetScreenWidth(); screenHeight = (float)GetScreenHeight();
        satMode = (mode == Mode::Globe || mode == Mode::Map || mode == Mode::Sky);
        ui.begin();
    };

    auto reportBenchmark = [&]() -> bool {
        if (benchMode) { static int lastSec = -1; const int sec = (int)GetTime(); if (sec != lastSec) { lastSec = sec; std::printf("t=%ds fps=%d frametime=%.1fms hdDay=%d hdNight=%d clouds=%d\n", sec, GetFPS(), GetFrameTime() * 1000.0f, hdDay, hdNight, opts.showClouds); std::fflush(stdout); } if (sec == 22) opts.showClouds = false; if (sec == 26) { opts.showClouds = true; opts.showConst = false; } if (sec == 30) return true; }
        return false;
    };

    auto swapInHighResolutionEarth = [&]() {
        if (imagery.dayReady() && !hdDay) {
            hdDay = true;
            Texture2D t = LoadTexture(imagery.dayPath().c_str());
            if (t.id) {
                GenTextureMipmaps(&t);
                SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
                UnloadTexture(earthTex);
                earthTex = t;
                SetMaterialTexture(&earthModel.materials[0], MATERIAL_MAP_DIFFUSE, earthTex);
                TraceLog(LOG_INFO, "Earth: HD day map %dx%d", t.width, t.height);
            }
        }
        if (imagery.nightReady() && !hdNight) {
            hdNight = true;
            Texture2D t = LoadTexture(imagery.nightPath().c_str());
            if (t.id) {
                GenTextureMipmaps(&t);
                SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
                UnloadTexture(nightTex);
                nightTex = t;
                SetMaterialTexture(&earthModel.materials[0], MATERIAL_MAP_SPECULAR, nightTex);
                TraceLog(LOG_INFO, "Earth: city-lights map %dx%d", t.width, t.height);
            }
        }
        nightAmt = (hdNight && opts.showNightLights) ? 1.0f : 0.0f;

    };

    auto runScriptedTour = [&]() -> bool {
        if (!shotPrefix.empty()) {
            const double tnow = GetTime();
            if (frame < 60) SetMousePosition(screenWidth - 4.0f, screenHeight * 0.6f);
            auto centreOn = [&](double lat, double lon, double zoom) {
                mapCentreU = (lon + 180.0) / 360.0;
                const double sn = std::sin(lat * DEG); mapCentreV = 0.5 - std::log((1 + sn) / (1 - sn)) / (4 * PI_D);
                mapWidthPx = screenWidth * zoom;
            };
            if (frame == 10) { station = { true, 40.7, -74.0 }; ++stationVer; }
            if (frame == 30 && !entities.empty()) { selected = 0; follow = false; distGoal = dist = 16.0f; }
            if (frame == 100) { setMode(Mode::Solar); solarCam.goal = 40.0; }
            if (frame == 150) setMode(Mode::Galaxy);
            if (frame == 200) { galCam.tgoal = sunGal; galCam.goal = 150.0; selGal = galSunIdx; }
            if (frame == 262) { galCam.goal = 12000.0; galCam.tgoal = { 0, 0, 0 }; }
            if (frame == 300) {
                setMode(Mode::Globe); selected = -1; distGoal = dist = 16.0f;
                const Vector3 sr = Vector3Normalize(toRender(sunDirEci(simTime)));
                yaw = std::atan2(sr.x, sr.z) + 0.35f; pitch = std::asin(sr.y) * 0.5f + 0.12f; yawV = pitchV = 0;
            }
            if (frame == 297 && !(hdDay && hdNight)) {
                static double holdStart = -1.0;
                if (holdStart < 0) holdStart = tnow;
                if (tnow - holdStart < 180.0) --frame;
            }
            if (frame == 326) {
                const Vector3 sr = Vector3Normalize(toRender(sunDirEci(simTime)));
                yaw = std::atan2(sr.x, sr.z) + 3.14159f - 0.3f; pitch = -std::asin(sr.y) * 0.5f + 0.2f; yawV = pitchV = 0;
            }
            if (frame == 332) pendingShot = shotPrefix + "night.png";
            if (frame == 335) { setMode(Mode::Sky); skyAz = 0.0f; skyEl = 0.55f; skyFov = 105.0f; selected = -1; }
            if (frame == 352) pendingShot = shotPrefix + "skynorth.png";
            if (frame == 354) { skyAz = 3.14159f; skyEl = 0.6f; skyFov = 100.0f; }
            if (frame == 372) pendingShot = shotPrefix + "skysouth.png";
            if (frame == 374) { setMode(Mode::Map); mapWidthPx = screenWidth; mapCentreU = 0.5; mapCentreV = 0.4; selected = 0; }
            if (frame == 390) {
                centreOn(40.7, -74.0, 14.0);
                adsb.setEnabled(true); opts.radarOn = true; radarFeed.setEnabled(true); opts.cloudsOn = true; opts.windOn = true; wxProbe = true;
                selected = -1; scriptStep = 1; scriptT = tnow;
            }
            if (scriptStep == 1 && (tnow - scriptT > 26.0 || (tnow - scriptT > 9.0 && tiles.pending() == 0 && adsb.status().find("aircraft") != std::string::npos &&
                                                               wx.status().find("Open-Meteo") != std::string::npos))) { pendingShot = shotPrefix + "nyc.png"; scriptStep = 2; scriptT = tnow; }
            if (scriptStep == 2 && tnow - scriptT > 0.6) {
                adsb.setEnabled(false); opts.radarOn = false; opts.cloudsOn = false; opts.windOn = false; wxProbe = false;
                toggleShips(); centreOn(60.17, 24.94, 40.0); scriptStep = 3; scriptT = tnow;
            }
            if (scriptStep == 3 && (tnow - scriptT > 30.0 || (tnow - scriptT > 9.0 && tiles.pending() == 0 && ais.status().find("vessels") != std::string::npos))) { pendingShot = shotPrefix + "ships.png"; scriptStep = 4; scriptT = tnow; }
            if (scriptStep == 4 && tnow - scriptT > 0.6) { settingsOpen = true; scriptStep = 5; scriptT = tnow; }
            if (scriptStep == 5 && tnow - scriptT > 0.9) { pendingShot = shotPrefix + "settings.png"; scriptStep = 6; scriptT = tnow; }
            if (scriptStep == 6 && tnow - scriptT > 0.2) { settingsOpen = false; layersOpen = true; scriptStep = 7; scriptT = tnow; }
            if (scriptStep == 7 && tnow - scriptT > 0.9) { pendingShot = shotPrefix + "layers.png"; scriptStep = 8; scriptT = tnow; }
            if (scriptStep == 8 && tnow - scriptT > 0.2) { layersOpen = false; helpOpen = true; scriptStep = 9; scriptT = tnow; }
            if (scriptStep == 9 && tnow - scriptT > 0.9) { pendingShot = shotPrefix + "help.png"; scriptStep = 10; scriptT = tnow; }
            if (scriptStep == 10 && tnow - scriptT > 0.6) {
                helpOpen = false; setMode(Mode::Globe); faceGlobeTowards(40.7, -74.0); dist = distGoal = earthR + 0.9f; opts.showCities = true;
                scriptStep = 11; scriptT = tnow;
            }
            if (scriptStep == 11 && tnow - scriptT > 1.2) { pendingShot = shotPrefix + "globecities.png"; scriptStep = 12; scriptT = tnow; }
            if (scriptStep == 12 && tnow - scriptT > 0.3) { switchGlobeToMap(); scriptStep = 13; scriptT = tnow; }
            if (scriptStep == 13 && (tnow - scriptT > 14.0 || (tnow - scriptT > 5.0 && tiles.pending() == 0))) { pendingShot = shotPrefix + "handoff.png"; scriptStep = 14; scriptT = tnow; }
            if (scriptStep == 14 && tnow - scriptT > 0.4) { mapWidthPx = screenWidth * 1.0; mapCentreU = 0.45; mapCentreV = 0.4; scriptStep = 15; scriptT = tnow; }
            if (scriptStep == 15 && tnow - scriptT > 1.5) { pendingShot = shotPrefix + "mapcities.png"; scriptStep = 16; scriptT = tnow; }
            if (scriptStep == 16 && tnow - scriptT > 0.4) { switchMapToGlobe(); scriptStep = 17; scriptT = tnow; }
            if (scriptStep == 17 && tnow - scriptT > 1.5) { pendingShot = shotPrefix + "globeback.png"; scriptStep = 18; scriptT = tnow; }
            if (scriptStep == 18 && tnow - scriptT > 0.6) return true;
            if (frame == 90 || frame == 140 || frame == 190 || frame == 255 || frame == 296 || frame == 325 || frame == 365) pendingShot = shotPrefix + std::to_string(frame) + ".png";
        }

        return false;
    };

    auto handleKeyboardAndSearch = [&]() -> bool {
        if (timeOpen) {
            for (int ch; (ch = GetCharPressed()) > 0;)
                if ((std::isdigit(ch) || ch == '-' || ch == ':' || ch == ' ') && timeText.size() < 19) timeText.push_back((char)ch);
            if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !timeText.empty()) timeText.pop_back();
            if (IsKeyPressed(KEY_ESCAPE)) timeOpen = false;
            if (IsKeyPressed(KEY_ENTER)) {
                int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
                const int fieldsRead = std::sscanf(timeText.c_str(), "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second);
                if (fieldsRead >= 3 && month >= 1 && month <= 12 && day >= 1 && day <= 31) {
                    simTime = utcToUnix(year, month, day, hour, minute, second);
                    timeOpen = false;
                }
            }
        } else if (searching) {
            bool changed = false;
            for (int ch; (ch = GetCharPressed()) > 0;)
                if (ch >= 32 && ch < 127 && query.size() < 24) { query.push_back((char)ch); changed = true; }
            if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !query.empty()) { query.pop_back(); changed = true; }
            if (changed) recompute();
            const int shown = (int)std::min<size_t>(matches.size(), 8);
            if (IsKeyPressed(KEY_DOWN) && shown) matchSel = (matchSel + 1) % shown;
            if (IsKeyPressed(KEY_UP) && shown) matchSel = (matchSel + shown - 1) % shown;
            if (IsKeyPressed(KEY_ENTER) && !matches.empty()) { focus(matches[matchSel]); searching = false; }
            if (IsKeyPressed(KEY_ESCAPE)) searching = false;
        } else {
            if (IsKeyPressed(KEY_ESCAPE)) {
                if (follow || followBody || followGal) follow = followBody = followGal = false;
                else if (helpOpen || settingsOpen || layersOpen) { helpOpen = settingsOpen = layersOpen = false; }
                else if (selected >= 0 || selBody >= 0 || selGal >= 0 || !selectedAc.empty() || selectedShip) { selected = selBody = selGal = -1; selectedAc.clear(); selectedShip = 0; }
                else return true;
            }
            if (IsKeyPressed(KEY_SLASH)) { searching = true; query.clear(); matches.clear(); while (GetCharPressed() > 0) {} }
            if (IsKeyPressed(KEY_TAB)) {
                static const Mode order[] = { Mode::Globe, Mode::Map, Mode::Sky, Mode::Solar, Mode::Galaxy };
                int k = 0; for (int i = 0; i < 5; ++i) if (order[i] == mode) k = i;
                setMode(order[(k + 1) % 5]);
            }
            if (IsKeyPressed(KEY_E)) setMode(Mode::Globe);
            if (IsKeyPressed(KEY_M)) setMode(mode == Mode::Map ? Mode::Globe : Mode::Map);
            if (IsKeyPressed(KEY_P)) setMode(Mode::Solar);
            if (IsKeyPressed(KEY_W)) setMode(Mode::Galaxy);
            if (IsKeyPressed(KEY_S)) setMode(Mode::Sky);
            if (IsKeyPressed(KEY_Q)) opts.showConst = !opts.showConst;
            if (IsKeyPressed(KEY_I)) opts.showCities = !opts.showCities;
            if (IsKeyPressed(KEY_J)) { timeOpen = !timeOpen; if (timeOpen) timeText = fmtClock(simTime, false).substr(0, 16); }
            if (IsKeyPressed(KEY_SPACE)) paused = !paused;
            const double sgn = timeScale < 0 ? -1.0 : 1.0;
            if (IsKeyPressed(KEY_PERIOD)) timeScale = sgn * std::min(std::fabs(timeScale) * 2.0, 1.6e8);
            if (IsKeyPressed(KEY_COMMA)) timeScale = sgn * std::max(std::fabs(timeScale) / 2.0, 1.0);
            if (IsKeyPressed(KEY_T)) timeScale = -timeScale;
            if (IsKeyPressed(KEY_ONE)) timeScale = 1.0;
            if (IsKeyPressed(KEY_N)) { simTime = nowUnix(); timeScale = 1.0; }
            if (IsKeyPressed(KEY_L)) showLabels = !showLabels;
            if (IsKeyPressed(KEY_O)) showOrbits = !showOrbits;
            if (IsKeyPressed(KEY_C)) showConstellation = !showConstellation;
            if (IsKeyPressed(KEY_G)) showTrack = !showTrack;
            if (IsKeyPressed(KEY_A)) { adsb.setEnabled(!adsb.enabled()); if (adsb.enabled() && mode != Mode::Map) setMode(Mode::Map); }
            if (IsKeyPressed(KEY_B)) { toggleShips(); if (shipsOn && mode != Mode::Map) setMode(Mode::Map); }
            if (IsKeyPressed(KEY_Y)) opts.baseMap = (opts.baseMap + 1) % 4;
            if (IsKeyPressed(KEY_D)) { toggleRadar(); if (opts.radarOn && mode != Mode::Map) setMode(Mode::Map); }
            if (IsKeyPressed(KEY_K)) { opts.cloudsOn = !opts.cloudsOn; if (opts.cloudsOn && mode != Mode::Map) setMode(Mode::Map); }
            if (IsKeyPressed(KEY_Z)) { opts.windOn = !opts.windOn; if (opts.windOn && mode != Mode::Map) setMode(Mode::Map); }
            if (IsKeyPressed(KEY_X)) { wxProbe = !wxProbe; if (wxProbe && mode != Mode::Map) setMode(Mode::Map); }
            if (IsKeyPressed(KEY_U)) opts.units = (opts.units + 1) % 3;
            if (IsKeyPressed(KEY_F1)) helpOpen = !helpOpen;
            if (IsKeyPressed(KEY_F2)) { settingsOpen = !settingsOpen; layersOpen = false; }
            if (IsKeyPressed(KEY_F)) {
                if (mode == Mode::Globe && selected >= 0) { follow = !follow; if (follow) distGoal = std::min(distGoal, 3.0f); }
                else if (mode == Mode::Map && selected >= 0) follow = !follow;
                else if (mode == Mode::Sky && selected >= 0) follow = !follow;
                else if (mode == Mode::Solar && selBody >= 0) followBody = !followBody;
                else if (mode == Mode::Galaxy && selGal >= 0) { followGal = !followGal; if (followGal) galCam.tgoal = gal[selGal].pos; }
            }
            if (IsKeyPressed(KEY_H)) {
                if (mode == Mode::Map && station.set) {
                    mapCentreU = (station.lon + 180.0) / 360.0;
                    const double s = std::sin(station.lat * DEG); mapCentreV = 0.5 - std::log((1 + s) / (1 - s)) / (4 * PI_D);
                    mapWidthPx = std::max(mapWidthPx, (double)screenWidth * 40.0);
                } else if (mode == Mode::Galaxy) { followGal = false; galCam.tgoal = sunGal; galCam.goal = 60.0; }
                else if (mode == Mode::Solar) { followBody = false; solarCam.goal = 6.0; }
            }
            if (IsKeyPressed(KEY_R)) {
                yaw = 0.8f; pitch = 0.45f; distGoal = 22.0f; selected = -1; follow = false; yawV = pitchV = 0;
                solarCam.goal = 11.0; solarCam.pitch = 0.55f; followBody = false; selBody = -1;
                galCam.goal = 32000.0; galCam.tgoal = { 0, 0, 0 }; galCam.pitch = 0.95f; followGal = false; selGal = -1;
                if (mode == Mode::Map) { mapWidthPx = screenWidth; mapCentreU = 0.5; mapCentreV = 0.42; }
            }
        }
        return false;
    };

    auto handleMouseNavigation = [&]() {
        if (selected < 0) follow = false;
        if (selBody < 0) followBody = false;
        if (selGal < 0) followGal = false;

        dragging = (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) && !ui.pressedOnUi;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            dragDist = 0.0f; yawV = pitchV = solarCam.yawV = solarCam.pitchV = galCam.yawV = galCam.pitchV = 0.0f;
        }
        if (dragging) { const Vector2 d = GetMouseDelta(); dragDist += std::fabs(d.x) + std::fabs(d.y); }
        wheelDelta = (searching || ui.overUi) ? 0.0f : GetMouseWheelMove();
        if (mode == Mode::Globe) {
            applyOrbitDrag(yaw, pitch, yawV, pitchV);
            const float minDist = follow ? 0.03f : earthR + 0.4f;
            if (wheelDelta != 0.0f) distGoal *= std::pow(0.9f, wheelDelta);
            distGoal = Clamp(distGoal, minDist, 1200.0f);
            dist += (distGoal - dist) * (1.0f - std::exp(-14.0f * frameSeconds));
            if (wheelDelta > 0.0f && !follow && distGoal <= earthR + 0.55f) switchGlobeToMap();
        } else if (mode == Mode::Solar) {
            applyOrbitDrag(solarCam.yaw, solarCam.pitch, solarCam.yawV, solarCam.pitchV);
            if (wheelDelta != 0.0f) solarCam.goal *= std::pow(0.88, wheelDelta);
            solarCam.goal = Clamp(solarCam.goal, 0.0008, 600.0);
            solarCam.dist += (solarCam.goal - solarCam.dist) * (1.0 - std::exp(-12.0 * frameSeconds));
        } else if (mode == Mode::Galaxy) {
            applyOrbitDrag(galCam.yaw, galCam.pitch, galCam.yawV, galCam.pitchV);
            if (wheelDelta != 0.0f) galCam.goal *= std::pow(0.88, wheelDelta);
            galCam.goal = Clamp(galCam.goal, 2.0, 4.0e6);
            galCam.dist += (galCam.goal - galCam.dist) * (1.0 - std::exp(-12.0 * frameSeconds));
            {
                const Vector3 dv = Vector3Subtract(galCam.tgoal, galCam.target);
                const float len = Vector3Length(dv);
                if (len > 1e-6f) {
                    const float step = std::min(len, std::max(len * (1.0f - std::exp(-9.0f * frameSeconds)), (float)galCam.dist * 1.5f * frameSeconds));
                    galCam.target = Vector3Add(galCam.target, Vector3Scale(dv, step / len));
                }
            }
        } else if (mode == Mode::Sky) {
            if (dragging && !searching) {
                const Vector2 d = GetMouseDelta();
                const float sens = skyFov * (float)DEG / screenHeight;
                skyAz -= d.x * sens;
                skyEl = Clamp(skyEl + d.y * sens, -0.25f, 1.5707f);
                follow = false;
            }
            if (wheelDelta != 0.0f) skyFov = Clamp(skyFov * std::pow(0.9f, wheelDelta), 8.0f, 170.0f);
        } else {
            if (dragging && !searching) {
                const Vector2 d = GetMouseDelta();
                mapCentreU -= d.x / mapWidthPx; mapCentreV -= d.y / mapWidthPx;
                follow = false;
            }
            if (wheelDelta < 0.0f && mapWidthPx <= screenWidth * 1.001) {
                switchMapToGlobe();
                return;
            }
            if (wheelDelta != 0.0f) {
                const Vector2 m = GetMousePosition();
                const double uAt = mapCentreU + (m.x - screenWidth * 0.5) / mapWidthPx, vAt = mapCentreV + (m.y - screenHeight * 0.5) / mapWidthPx;
                mapWidthPx = std::min(std::max(mapWidthPx * std::pow(1.3, (double)wheelDelta), (double)screenWidth), 256.0 * std::pow(2.0, 19));
                mapCentreU = uAt - (m.x - screenWidth * 0.5) / mapWidthPx; mapCentreV = vAt - (m.y - screenHeight * 0.5) / mapWidthPx;
            }
        }

    };

    auto updateSatellitePositions = [&]() {
        if (satMode) {
            auto propagateRange = [&](size_t a, size_t b) {
                for (size_t i = a; i < b; ++i) {
                    Entity& e = entities[i];
                    Vec3d p = e.orbit.positionAt(simTime);
                    e.pos = toRender(p);
                    e.altKm = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z) - earth.radiusKm;
                }
            };
            const size_t n = entities.size();
            if (nThreads > 1 && n > 2000) {
                std::vector<std::thread> pool;
                const size_t chunk = (n + nThreads - 1) / nThreads;
                for (unsigned k = 1; k < nThreads; ++k) pool.emplace_back(propagateRange, std::min(n, k * chunk), std::min(n, (k + 1) * chunk));
                propagateRange(0, std::min(n, chunk));
                for (auto& t : pool) t.join();
            } else propagateRange(0, n);
        } else if (selected >= 0) {
            Vec3d p = entities[selected].orbit.positionAt(simTime);
            entities[selected].pos = toRender(p);
            entities[selected].altKm = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z) - earth.radiusKm;
        }

    };

    auto updateAstronomy = [&]() {
        daysSinceJ2000 = simTime / 86400.0 - 10957.5;
        earthRotationAngle = 2.0 * PI_D * std::fmod(0.7790572732640 + 1.00273781191135448 * daysSinceJ2000, 1.0);
        earthModel.transform = MatrixRotateY((float)earthRotationAngle);
        sunDirection = sunDirEci(simTime);
        sunSceneDirection = Vector3Normalize(toRender(sunDirection));
        moonScenePosition = toRender(moonPos(daysSinceJ2000));
        moonModel.transform = MatrixTranslate(moonScenePosition.x, moonScenePosition.y, moonScenePosition.z);
        sunDirectionEarthFixed = Vec3d{ sunDirection.x * std::cos(earthRotationAngle) + sunDirection.y * std::sin(earthRotationAngle), -sunDirection.x * std::sin(earthRotationAngle) + sunDirection.y * std::cos(earthRotationAngle), sunDirection.z };

    };

    auto updateCameraAndObserverFrame = [&]() {
        cam = Camera3D{};
        vp = MatrixIdentity();
        nearP = 0.01f;
        fwd = Vector3{ 0, 0, -1 };

        skyObs = station.set ? station : Station{ true, 0.0, 0.0 };
        skyE = { 1, 0, 0 }; skyN = { 0, 1, 0 }; skyU = { 0, 0, 1 }; obsPos = { 0, 0, 0 };
        sunAltDeg = 0.0; skyK = 1.0;
        fx = 0; fy = 1; fz = 0; rxv = 1; ryv = 0; ux = 0; uy = 0; uz = 1;
        if (mode == Mode::Sky) {
            const double lat = skyObs.lat * DEG, lamL = skyObs.lon * DEG + earthRotationAngle;
            skyE = { -std::sin(lamL), std::cos(lamL), 0.0 };
            skyN = { -std::sin(lat) * std::cos(lamL), -std::sin(lat) * std::sin(lamL), std::cos(lat) };
            skyU = { std::cos(lat) * std::cos(lamL), std::cos(lat) * std::sin(lamL), std::sin(lat) };
            obsPos = stationEci(skyObs, earthRotationAngle, earth.radiusKm);
            sunAltDeg = std::asin(std::max(-1.0, std::min(1.0, sunDirection.x * skyU.x + sunDirection.y * skyU.y + sunDirection.z * skyU.z))) / DEG;
            if (follow && selected >= 0) {
                const Vector3& q = entities[selected].pos;
                const Vec3d r{ q.x / kSceneUnitsPerKm - obsPos.x, -q.z / kSceneUnitsPerKm - obsPos.y, q.y / kSceneUnitsPerKm - obsPos.z };
                const double e = r.x * skyE.x + r.y * skyE.y + r.z * skyE.z, n = r.x * skyN.x + r.y * skyN.y + r.z * skyN.z, u = r.x * skyU.x + r.y * skyU.y + r.z * skyU.z;
                skyAz = (float)std::atan2(e, n); skyEl = (float)std::asin(u / std::sqrt(e * e + n * n + u * u));
            }
            skyAz = (float)std::fmod((double)skyAz, 2.0 * PI_D);
            skyK = (screenHeight * 0.5) / (2.0 * std::tan(skyFov * DEG / 4.0));
            const double cA = std::cos(skyAz), sA = std::sin(skyAz), cE = std::cos(skyEl), sE = std::sin(skyEl);
            fx = cE * sA; fy = cE * cA; fz = sE; rxv = cA; ryv = -sA; ux = -sE * sA; uy = -sE * cA; uz = cE;
            {
                const double T = (simTime / 86400.0 - 10957.5) / 36525.0, as = DEG / 3600.0;
                const double ze = (2306.2181 * T + 0.30188 * T * T + 0.017998 * T * T * T) * as, zz = (2306.2181 * T + 1.09468 * T * T + 0.018203 * T * T * T) * as,
                             th = (2004.3109 * T - 0.42665 * T * T - 0.041833 * T * T * T) * as;
                const double cz = std::cos(zz), sz = std::sin(zz), ct = std::cos(th), st = std::sin(th), cze = std::cos(ze), sze = std::sin(ze);
                skyP[0][0] = cze * ct * cz - sze * sz; skyP[0][1] = -sze * ct * cz - cze * sz; skyP[0][2] = -st * cz;
                skyP[1][0] = cze * ct * sz + sze * cz; skyP[1][1] = -sze * ct * sz + cze * cz; skyP[1][2] = -st * sz;
                skyP[2][0] = cze * st;                 skyP[2][1] = -sze * st;                 skyP[2][2] = ct;
            }
        }


        if (mode == Mode::Globe) {
            if (selected >= 0 && follow) lastGoal = entities[selected].pos;
            followBlend = Clamp(followBlend + (follow ? 1.0f : -1.0f) * frameSeconds * 2.5f, 0.0f, 1.0f);
            camTarget = Vector3Scale(lastGoal, followBlend * followBlend * (3.0f - 2.0f * followBlend));
            nearP = Clamp(dist * 0.003f, 0.002f, 0.05f);
            applyCamera(Vector3Add(camTarget, { dist * std::cos(pitch) * std::sin(yaw), dist * std::sin(pitch), dist * std::cos(pitch) * std::cos(yaw) }), camTarget, nearP, 3000.0);
            fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
            SetShaderValue(earthSh, earthSunDirLoc, &sunSceneDirection, SHADER_UNIFORM_VEC3);
            SetShaderValue(earthSh, earthViewPosLoc, &cam.position, SHADER_UNIFORM_VEC3);
            SetShaderValue(atmoSh, atmosphereSunDirLoc, &sunSceneDirection, SHADER_UNIFORM_VEC3);
            SetShaderValue(atmoSh, atmosphereViewPosLoc, &cam.position, SHADER_UNIFORM_VEC3);
            SetShaderValue(moonSh, moonSunDirLoc, &sunSceneDirection, SHADER_UNIFORM_VEC3);
            {
                const Vector3 so = Vector3Normalize(Vector3{ (float)sunDirectionEarthFixed.x, (float)sunDirectionEarthFixed.z, (float)-sunDirectionEarthFixed.y });
                const float cT = (float)std::fmod(simTime / 86400.0 * 0.9, 500.0), cA = opts.showClouds ? 1.0f : 0.0f, sp = opts.showSpecular ? 1.0f : 0.0f;
                SetShaderValue(earthSh, earthSunObjectLoc, &so, SHADER_UNIFORM_VEC3);
                SetShaderValue(earthSh, earthCloudTimeLoc, &cT, SHADER_UNIFORM_FLOAT);
                SetShaderValue(earthSh, earthCloudAmountLoc, &cA, SHADER_UNIFORM_FLOAT);
                SetShaderValue(earthSh, earthSpecularLoc, &sp, SHADER_UNIFORM_FLOAT);
                SetShaderValue(earthSh, earthNightLightsLoc, &nightAmt, SHADER_UNIFORM_FLOAT);
            }
        } else if (mode == Mode::Solar) {
            const double T = (simTime / 86400.0 - 10957.5) / 36525.0;
            for (int i = 0; i < NPLANETS; ++i) bodyPos[i] = planetPos(i, T);
            bodyPos[BODY_HALLEY] = smallBodyPos(halleyBody, T);
            bodyPos[BODY_SUN] = { 0, 0, 0 };
            Vector3 goal{ 0, 0, 0 };
            if (selBody >= 0 && followBody) goal = { (float)bodyPos[selBody].x, (float)bodyPos[selBody].z, (float)-bodyPos[selBody].y };
            {
                const Vector3 dv = Vector3Subtract(goal, solarCam.target);
                const float len = Vector3Length(dv);
                if (len > 1e-9f) {
                    const float step = std::min(len, std::max(len * (1.0f - std::exp(-8.0f * frameSeconds)), (float)solarCam.dist * 1.5f * frameSeconds));
                    solarCam.target = Vector3Add(solarCam.target, Vector3Scale(dv, step / len));
                }
            }
            const float d = (float)solarCam.dist;

            applyCamera({ d * std::cos(solarCam.pitch) * std::sin(solarCam.yaw), d * std::sin(solarCam.pitch), d * std::cos(solarCam.pitch) * std::cos(solarCam.yaw) }, { 0, 0, 0 }, d * 0.001, d * 1000.0 + 1000.0);
            fwd = Vector3Normalize(Vector3Negate(cam.position));
        } else if (mode == Mode::Galaxy) {
            const float d = (float)galCam.dist;
            applyCamera({ d * std::cos(galCam.pitch) * std::sin(galCam.yaw), d * std::sin(galCam.pitch), d * std::cos(galCam.pitch) * std::cos(galCam.yaw) }, { 0, 0, 0 }, d * 0.001, d * 1000.0 + 1.0e7);
            fwd = Vector3Normalize(Vector3Negate(cam.position));
        }

    };

    auto projectSatellitesAndFindHover = [&]() {
        hovered = -1; hovBody = -1; hovGal = -1; hoveredAc = -1; hoveredShip = -1;
        mouse = GetMousePosition();
        pickOk = !searching && !ui.overUi && (dragDist < 5.0f || !dragging);

        if (mode == Mode::Map) {
            const double halfV = screenHeight * 0.5 / mapWidthPx;
            if (halfV < 0.5) mapCentreV = std::min(std::max(mapCentreV, halfV), 1.0 - halfV); else mapCentreV = 0.5;
            mapCentreU -= std::floor(mapCentreU);
            if (follow && selected >= 0) {
                const Vector3& q = entities[selected].pos;
                const double rx = q.x, ry = -q.z, rz = q.y;
                const double lat = std::asin(rz / std::sqrt(rx * rx + ry * ry + rz * rz)) / DEG, lon = (std::atan2(ry, rx) - earthRotationAngle) / DEG;
                mapCentreU = (lon + 180.0) / 360.0; mapCentreV = mercatorWorldY(lat);
            }
        }
        if (satMode) {
            for (size_t i = 0; i < entities.size(); ++i) {
                const bool hide = ((entities[i].kind == Kind::Constellation && !showConstellation) || !orbitOn(entities[i])) && (int)i != selected;
                if (mode == Mode::Sky) {
                    const Vector3& q = entities[i].pos;
                    const Vec3d sp{ q.x / kSceneUnitsPerKm, -q.z / kSceneUnitsPerKm, q.y / kSceneUnitsPerKm }, r{ sp.x - obsPos.x, sp.y - obsPos.y, sp.z - obsPos.z };
                    double e, n, u;
                    toHorizontalFrame(r, e, n, u);
                    const double rng = std::sqrt(e * e + n * n + u * u);
                    bool ok = u > 0.0 && projectSkyDirection(e / rng, n / rng, u / rng, screenPositions[i]);
                    vis[i] = !hide && ok && screenPositions[i].x > -20 && screenPositions[i].x < screenWidth + 20 && screenPositions[i].y > -20 && screenPositions[i].y < screenHeight + 20;
                    skyLit[i] = sunlit(sp, sunDirection, earth.radiusKm) ? 1 : 0;
                } else if (mode == Mode::Map) {
                    const Vector3& q = entities[i].pos;
                    const double rx = q.x, ry = -q.z, rz = q.y;
                    const double lat = std::asin(rz / std::sqrt(rx * rx + ry * ry + rz * rz)) / DEG;
                    screenPositions[i] = latLonToScreen(lat, (std::atan2(ry, rx) - earthRotationAngle) / DEG);
                    vis[i] = !hide && screenPositions[i].x > -20 && screenPositions[i].x < screenWidth + 20 && screenPositions[i].y > -20 && screenPositions[i].y < screenHeight + 20;
                } else {
                    const bool front = Vector3DotProduct(Vector3Subtract(entities[i].pos, cam.position), fwd) > 0.0f;
                    vis[i] = !hide && front && !occludedByBody(cam.position, entities[i].pos, earthR);
                    screenPositions[i] = projectToScreen(entities[i].pos);
                }
            }
            if (pickOk) {
                float best = 14.0f * 14.0f;
                for (size_t i = 0; i < entities.size(); ++i) {
                    if (!vis[i]) continue;
                    float dx = screenPositions[i].x - mouse.x, dy = screenPositions[i].y - mouse.y, d2 = dx * dx + dy * dy;
                    if (d2 < best) { best = d2; hovered = (int)i; }
                }
            }
        }

    };

    auto refreshMapDataLayers = [&]() {
        steadyNow = steadySeconds();
        if (mode == Mode::Map) {
            double lat0, lon0, lat1, lon1, latC, lonC;
            screenToLatLon(0, 0, lat0, lon0); screenToLatLon(screenWidth, screenHeight, lat1, lon1); screenToLatLon(screenWidth * 0.5, screenHeight * 0.5, latC, lonC);
            if (adsb.enabled()) {
                AdsbRegion reg;
                reg.valid = true; reg.cLat = latC; reg.cLon = lonC;
                reg.minLat = std::min(lat0, lat1); reg.maxLat = std::max(lat0, lat1);
                reg.minLon = lon0 <= lon1 ? lon0 : -180.0; reg.maxLon = lon0 <= lon1 ? lon1 : 180.0;
                const double a1 = latC * DEG, a2 = lat0 * DEG, dl = (lon0 - lonC) * DEG;
                reg.halfDiagNm = std::acos(Clamp((float)(std::sin(a1) * std::sin(a2) + std::cos(a1) * std::cos(a2) * std::cos(dl)), -1.0f, 1.0f)) * 3440.065;
                adsb.setRegion(reg);
            } else adsb.setRegion(AdsbRegion{});
            WeatherRequest wr;
            wr.valid = wxProbe || opts.windOn;
            wr.cLat = latC; wr.cLon = lonC; wr.minLat = std::min(lat0, lat1); wr.maxLat = std::max(lat0, lat1);
            wr.minLon = lon0 <= lon1 ? lon0 : -180.0; wr.maxLon = lon0 <= lon1 ? lon1 : 180.0; wr.wantGrid = opts.windOn;
            wx.setEnabled(wr.valid);
            wx.setRequest(wr);

            float bestAc = 13.0f * 13.0f, bestShip = 13.0f * 13.0f;
            if (adsb.enabled()) {
                if (adsb.version() != acVer) { aircraftList = adsb.snapshot(); acVer = adsb.version(); }
                aircraftScreen.resize(aircraftList.size()); aircraftLatLon.resize(aircraftList.size());
                for (size_t i = 0; i < aircraftList.size(); ++i) {
                    const Aircraft& a = aircraftList[i];
                    double lat = a.lat, lon = a.lon;
                    const double age = std::min(steadyNow - a.stamp, 60.0);
                    if (!a.ground && a.gs > 1.0 && age > 0.0) {
                        const double nm = a.gs * age / 3600.0, trk = a.track * DEG;
                        lat += nm * std::cos(trk) / 60.0;
                        lon += nm * std::sin(trk) / (60.0 * std::max(0.05, std::cos(lat * DEG)));
                    }
                    aircraftLatLon[i] = { lat, lon };
                    aircraftScreen[i] = latLonToScreen(lat, lon);
                    if (pickOk && passesAircraftFilter(a)) {
                        const float dx = aircraftScreen[i].x - mouse.x, dy = aircraftScreen[i].y - mouse.y, d2 = dx * dx + dy * dy;
                        if (d2 < bestAc) { bestAc = d2; hoveredAc = (int)i; }
                    }
                }
            }
            if (shipsOn) {
                if (ais.version() != shipVer) { ships = ais.snapshot(); shipVer = ais.version(); }
                shipScreen.resize(ships.size()); shipLatLon.resize(ships.size());
                for (size_t i = 0; i < ships.size(); ++i) {
                    const Vessel& v = ships[i];
                    double lat = v.lat, lon = v.lon;
                    const double age = std::min(steadyNow - v.stamp, 600.0);
                    if (v.sog >= 0.5 && age > 0.0) {
                        const double nm = v.sog * age / 3600.0, trk = v.cog * DEG;
                        lat += nm * std::cos(trk) / 60.0;
                        lon += nm * std::sin(trk) / (60.0 * std::max(0.05, std::cos(lat * DEG)));
                    }
                    shipLatLon[i] = { lat, lon };
                    shipScreen[i] = latLonToScreen(lat, lon);
                    if (pickOk && passesShipFilter(v)) {
                        const float dx = shipScreen[i].x - mouse.x, dy = shipScreen[i].y - mouse.y, d2 = dx * dx + dy * dy;
                        if (d2 < bestShip) { bestShip = d2; hoveredShip = (int)i; }
                    }
                }
            }
            {
                auto d2 = [&](Vector2 a) { return (a.x - mouse.x) * (a.x - mouse.x) + (a.y - mouse.y) * (a.y - mouse.y); };
                float dSat = hovered >= 0 ? d2(screenPositions[hovered]) : 1e9f, dAc = hoveredAc >= 0 ? bestAc : 1e9f, dSh = hoveredShip >= 0 ? bestShip : 1e9f;
                const float m = std::min(dSat, std::min(dAc, dSh));
                if (dSat != m) hovered = -1;
                if (dAc != m) hoveredAc = -1;
                if (dSh != m) hoveredShip = -1;
            }
        } else { adsb.setRegion(AdsbRegion{}); wx.setEnabled(false); wx.setRequest(WeatherRequest{}); }

    };

    auto pickSolarAndGalaxyObjects = [&]() {
        bodyScr.clear();
        if (mode == Mode::Solar) {
            bodyScr.resize(bodyPos.size());
            float best = 15.0f * 15.0f;
            for (size_t i = 0; i < bodyPos.size(); ++i) {
                float w; bodyScr[i] = projectToScreen(solarRelativePosition(bodyPos[i]), &w);
                if (w <= 0 || !pickOk) continue;
                const float dx = bodyScr[i].x - mouse.x, dy = bodyScr[i].y - mouse.y, d2 = dx * dx + dy * dy;
                if (d2 < best) { best = d2; hovBody = (int)i; }
            }
        }

        galScr.clear();
        if (mode == Mode::Galaxy) {
            galScr.resize(gal.size());
            float best = 13.0f * 13.0f;
            for (size_t i = 0; i < gal.size(); ++i) {
                float w; galScr[i] = projectToScreen(Vector3Subtract(gal[i].pos, galCam.target), &w);
                if (w <= 0 || !pickOk) continue;
                if (gal[i].kind == 0 && galCam.dist > 5000.0) continue;
                const float dx = galScr[i].x - mouse.x, dy = galScr[i].y - mouse.y, d2 = dx * dx + dy * dy;
                if (d2 < best) { best = d2; hovGal = (int)i; }
            }
        }

    };

    auto handleClicks = [&]() {
        if (!searching && !ui.pressedOnUi && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && dragDist < 5.0f) {
            if (satMode) {
                selected = hovered;
                selectedAc = (mode == Mode::Map && hoveredAc >= 0) ? aircraftList[hoveredAc].hex : std::string();
                selectedShip = (mode == Mode::Map && hoveredShip >= 0) ? ships[hoveredShip].mmsi : 0;
            }
            else if (mode == Mode::Solar) selBody = hovBody;
            else selGal = hovGal;
        }
        if (!searching && !ui.pressedOnUi && !ui.overUi && IsMouseButtonReleased(MOUSE_BUTTON_RIGHT) && dragDist < 5.0f && satMode && mode != Mode::Sky) {
            if (mode == Mode::Map) {
                double la, lo; screenToLatLon(mouse.x, mouse.y, la, lo);
                setStation(la, lo);
            } else {
                const Ray ray = GetScreenToWorldRay(mouse, cam);
                const float b = Vector3DotProduct(ray.position, ray.direction), c = Vector3DotProduct(ray.position, ray.position) - earthR * earthR;
                const float disc = b * b - c;
                if (disc >= 0.0f) {
                    const Vector3 hit = Vector3Add(ray.position, Vector3Scale(ray.direction, -b - std::sqrt(disc)));
                    const double rx = hit.x, ry = -hit.z, rz = hit.y;
                    double lon = std::fmod((std::atan2(ry, rx) - earthRotationAngle) / DEG, 360.0);
                    if (lon > 180.0) lon -= 360.0; else if (lon < -180.0) lon += 360.0;
                    setStation(std::asin(rz / earthR) / DEG, lon);
                }
            }
        }

    };

    auto updatePassPredictions = [&]() {
        if (selected >= 0 && station.set && (passSel != selected || passVer != stationVer || std::fabs(simTime - passT) > 1800.0)) {
            passes = computePasses(entities[selected], station, simTime, earth.radiusKm);
            passSel = selected; passVer = stationVer; passT = simTime;
        }

    };

    auto drawGlobeScene = [&]() {
        drawStarBackdrop(cam.position);
        if (Vector3DotProduct(sunSceneDirection, fwd) > 0.0f) {
            Vector2 p = projectToScreen(Vector3Add(cam.position, Vector3Scale(sunSceneDirection, 100.0f)));
            DrawCircleGradient((int)p.x, (int)p.y, 90, Fade({ 255, 235, 190, 255 }, 0.55f), BLANK);
            DrawCircleGradient((int)p.x, (int)p.y, 26, Fade({ 255, 250, 235, 255 }, 0.9f), BLANK);
        }
        rlSetClipPlanes(nearP, 3000.0);
        BeginMode3D(cam);
        DrawModel(earthModel, { 0, 0, 0 }, 1.0f, WHITE);
        DrawModel(moonModel, { 0, 0, 0 }, 1.0f, WHITE);
        for (size_t i = 0; i < entities.size(); ++i) {
            const Entity& e = entities[i];
            bool sel = (int)i == selected;
            if (!sel && !(showOrbits && e.labeled)) continue;
            Color c = sel ? Color{ 255, 255, 255, 255 } : Color{ e.color.r, e.color.g, e.color.b, 110 };
            drawOrbitPath(e, simTime, c);
        }
        if (selected >= 0) {
            const Entity& e = entities[selected];
            if (showTrack) drawGroundTrack(e, simTime, earth.radiusKm, { 255, 220, 90, 255 });
            DrawLine3D(e.pos, Vector3Scale(Vector3Normalize(e.pos), earthR), Fade(WHITE, 0.5f));
        }
        const float base = std::max(0.001f, dist * 0.0045f) * opts.markerScale;
        for (size_t i = 0; i < entities.size(); ++i) {
            const Entity& e = entities[i];
            if (!vis[i] && (int)i != selected) continue;
            if (e.dot && (int)i != selected && (int)i != hovered) continue;
            float f = e.kind == Kind::Ship ? 1.5f : e.kind == Kind::Station ? 1.3f : e.kind == Kind::Constellation ? 0.55f : 1.0f;
            if ((int)i == selected || (int)i == hovered) f *= 1.6f;
            float s = base * f;
            if (e.kind == Kind::Ship) DrawSphere(e.pos, s * 0.6f, e.color);
            else DrawCube(e.pos, s, s, s, e.color);
        }
        if (station.set) {
            const Vector3 gp = toRender(stationEci(station, earthRotationAngle, earth.radiusKm * 1.002));
            DrawSphere(gp, std::max(0.004f, dist * 0.0028f), { 90, 255, 140, 255 });
            if (selected >= 0 && lookAngles(station, Vec3d{ entities[selected].pos.x / kSceneUnitsPerKm, -entities[selected].pos.z / kSceneUnitsPerKm, entities[selected].pos.y / kSceneUnitsPerKm }, earthRotationAngle, earth.radiusKm).el > 0.0)
                DrawLine3D(gp, entities[selected].pos, { 90, 255, 140, 255 });
        }
        BeginBlendMode(BLEND_ADDITIVE);
        DrawModel(atmoModel, { 0, 0, 0 }, 1.0f, WHITE);
        EndBlendMode();
        EndMode3D();
    };

    auto drawMapScene = [&]() {

        const Vector3 sunE{ (float)sunDirectionEarthFixed.x, (float)sunDirectionEarthFixed.y, (float)sunDirectionEarthFixed.z };
        const float centerV[2] = { (float)mapCentreU, (float)mapCentreV }, spanV[2] = { (float)(screenWidth / mapWidthPx), (float)(screenHeight / mapWidthPx) };
        SetShaderValue(mapSh, mapSunDirLoc, &sunE, SHADER_UNIFORM_VEC3);  // must precede BeginShaderMode (it unbinds the shader)
        SetShaderValue(mapSh, mapCenterLoc, centerV, SHADER_UNIFORM_VEC2);
        SetShaderValue(mapSh, mapSpanLoc, spanV, SHADER_UNIFORM_VEC2);
        SetShaderValue(mapSh, mapNightLightsLoc, &nightAmt, SHADER_UNIFORM_FLOAT);
        SetShaderValueTexture(mapSh, mapNightTextureLoc, nightTex);
        BeginShaderMode(mapSh);
        DrawTexturePro(earthTex, { 0, 0, (float)earthTex.width, (float)earthTex.height }, { 0, 0, screenWidth, screenHeight }, { 0, 0 }, 0.0f, WHITE);
        EndShaderMode();

        const double zoomF = mapWidthPx / screenWidth;
        const double unixNow = nowUnix();
        double cLat, cLon;
        screenToLatLon(screenWidth * 0.5, screenHeight * 0.5, cLat, cLon);
        if (!tilesStarted && (opts.baseMap != 0 || opts.radarOn || opts.cloudsOn)) { tiles.start(2); tilesStarted = true; }  // OSM policy: at most 2 connections
        if (steadyNow - layerCheck > 20.0) {
            layerCheck = steadyNow;
            const std::string d = formatTime(unixNow - 86400.0, "%Y-%m-%d");
            modisLayer = tiles.addLayer("modis/" + d, "https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/MODIS_Terra_CorrectedReflectance_TrueColor/default/" + d + "/GoogleMapsCompatible_Level9/{z}/{y}/{x}.jpg", ".jpg");
            const char* satId = (cLon >= 70 || cLon < -165) ? "Himawari_AHI_Band13_Clean_Infrared" : cLon < -112 ? "GOES-West_ABI_Band13_Clean_Infrared" : "GOES-East_ABI_Band13_Clean_Infrared";
            irLayer = tiles.addLayer(std::string("ir/") + satId + "/" + gibsTimeSlot(unixNow, 40, true),
                                     std::string("https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/") + satId + "/default/" + gibsTimeSlot(unixNow, 40, false) + "/GoogleMapsCompatible_Level6/{z}/{y}/{x}.png", ".png");
        }
        if (radarFeed.version() != radarVer) {
            radarVer = radarFeed.version();
            radarFrames = radarFeed.frames();
            const std::string host = radarFeed.host();
            radarLayers.clear();
            for (const RadarFrame& f : radarFrames)
                radarLayers.push_back(tiles.addLayer("radar/" + f.path.substr(f.path.find_last_of('/') + 1), host + f.path + "/256/{z}/{x}/{y}/2/1_1.png", ".png"));
            radarIdx = std::max(0, (int)radarLayers.size() - 1);
        }

        auto drawTiles = [&](int layer, int maxZ, float alphaMul, int dark, bool paint) -> bool {
            if (layer < 0 || alphaMul <= 0.002f) return false;
            bool allLoaded = true;
            const int zt = std::clamp((int)std::ceil(std::log2(mapWidthPx / 256.0)), 0, maxZ);
            const int n = 1 << zt;
            const double ts = mapWidthPx / n;
            const double u0 = mapCentreU - screenWidth * 0.5 / mapWidthPx, u1 = mapCentreU + screenWidth * 0.5 / mapWidthPx, v0 = mapCentreV - screenHeight * 0.5 / mapWidthPx, v1 = mapCentreV + screenHeight * 0.5 / mapWidthPx;
            const int tx0 = (int)std::floor(u0 * n), tx1 = (int)std::floor(u1 * n);
            const int ty0 = std::max(0, (int)std::floor(v0 * n)), ty1 = std::min(n - 1, (int)std::floor(v1 * n));
            if (paint) { const float df = (float)dark; SetShaderValue(tileSh, tileDarkLoc, &df, SHADER_UNIFORM_FLOAT); BeginShaderMode(tileSh); }
            int count = 0;
            for (int ty = ty0; ty <= ty1 && count < 300; ++ty)
                for (int tx = tx0; tx <= tx1 && count < 300; ++tx, ++count) {
                    const int wx2 = ((tx % n) + n) % n;
                    float fa = 1.0f;
                    const Texture2D* t = tiles.get(layer, zt, wx2, ty, true, &fa);
                    if (!t) allLoaded = false;
                    if (!paint) continue;
                    Rectangle src{ 0, 0, 0, 0 };
                    bool parent = false;
                    if (t) src = { 0, 0, (float)t->width, (float)t->height };
                    for (int up = 1; !t && up <= 4 && zt - up >= 0; ++up) {
                        const Texture2D* pt = tiles.get(layer, zt - up, wx2 >> up, ty >> up, false);
                        if (pt) { const float sz = (float)pt->width / (1 << up); src = { (float)(wx2 & ((1 << up) - 1)) * sz, (float)(ty & ((1 << up) - 1)) * sz, sz, sz }; t = pt; parent = true; }
                    }
                    if (!t) continue;
                    const float px = (float)(screenWidth * 0.5 + (tx / (double)n - mapCentreU) * mapWidthPx), py = (float)(screenHeight * 0.5 + (ty / (double)n - mapCentreV) * mapWidthPx);
                    DrawTexturePro(*t, src, { px, py, (float)ts + 0.6f, (float)ts + 0.6f }, { 0, 0 }, 0.0f, Fade(WHITE, alphaMul * (parent ? 1.0f : fa)));
                }
            if (paint) EndShaderMode();
            return allLoaded;
        };

        if (opts.baseMap == 3) drawTiles(modisLayer, 9, Clamp((float)((zoomF - 1.05) / 0.8), 0.0f, 1.0f), 0, true);
        else if (opts.baseMap == 1 || opts.baseMap == 2) drawTiles(osmLayer, 19, Clamp((float)((zoomF - 1.6) / 1.6), 0.0f, 1.0f), opts.baseMap == 1 ? 1 : 0, true);
        if (opts.cloudsOn) drawTiles(irLayer, 6, 0.62f, 0, true);
        if (opts.radarOn && !radarLayers.empty()) {

            std::vector<char> ready(radarLayers.size(), 0);
            for (size_t i = 0; i < radarLayers.size(); ++i) ready[i] = drawTiles(radarLayers[i], 7, opts.radarOpacity, 0, false) ? 1 : 0;

            if (radarPlay && steadyNow >= radarNext) {
                for (int k = 1; k <= (int)radarLayers.size(); ++k) {
                    const int j = (radarIdx + k) % (int)radarLayers.size();
                    if (ready[j]) { radarIdx = j; break; }
                }
                radarNext = steadyNow + (radarIdx == (int)radarLayers.size() - 1 ? 2.2 : 0.55);
            }
            radarIdx = std::clamp(radarIdx, 0, (int)radarLayers.size() - 1);
            int show = radarIdx;
            if (!ready[show]) {
                int found = -1;
                for (int j = show; j >= 0 && found < 0; --j) if (ready[j]) found = j;
                for (int j = (int)radarLayers.size() - 1; j > show && found < 0; --j) if (ready[j]) found = j;
                if (found >= 0) show = found;
            }
            radarShown = show;
            drawTiles(radarLayers[show], 7, opts.radarOpacity, 0, true);
        }
        tiles.evict(1100);

        DrawRectangleGradientV(0, 0, (int)screenWidth, 120, Fade(BLACK, 0.55f), BLANK);
        DrawRectangleGradientV(0, (int)screenHeight - 130, (int)screenWidth, 130, BLANK, Fade(BLACK, 0.60f));
        DrawRectangleGradientH(0, 0, 90, (int)screenHeight, Fade(BLACK, 0.35f), BLANK);
        DrawRectangleGradientH((int)screenWidth - 90, 0, 90, (int)screenHeight, BLANK, Fade(BLACK, 0.35f));

        {
            const float ga = opts.baseMap == 0 ? 1.0f : Clamp(1.0f - (float)((zoomF - 2.0) / 4.0), 0.0f, 1.0f);
            static const double steps[] = { 60, 30, 15, 10, 5, 2, 1, 0.5, 0.2, 0.1, 0.05, 0.02, 0.01 };
            double step = 60;
            for (double st : steps) if (st / 360.0 * mapWidthPx >= 90.0) step = st; else break;
            double la0, lo0, la1, lo1;
            screenToLatLon(0, 0, la0, lo0); screenToLatLon(screenWidth, screenHeight, la1, lo1);
            if (lo1 < lo0) { lo0 = -180; lo1 = 180; }
            if (ga > 0.01f) {
                for (double lo = std::ceil(lo0 / step) * step; lo <= lo1; lo += step) {
                    const Vector2 a = latLonToScreen(0, lo);
                    DrawLineV({ a.x, 0 }, { a.x, screenHeight }, Fade(WHITE, (std::fabs(lo) < 1e-9 ? 0.22f : 0.07f) * ga));
                    if (step >= 0.1) UiText(TextFormat(step < 1 ? "%.1f" : "%.0f", lo), (int)a.x + 3, (int)screenHeight - 50, 11, Fade(WHITE, 0.45f * ga));
                }
                for (double la = std::ceil(std::min(la0, la1) / step) * step; la <= std::max(la0, la1); la += step) {
                    const Vector2 a = latLonToScreen(la, 0);
                    DrawLineV({ 0, a.y }, { screenWidth, a.y }, Fade(WHITE, (std::fabs(la) < 1e-9 ? 0.22f : 0.07f) * ga));
                    if (step >= 0.1) UiText(TextFormat(step < 1 ? "%.1f" : "%.0f", la), 6, (int)a.y - 12, 11, Fade(WHITE, 0.45f * ga));
                }
            }
        }

        if (opts.windOn) {
            for (const WeatherPoint& wp : wx.grid()) {
                const Vector2 c = latLonToScreen(wp.lat, wp.lon);
                if (c.x < -40 || c.x > screenWidth + 40 || c.y < -40 || c.y > screenHeight + 40) continue;
                const float to = (float)((wp.windDir + 180.0) * DEG), kt = (float)wp.windKt;
                const Vector2 dir{ std::sin(to), -std::cos(to) }, perp{ -dir.y, dir.x };
                const float len = 14.0f + std::min(kt, 55.0f) * 0.85f;
                const Color col = kt < 10 ? Color{ 130, 200, 255, 255 } : kt < 20 ? Color{ 90, 235, 190, 255 } : kt < 30 ? Color{ 235, 235, 90, 255 } : kt < 40 ? Color{ 255, 170, 60, 255 } : Color{ 255, 85, 85, 255 };
                const Vector2 a{ c.x - dir.x * len * 0.5f, c.y - dir.y * len * 0.5f }, b{ c.x + dir.x * len * 0.5f, c.y + dir.y * len * 0.5f };
                DrawLineEx({ a.x + 1, a.y + 1 }, { b.x + 1, b.y + 1 }, 3.2f, Fade(BLACK, 0.5f));
                DrawLineEx(a, b, 2.4f, col);
                DrawTriangle(b, { b.x - dir.x * 8 - perp.x * 4.5f, b.y - dir.y * 8 - perp.y * 4.5f }, { b.x - dir.x * 8 + perp.x * 4.5f, b.y - dir.y * 8 + perp.y * 4.5f }, col);
                UiText(TextFormat("%.0f kt", kt), (int)c.x - 14, (int)c.y + 8, 11, Fade(WHITE, 0.85f));
            }
        }

        {
            const Vector2 sp = latLonToScreen(std::asin(sunDirection.z) / DEG, (std::atan2(sunDirection.y, sunDirection.x) - earthRotationAngle) / DEG);
            DrawCircleGradient((int)sp.x, (int)sp.y, 26, Fade({ 255, 235, 150, 255 }, 0.75f), BLANK);
            DrawCircle((int)sp.x, (int)sp.y, 4, { 255, 245, 200, 255 });
            const double mrx = moonScenePosition.x, mry = -moonScenePosition.z, mrz = moonScenePosition.y;
            const Vector2 mp = latLonToScreen(std::asin(mrz / std::sqrt(mrx * mrx + mry * mry + mrz * mrz)) / DEG, (std::atan2(mry, mrx) - earthRotationAngle) / DEG);
            DrawCircle((int)mp.x, (int)mp.y, 4, LIGHTGRAY);
            UiText("Moon", (int)mp.x + 8, (int)mp.y - 6, 12, LIGHTGRAY);
        }
        if (selected >= 0 && showTrack) {
            const Entity& e = entities[selected];
            const double span = std::min(e.orbit.period(), 1.5 * 86400.0);
            Vector2 prev{}; bool have = false;
            for (int i = -40; i <= 240; ++i) {
                const double t = simTime + span * i / 240.0;
                const Vec3d p = e.orbit.positionAt(t);
                const Vector2 cur = latLonToScreen(std::atan2(p.z, std::hypot(p.x, p.y)) / DEG, (std::atan2(p.y, p.x) - eraAt(t)) / DEG);
                if (have && std::fabs(cur.x - prev.x) < screenWidth * 0.5f) DrawLineEx(prev, cur, i < 0 ? 1.3f : 2.0f, i < 0 ? Fade({ 255, 220, 90, 255 }, 0.35f) : Color{ 255, 220, 90, 255 });
                prev = cur; have = true;
            }
        }
        if (station.set) {
            const Vector2 g = latLonToScreen(station.lat, station.lon);
            DrawCircleGradient((int)g.x, (int)g.y, 16, Fade({ 90, 255, 140, 255 }, 0.35f), BLANK);
            DrawPoly(g, 4, 7.0f, 45.0f, { 90, 255, 140, 255 });
            UiText("Ground station", (int)g.x + 11, (int)g.y - 7, 12, { 90, 255, 140, 255 });
        }
        for (size_t i = 0; i < entities.size(); ++i) {
            const Entity& e = entities[i];
            if (!vis[i] || e.dot) continue;
            const float sz = 3.0f * opts.markerScale;
            DrawCircleGradient((int)screenPositions[i].x, (int)screenPositions[i].y, 11.0f * opts.markerScale, Fade(e.color, 0.28f), BLANK);
            DrawRectangle((int)(screenPositions[i].x - sz), (int)(screenPositions[i].y - sz), (int)(sz * 2 + 1), (int)(sz * 2 + 1), e.color);
        }

        std::unordered_set<long long> labelCells;
        auto labelFree = [&](float x, float y) { return labelCells.insert((long long)((int)(x / 84.0f)) * 100003LL + (int)(y / 15.0f)).second; };

        const float cityFade = opts.baseMap == 0 ? 1.0f : Clamp((float)((10.0 - zoomF) / 4.0), 0.0f, 1.0f);
        if (opts.showCities && cityFade > 0.01f) {
            const double minimumImportance = 14.0e6 / std::pow(zoomF, 1.75);
            for (const City& city : cities) {
                if (city.importance < minimumImportance) continue;
                const Vector2 p = latLonToScreen(city.latitude, city.longitude);
                if (p.x < -40 || p.x > screenWidth + 40 || p.y < -20 || p.y > screenHeight + 20) continue;
                const float radius = (float)Clamp(2.2 + std::log10(std::max(1.0, city.population / 1.0e5)) * 1.6, 2.2, 5.5) * opts.markerScale;
                const Color fill = city.capital ? Color{ 255, 214, 110, 255 } : Color{ 236, 241, 250, 255 };
                DrawCircleV(p, radius + 1.6f, Fade(BLACK, 0.6f * cityFade));
                DrawCircleV(p, radius, Fade(fill, cityFade));
                if (city.capital) DrawCircleLines((int)p.x, (int)p.y, radius + 3.0f, Fade(fill, 0.7f * cityFade));
                if (showLabels && labelFree(p.x + 9, p.y)) {
                    UiText(city.name.c_str(), (int)p.x + 10, (int)p.y - 6, 13, Fade(BLACK, 0.8f * cityFade));
                    UiText(city.name.c_str(), (int)p.x + 9, (int)p.y - 7, 13, Fade(city.capital ? Color{ 255, 232, 170, 255 } : WHITE, 0.95f * cityFade));
                }
            }
        }

        if (adsb.enabled()) {
            const float sz = (float)Clamp(4.8 + std::log2(std::max(1.0, zoomF)) * 0.45, 4.8, 10.5) * opts.markerScale;
            const bool trails = opts.acTrails && zoomF > 6.0;
            for (size_t i = 0; i < aircraftList.size(); ++i) {
                const Aircraft& a = aircraftList[i];
                if (!passesAircraftFilter(a)) continue;
                const Vector2 p = aircraftScreen[i];
                if (p.x < -30 || p.x > screenWidth + 30 || p.y < -30 || p.y > screenHeight + 30) continue;
                const bool sel = a.hex == selectedAc, hov = (int)i == hoveredAc;
                const Color c = a.ground ? Color{ 140, 140, 150, 255 } : (a.hasAlt ? altColor(a.altFt) : Color{ 200, 200, 200, 255 });
                if ((trails || sel) && a.trail.size() > 1) {
                    Vector2 prev = latLonToScreen(a.trail[0].first, a.trail[0].second);
                    for (size_t k = 1; k < a.trail.size(); ++k) {
                        const Vector2 cur = latLonToScreen(a.trail[k].first, a.trail[k].second);
                        if (std::fabs(cur.x - prev.x) < screenWidth * 0.4f) DrawLineEx(prev, cur, 1.6f, Fade(c, 0.12f + 0.55f * k / a.trail.size()));
                        prev = cur;
                    }
                    if (std::fabs(p.x - prev.x) < screenWidth * 0.4f) DrawLineEx(prev, p, 1.6f, Fade(c, 0.7f));
                }
                const float s = sz * (sel || hov ? 1.5f : 1.0f);
                const float trk = (float)(a.track * DEG);
                const Vector2 dir{ std::sin(trk), -std::cos(trk) }, perp{ -dir.y, dir.x };
                auto tri = [&](float k, Color col) {
                    DrawTriangle({ p.x + dir.x * s * k, p.y + dir.y * s * k }, { p.x - dir.x * s * 0.7f * k - perp.x * s * 0.6f * k, p.y - dir.y * s * 0.7f * k - perp.y * s * 0.6f * k },
                                 { p.x - dir.x * s * 0.7f * k + perp.x * s * 0.6f * k, p.y - dir.y * s * 0.7f * k + perp.y * s * 0.6f * k }, col);
                };
                if (a.ground) { DrawCircleV(p, s * 0.6f, Fade(BLACK, 0.5f)); DrawCircleV(p, s * 0.45f, c); }
                else { tri(1.32f, Fade(BLACK, 0.55f)); tri(1.0f, c); }
                if (sel) DrawCircleLines((int)p.x, (int)p.y, s + 6, WHITE);
                if ((showLabels && zoomF > 14.0 && !a.call.empty() && labelFree(p.x, p.y)) || sel || hov)
                    UiText(a.call.empty() ? a.hex.c_str() : a.call.c_str(), (int)p.x + (int)s + 5, (int)p.y - 6, 12, sel || hov ? WHITE : Fade(c, 0.95f));
            }
        }

        if (shipsOn) {
            const double mPerPx = 40075016.686 / mapWidthPx;
            for (size_t i = 0; i < ships.size(); ++i) {
                const Vessel& v = ships[i];
                if (!passesShipFilter(v)) continue;
                const Vector2 p = shipScreen[i];
                if (p.x < -60 || p.x > screenWidth + 60 || p.y < -60 || p.y > screenHeight + 60) continue;
                const int cat = ais::category(v.shipType);
                const Color c = v.shipType == 0 ? Color{ 170, 176, 188, 255 } : cat == 0 ? Color{ 90, 214, 120, 255 } : cat == 1 ? Color{ 242, 92, 82, 255 } : cat == 2 ? Color{ 92, 172, 255, 255 } : cat == 3 ? Color{ 255, 172, 62, 255 } : Color{ 200, 172, 255, 255 };
                const bool sel = v.mmsi == selectedShip, hov = (int)i == hoveredShip;
                if (((opts.acTrails && zoomF > 6.0) || sel) && v.trail.size() > 1) {
                    Vector2 prev = latLonToScreen(v.trail[0].first, v.trail[0].second);
                    for (size_t k = 1; k < v.trail.size(); ++k) {
                        const Vector2 cur = latLonToScreen(v.trail[k].first, v.trail[k].second);
                        if (std::fabs(cur.x - prev.x) < screenWidth * 0.4f) DrawLineEx(prev, cur, 1.5f, Fade(c, 0.10f + 0.5f * k / v.trail.size()));
                        prev = cur;
                    }
                    if (std::fabs(p.x - prev.x) < screenWidth * 0.4f) DrawLineEx(prev, p, 1.5f, Fade(c, 0.65f));
                }
                float s = (float)Clamp(5.5 + std::log2(std::max(1.0, zoomF)) * 0.35, 5.5, 11.0) * opts.markerScale;
                if (v.len > 0) {
                    const double lenPx = v.len / (mPerPx / std::max(0.05, 1.0 / std::cos(shipLatLon[i].first * DEG)));
                    if (lenPx > s * 2.2) s = (float)std::min(lenPx * 0.5, 320.0);
                }
                if (sel || hov) s *= 1.25f;
                const bool moving = v.sog >= 0.5;
                const float hd = (float)((v.heading < 360 ? v.heading : v.cog) * DEG);
                const Vector2 dir{ std::sin(hd), -std::cos(hd) }, perp{ -dir.y, dir.x };
                const float wf = (v.len > 0 && v.beam > 0) ? Clamp((float)v.beam / v.len * 2.4f, 0.32f, 0.9f) : 0.5f;
                if (moving) {
                    auto hull = [&](float k, Color col) {
                        const Vector2 bow{ p.x + dir.x * s * 1.25f * k, p.y + dir.y * s * 1.25f * k };
                        const Vector2 pl{ p.x - dir.x * s * 0.75f * k - perp.x * s * wf * k, p.y - dir.y * s * 0.75f * k - perp.y * s * wf * k };
                        const Vector2 pr{ p.x - dir.x * s * 0.75f * k + perp.x * s * wf * k, p.y - dir.y * s * 0.75f * k + perp.y * s * wf * k };
                        const Vector2 tail{ p.x - dir.x * s * 0.4f * k, p.y - dir.y * s * 0.4f * k };
                        DrawTriangle(bow, pl, tail, col); DrawTriangle(bow, tail, pr, col);
                    };
                    hull(1.3f, Fade(BLACK, 0.55f)); hull(1.0f, c);
                } else {
                    DrawPoly(p, 4, s * 0.95f, 45.0f, Fade(BLACK, 0.55f)); DrawPoly(p, 4, s * 0.72f, 45.0f, c);
                }
                if (sel) DrawCircleLines((int)p.x, (int)p.y, s + 7, WHITE);
                if ((showLabels && zoomF > 22.0 && !v.name.empty() && labelFree(p.x, p.y)) || sel || hov)
                    UiText(v.name.empty() ? TextFormat("MMSI %d", v.mmsi) : v.name.c_str(), (int)p.x + (int)s + 6, (int)p.y - 6, 12, sel || hov ? WHITE : Fade(c, 0.95f));
            }
        }

        if (wxProbe) {
            const Vector2 c{ screenWidth * 0.5f, screenHeight * 0.5f };
            DrawCircleLines((int)c.x, (int)c.y, 9, Fade(WHITE, 0.8f));
            DrawLine((int)c.x - 16, (int)c.y, (int)c.x - 5, (int)c.y, Fade(WHITE, 0.8f)); DrawLine((int)c.x + 5, (int)c.y, (int)c.x + 16, (int)c.y, Fade(WHITE, 0.8f));
            DrawLine((int)c.x, (int)c.y - 16, (int)c.x, (int)c.y - 5, Fade(WHITE, 0.8f)); DrawLine((int)c.x, (int)c.y + 5, (int)c.x, (int)c.y + 16, Fade(WHITE, 0.8f));
        }
    };

    auto drawSolarSystemScene = [&]() {
        drawStarBackdrop(cam.position);
        const double T = (simTime / 86400.0 - 10957.5) / 36525.0;
        const double d = solarCam.dist;
        const double pxPerAU = (screenHeight * 0.5) / (d * std::tan(22.5 * DEG));
        auto line3 = [&](const Vec3d& a, const Vec3d& b, Color c) {
            float w0, w1;
            const Vector2 p0 = projectToScreen(solarRelativePosition(a), &w0), p1 = projectToScreen(solarRelativePosition(b), &w1);
            if (w0 > 0 && w1 > 0) DrawLineV(p0, p1, c);
        };

        {
            static const double rings[] = { 0.1, 0.25, 0.5, 1, 2, 5, 10, 20, 50, 100 };
            for (double R : rings) {
                const double rpx = R * pxPerAU;
                if (rpx < 28.0 || rpx > 20000.0) continue;
                Vec3d prev = eclipticToEquatorial({ R, 0, 0 });
                for (int k = 1; k <= 96; ++k) {
                    const double a = 2.0 * PI_D * k / 96;
                    const Vec3d cur = eclipticToEquatorial({ R * std::cos(a), R * std::sin(a), 0 });
                    line3(prev, cur, Fade(WHITE, 0.07f));
                    prev = cur;
                }
                float w; const Vector2 lp = projectToScreen(solarRelativePosition(eclipticToEquatorial({ R, 0, 0 })), &w);
                if (w > 0 && lp.x > 0 && lp.x < screenWidth && lp.y > 0 && lp.y < screenHeight) UiText(R < 1 ? TextFormat("%.2g AU", R) : TextFormat("%.0f AU", R), (int)lp.x + 4, (int)lp.y, 11, Fade(WHITE, 0.35f));
            }
        }

        for (int i = 0; i < NPLANETS; ++i) {
            const KeplerEl el = planetElements(i, T);
            Vec3d prev = orbitPoint(el, 0.0);
            const Color c{ PLANETS[i].r, PLANETS[i].g, PLANETS[i].b, (unsigned char)(i == selBody ? 230 : 110) };
            for (int k = 1; k <= 200; ++k) { const Vec3d cur = orbitPoint(el, k / 200.0); line3(prev, cur, c); prev = cur; }
        }
        {
            Vec3d prev = orbitPoint(halleyBody.el, 0.0);
            for (int k = 1; k <= 400; ++k) { const Vec3d cur = orbitPoint(halleyBody.el, k / 400.0); line3(prev, cur, Fade({ 170, 255, 220, 255 }, selBody == BODY_HALLEY ? 0.8f : 0.3f)); prev = cur; }
        }

        {
            const double days = simTime / 86400.0 - 10957.5;
            const KeplerEl jup = planetElements(4, T);
            for (const Rock& r : rocks) {
                KeplerEl k = r.el;
                k.MDeg = r.trojan ? jup.MDeg + r.mOff : r.el.MDeg + r.n * days;
                const Vec3d p = eclipticToEquatorial(keplerEcliptic(k));
                float w; const Vector2 s = projectToScreen(solarRelativePosition(p), &w);
                if (w <= 0 || s.x < 0 || s.x >= screenWidth || s.y < 0 || s.y >= screenHeight) continue;
                DrawPixelV(s, r.el.a > 20.0 ? Color{ 110, 140, 190, 150 } : r.trojan ? Color{ 200, 170, 120, 190 } : Color{ 170, 160, 150, 170 });
            }
        }

        {
            const Vector2 s = bodyScr[BODY_SUN];
            const float r = (float)std::max(4.0, SUN_RADIUS_KM / AU_KM * pxPerAU);
            DrawCircleGradient((int)s.x, (int)s.y, r * 5.0f + 20.0f, Fade({ 255, 220, 140, 255 }, 0.55f), BLANK);
            DrawCircle((int)s.x, (int)s.y, r, { 255, 245, 210, 255 });
        }

        for (int i = 0; i < NPLANETS + 1; ++i) {
            const bool halleyB = i == BODY_HALLEY;
            float w; const Vector2 s = projectToScreen(solarRelativePosition(bodyPos[i]), &w);
            if (w <= 0) continue;
            const Color c = halleyB ? Color{ 170, 255, 220, 255 } : Color{ PLANETS[i].r, PLANETS[i].g, PLANETS[i].b, 255 };
            const float rTrue = halleyB ? 0.0f : (float)(PLANETS[i].radiusKm / AU_KM * pxPerAU);
            const float r = std::max(halleyB ? 2.5f : 3.0f, rTrue);
            if (halleyB) { DrawCircleV(s, r, c); }
            else {
                DrawCircleV(s, r, c);
                DrawCircleV({ s.x + r * 0.28f, s.y + r * 0.28f }, r * 0.72f, Fade(BLACK, 0.28f));
                if (i == 5 && r > 3.0f) DrawEllipseLines((int)s.x, (int)s.y, r * 2.0f, r * 0.55f, Fade(c, 0.8f));
            }
            if (i == selBody || i == hovBody) DrawCircleLines((int)s.x, (int)s.y, r + 6.0f, i == selBody ? WHITE : GRAY);
            if (showLabels || i == selBody || i == hovBody) UiText(halleyB ? "Halley" : PLANETS[i].name, (int)s.x + (int)r + 5, (int)s.y - 7, 14, Fade(c, 0.95f));
        }
        if (selBody == BODY_SUN || hovBody == BODY_SUN) DrawCircleLines((int)bodyScr[BODY_SUN].x, (int)bodyScr[BODY_SUN].y, 14.0f, selBody == BODY_SUN ? WHITE : GRAY);
        UiText("Sun", (int)bodyScr[BODY_SUN].x + 12, (int)bodyScr[BODY_SUN].y - 20, 14, { 255, 230, 150, 255 });
    };

    auto drawSkyScene = [&]() {

        auto lerpC = [](Color a, Color b, float t) { return Color{ (unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t), (unsigned char)(a.b + (b.b - a.b) * t), 255 }; };
        struct Key { float alt; Color top, hor; };
        static const Key keys[] = { { -18, { 2, 4, 12, 255 }, { 10, 14, 30, 255 } }, { -12, { 6, 10, 32, 255 }, { 30, 36, 70, 255 } }, { -6, { 18, 30, 75, 255 }, { 120, 80, 110, 255 } },
                                    { 0, { 40, 70, 130, 255 }, { 235, 150, 90, 255 } }, { 10, { 60, 110, 190, 255 }, { 150, 190, 235, 255 } } };
        Color skyTop = keys[0].top, skyHor2 = keys[0].hor;
        const float sa = (float)sunAltDeg;
        if (sa >= keys[4].alt) { skyTop = keys[4].top; skyHor2 = keys[4].hor; }
        else if (sa > keys[0].alt)
            for (int k = 1; k < 5; ++k)
                if (sa <= keys[k].alt) { const float t = (sa - keys[k - 1].alt) / (keys[k].alt - keys[k - 1].alt); skyTop = lerpC(keys[k - 1].top, keys[k].top, t); skyHor2 = lerpC(keys[k - 1].hor, keys[k].hor, t); break; }
        DrawRectangleGradientV(0, 0, (int)screenWidth, (int)screenHeight, skyTop, skyHor2);
        const float starVis = Clamp((float)((-sunAltDeg - 3.0) / 9.0), 0.0f, 1.0f);
        const double Tc = (simTime / 86400.0 - 10957.5) / 36525.0;

        {
            const double elRings[] = { 0.0, -4.0, -10.0, -20.0, -35.0, -55.0, -80.0 };
            const int NA = 72, NR = 7;
            static Vector2 pts[7][73];
            static bool okp[7][73];
            for (int ri = 0; ri < NR; ++ri)
                for (int ai = 0; ai <= NA; ++ai) {
                    const double az = ai * 2.0 * PI_D / NA, el = elRings[ri] * DEG;
                    okp[ri][ai] = projectSkyDirection(std::cos(el) * std::sin(az), std::cos(el) * std::cos(az), std::sin(el), pts[ri][ai]);
                }
            const Color g0 = lerpC({ 14, 16, 18, 255 }, skyHor2, 0.18f), g1 = { 6, 7, 8, 255 };
            for (int ri = 0; ri < NR - 1; ++ri)
                for (int ai = 0; ai < NA; ++ai) {
                    if (!(okp[ri][ai] && okp[ri][ai + 1] && okp[ri + 1][ai] && okp[ri + 1][ai + 1])) continue;
                    const Color c = lerpC(g0, g1, std::min(1.0f, ri / 3.0f));
                    const Vector2 a = pts[ri][ai], b = pts[ri][ai + 1], d = pts[ri + 1][ai], e2 = pts[ri + 1][ai + 1];
                    DrawTriangle(a, d, b, c); DrawTriangle(a, b, d, c); DrawTriangle(b, d, e2, c); DrawTriangle(b, e2, d, c);
                }
            for (int ai = 0; ai < NA; ++ai) if (okp[0][ai] && okp[0][ai + 1]) DrawLineEx(pts[0][ai], pts[0][ai + 1], 1.5f, Fade(WHITE, 0.35f));
            for (double el : { 30.0, 60.0 }) {
                Vector2 pv{}; bool have = false;
                for (int ai = 0; ai <= NA; ++ai) {
                    const double az = ai * 2.0 * PI_D / NA, e = el * DEG;
                    Vector2 q;
                    const bool ok = projectSkyDirection(std::cos(e) * std::sin(az), std::cos(e) * std::cos(az), std::sin(e), q);
                    if (ok && have) DrawLineV(pv, q, Fade(WHITE, 0.07f));
                    pv = q; have = ok;
                }
            }
            static const char* cardinals[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
            for (int k = 0; k < 8; ++k) {
                const double az = k * PI_D / 4.0, e = 3.0 * DEG;
                Vector2 q;
                if (projectSkyDirection(std::cos(e) * std::sin(az), std::cos(e) * std::cos(az), std::sin(e), q))
                    UiText(cardinals[k], (int)q.x - UiMeasure(cardinals[k], k % 2 ? 12 : 16) / 2, (int)q.y - 20, k % 2 ? 12 : 16, k % 2 ? Fade(WHITE, 0.5f) : Color{ 235, 240, 250, 255 });
            }
        }

        if (starVis > 0.01f) {
            for (const auto& b : band) {
                const Vec3d v = precessToDate({ b.first.x, -b.first.z, b.first.y });
                double e, n, u;
                toHorizontalFrame(v, e, n, u);
                if (u < 0.0) continue;
                Vector2 q;
                if (projectSkyDirection(e, n, u, q)) DrawPixelV(q, Fade(b.second, starVis * 0.8f));
            }
            if (opts.showConst) {
                for (const ConstellationSegment& g : consSegs) {
                    double e1, n1, u1, e2, n2, u2;
                    toHorizontalFrame(precessToDate(g.from), e1, n1, u1); toHorizontalFrame(precessToDate(g.to), e2, n2, u2);
                    if (u1 < -0.05 && u2 < -0.05) continue;
                    Vector2 a, b;
                    if (projectSkyDirection(e1, n1, u1, a) && projectSkyDirection(e2, n2, u2, b)) DrawLineV(a, b, Fade({ 105, 150, 235, 255 }, 0.38f * starVis));
                }
                for (const ConstellationLabel& c : consLabels) {
                    double e, n, u;
                    toHorizontalFrame(precessToDate(c.direction), e, n, u);
                    Vector2 q;
                    if (u > 0.02 && projectSkyDirection(e, n, u, q)) UiText(c.name.c_str(), (int)q.x - UiMeasure(c.name.c_str(), 12) / 2, (int)q.y - 6, 12, Fade({ 125, 165, 245, 255 }, 0.75f * starVis));
                }
            }
            for (const auto& st : sky) {
                const Vec3d v = precessToDate({ st.direction.x, -st.direction.z, st.direction.y });
                double e, n, u;
                toHorizontalFrame(v, e, n, u);
                if (u < -0.01) continue;
                Vector2 q;
                if (!projectSkyDirection(e, n, u, q) || q.x < -10 || q.x > screenWidth + 10 || q.y < -10 || q.y > screenHeight + 10) continue;
                const float ext = 0.35f + 0.65f * Clamp((float)(u / 0.25), 0.0f, 1.0f);
                const float k = starVis * ext * opts.starBright;
                const Color c{ (unsigned char)std::min(255.0f, st.color.r * k * 1.25f), (unsigned char)std::min(255.0f, st.color.g * k * 1.25f), (unsigned char)std::min(255.0f, st.color.b * k * 1.25f), 255 };
                const int sz = st.pixelSize + (st.pixelSize > 1 ? 1 : 0);
                if (sz <= 1) DrawPixelV(q, c); else DrawCircleV(q, sz * 0.7f, c);
            }
        }

        const Vec3d sunUnit = sunDirection;
        Vector2 sunScr{ 0, 0 };
        bool sunOk = false;
        {
            double e, n, u;
            toHorizontalFrame(sunUnit, e, n, u);
            sunOk = u > -0.08 && projectSkyDirection(e, n, u, sunScr);
            if (sunOk) {
                DrawCircleGradient((int)sunScr.x, (int)sunScr.y, 170, Fade({ 255, 225, 170, 255 }, u > 0 ? 0.55f : 0.30f), BLANK);
                DrawCircleGradient((int)sunScr.x, (int)sunScr.y, 34, Fade({ 255, 250, 235, 255 }, 0.9f), BLANK);
                DrawCircleV(sunScr, (float)std::max(7.0, skyK * 0.0047), { 255, 250, 232, 255 });
                if (showLabels) UiText("Sun", (int)sunScr.x + 14, (int)sunScr.y - 8, 14, { 255, 235, 170, 255 });
            }
        }
        {
            const Vec3d mg = moonPos(daysSinceJ2000), mt{ mg.x - obsPos.x, mg.y - obsPos.y, mg.z - obsPos.z };
            const double ml = std::sqrt(mt.x * mt.x + mt.y * mt.y + mt.z * mt.z);
            double e, n, u;
            toHorizontalFrame({ mt.x / ml, mt.y / ml, mt.z / ml }, e, n, u);
            Vector2 mp;
            if (u > -0.05 && projectSkyDirection(e, n, u, mp)) {
                const double mgl = std::sqrt(mg.x * mg.x + mg.y * mg.y + mg.z * mg.z);
                const double cosEl = (mg.x * sunUnit.x + mg.y * sunUnit.y + mg.z * sunUnit.z) / mgl;
                const float frac = (float)((1.0 - cosEl) * 0.5), rm = (float)std::max(8.0, skyK * 0.0047);
                const float chi = sunOk ? std::atan2(sunScr.y - mp.y, sunScr.x - mp.x) : 0.0f;
                DrawCircleGradient((int)mp.x, (int)mp.y, rm * 4.0f, Fade({ 200, 210, 235, 255 }, 0.14f), BLANK);
                DrawCircleV(mp, rm, { 34, 38, 48, 255 });
                const float a = rm * (1.0f - 2.0f * frac);
                for (int t = -90; t < 90; t += 10) {
                    auto pt = [&](float x, float y) { return Vector2{ mp.x + x * std::cos(chi) - y * std::sin(chi), mp.y + x * std::sin(chi) + y * std::cos(chi) }; };
                    const float t0 = t * (float)DEG, t1 = (t + 10) * (float)DEG;
                    const Vector2 l0 = pt(rm * std::cos(t0), rm * std::sin(t0)), l1 = pt(rm * std::cos(t1), rm * std::sin(t1));
                    const Vector2 m0 = pt(a * std::cos(t0), rm * std::sin(t0)), m1 = pt(a * std::cos(t1), rm * std::sin(t1));
                    const Color mc{ 236, 236, 226, 255 };
                    DrawTriangle(l0, m0, l1, mc); DrawTriangle(l0, l1, m0, mc); DrawTriangle(l1, m0, m1, mc); DrawTriangle(l1, m1, m0, mc);
                }
                DrawCircleLines((int)mp.x, (int)mp.y, rm, Fade(WHITE, 0.25f));
                if (showLabels) UiText(TextFormat("Moon  %.0f%%", frac * 100.0f), (int)mp.x + (int)rm + 8, (int)mp.y - 7, 14, { 225, 228, 240, 255 });
            }
        }
        {
            static const double pmag[] = { 0.0, -4.2, 0.0, 0.8, -2.2, 0.7, 5.7, 7.8, 14.0 };
            const Vec3d ep = planetPos(2, Tc);
            for (int i = 0; i < 8; ++i) {
                if (i == 2) continue;
                const Vec3d pp = planetPos(i, Tc);
                Vec3d g{ pp.x - ep.x, pp.y - ep.y, pp.z - ep.z };
                const double gl = std::sqrt(g.x * g.x + g.y * g.y + g.z * g.z);
                double e, n, u;
                toHorizontalFrame(precessToDate({ g.x / gl, g.y / gl, g.z / gl }), e, n, u);
                Vector2 q;
                if (u < -0.02 || !projectSkyDirection(e, n, u, q)) continue;
                if (pmag[i] > 6.0 && starVis < 0.6f) continue;
                const float vis = pmag[i] < -2.0 ? 1.0f : Clamp(0.25f + (1.0f - (float)sunAltDeg / 12.0f) * 0.75f, 0.15f, 1.0f);
                const float r = (float)Clamp(2.2 + (3.5 - pmag[i]) * 0.75, 2.2, 7.5);
                const Color c{ PLANETS[i].r, PLANETS[i].g, PLANETS[i].b, 255 };
                DrawCircleGradient((int)q.x, (int)q.y, r * 3.5f, Fade(c, 0.35f * vis), BLANK);
                DrawCircleV(q, r, Fade(c, 0.4f + 0.6f * vis));
                UiText(PLANETS[i].name, (int)q.x + (int)r + 5, (int)q.y - 7, 13, Fade(c, 0.95f));
            }
        }

        for (size_t i = 0; i < entities.size(); ++i) {
            const Entity& e = entities[i];
            if (!vis[i] || e.dot) continue;
            const Color c = skyLit[i] ? e.color : Fade(e.color, 0.3f);
            DrawCircleGradient((int)screenPositions[i].x, (int)screenPositions[i].y, 14.0f * opts.markerScale, Fade(c, 0.30f), BLANK);
            DrawRectangle((int)screenPositions[i].x - 3, (int)screenPositions[i].y - 3, 7, 7, c);
        }
        if (!station.set)
            UiText("Showing the sky from 0°N 0°E.  Right-click the globe or map to set your own location.", (int)(screenWidth - UiMeasure("Showing the sky from 0°N 0°E.  Right-click the globe or map to set your own location.", 14)) / 2, 52, 14, { 255, 220, 120, 255 });
    };

    auto drawGalaxyScene = [&]() {
        const double d = galCam.dist;
        const double pxPerPc = (screenHeight * 0.5) / (d * std::tan(22.5 * DEG));
        const Vector3 tg = galCam.target;
        const float procFade = (float)Clamp((std::log10(d) - std::log10(60.0)) / (std::log10(3000.0) - std::log10(60.0)), 0.0, 1.0);

        const float kProj = (screenHeight * 0.5f) / std::tan(22.5f * (float)DEG);

        if (procFade > 0.0f) {
            float w; const Vector2 gc = projectToScreen(Vector3Subtract({ 0, 0, 0 }, tg), &w);
            if (w > 0) {
                const float k = kProj / w;
                const float rd = std::min(3500.0f * k, 4000.0f), rb = std::min(900.0f * k, 2000.0f);
                if (gc.x > -rd && gc.x < screenWidth + rd && gc.y > -rd && gc.y < screenHeight + rd) {
                    DrawCircleGradient((int)gc.x, (int)gc.y, std::max(rd, 6.0f), Fade({ 255, 210, 140, 255 }, 0.30f * procFade), BLANK);
                    DrawCircleGradient((int)gc.x, (int)gc.y, std::max(rb, 3.0f), Fade({ 255, 235, 190, 255 }, 0.42f * procFade), BLANK);
                }
            }
        }
        canvas.begin(screenWidth, screenHeight);
        if (procFade > 0.01f) {
            for (const GalPoint& p : galaxy) {
                float w; const Vector2 s = projectToScreen({ p.x - tg.x, p.z - tg.y, -p.y - tg.z }, &w);
                if (w <= 0 || s.x < 0 || s.x >= screenWidth || s.y < 0 || s.y >= screenHeight) continue;
                const int a = (int)(p.a * procFade);
                canvas.plot(s.x, s.y, p.r * a / 255, p.g * a / 255, p.b * a / 255, 1);
            }
        }
        for (size_t i = 0; i < catPos.size(); ++i) {
            float w; const Vector2 s = projectToScreen(Vector3Subtract(catPos[i], tg), &w);
            if (w <= 0 || s.x < 0 || s.x >= screenWidth || s.y < 0 || s.y >= screenHeight) continue;
            const int sz = d < 400.0 ? catSz[i] + (d < 60.0 ? 1 : 0) : 1;
            const int mutedColor = d > 2000.0 ? 150 : 255;
            canvas.plot(s.x, s.y, catCol[i].r * mutedColor / 255, catCol[i].g * mutedColor / 255, catCol[i].b * mutedColor / 255, sz);
        }
        canvas.flush(screenWidth, screenHeight);

        {
            static const double rings[] = { 10.0, 100.0, 1000.0, 10000.0 };
            for (double R : rings) {
                const double rpx = R * pxPerPc;
                if (rpx < 30.0 || rpx > 12000.0) continue;
                Vector3 prev{}; bool have = false; bool first = true;
                for (int k = 0; k <= 120; ++k) {
                    const double a = 2.0 * PI_D * k / 120;
                    const Vector3 cur = Vector3Subtract(galacticToRender(GAL_SUN_X + R * std::cos(a), R * std::sin(a), GAL_SUN_Z), tg);
                    if (have) {
                        float w0, w1; const Vector2 p0 = projectToScreen(prev, &w0), p1 = projectToScreen(cur, &w1);
                        if (w0 > 0 && w1 > 0) DrawLineV(p0, p1, Fade({ 120, 200, 255, 255 }, 0.22f));
                        if (first && w1 > 0 && p1.x > 0 && p1.x < screenWidth && p1.y > 0 && p1.y < screenHeight) { UiText(R >= 1000 ? TextFormat("%.0f kpc", R / 1000.0) : TextFormat("%.0f pc", R), (int)p1.x + 3, (int)p1.y, 11, Fade({ 120, 200, 255, 255 }, 0.7f)); first = false; }
                    }
                    prev = cur; have = true;
                }
            }
        }

        if (d > 6000.0 && showLabels) {
            for (const SpiralArmName& arm : kSpiralArmNames) {
                const double psi = 2.35, r = galArmRadius(arm.armIndex, psi);
                float w; const Vector2 s = projectToScreen(Vector3Subtract(galacticToRender(r * std::cos(psi), r * std::sin(psi), 0), tg), &w);
                if (w > 0) UiText(arm.name, (int)s.x, (int)s.y, 13, Fade({ 150, 190, 255, 255 }, 0.8f));
            }
        }

        for (size_t i = 0; i < gal.size(); ++i) {
            const GalObj& o = gal[i];
            float w; const Vector2 sp = projectToScreen(Vector3Subtract(o.pos, tg), &w);
            if (w <= 0 || sp.x < -40 || sp.x > screenWidth + 40 || sp.y < -40 || sp.y > screenHeight + 40) continue;
            const bool sel = (int)i == selGal, hov = (int)i == hovGal;
            if (o.kind == 2) {
                DrawCircleGradient((int)sp.x, (int)sp.y, std::min(2500.0f, std::max(7.0f, o.sizePc * kProj / w)), Fade(o.col, 0.4f), BLANK);
                UiText(o.name.c_str(), (int)sp.x + 10, (int)sp.y - 7, 14, Fade(o.col, 0.9f));
            } else if (o.kind == 1) {
                DrawCircleLines((int)sp.x, (int)sp.y, 6, o.col);
                UiText(o.name.c_str(), (int)sp.x + 10, (int)sp.y - 7, 14, o.col);
            } else {
                const bool labelOk = showLabels && ((d < 40.0) || (d < 400.0 && o.mag < 2.5f) || (d < 3000.0 && o.mag < 1.2f));
                if (labelOk || sel || hov) UiText(o.name.c_str(), (int)sp.x + 7, (int)sp.y - 6, 13, Fade(o.col, 0.9f));
            }
            if (sel) DrawCircleLines((int)sp.x, (int)sp.y, 11, WHITE);
            else if (hov) DrawCircleLines((int)sp.x, (int)sp.y, 11, GRAY);
        }

        {
            const double target = 140.0 / pxPerPc;
            const double mag10 = std::pow(10.0, std::floor(std::log10(target)));
            const double nice = target / mag10 >= 5 ? 5 * mag10 : target / mag10 >= 2 ? 2 * mag10 : mag10;
            const float len = (float)(nice * pxPerPc);
            const int bx = (int)(screenWidth * 0.5f - len * 0.5f), by = (int)screenHeight - 74;
            DrawLine(bx, by, bx + (int)len, by, Fade(WHITE, 0.8f)); DrawLine(bx, by - 4, bx, by + 4, Fade(WHITE, 0.8f)); DrawLine(bx + (int)len, by - 4, bx + (int)len, by + 4, Fade(WHITE, 0.8f));
            const std::string lab = fmtParsec(nice);
            UiText(lab.c_str(), (int)(screenWidth * 0.5f) - UiMeasure(lab.c_str(), 14) / 2, by - 20, 14, Fade(WHITE, 0.9f));
        }
    };

    auto drawGlobeCities = [&]() {
        if (mode != Mode::Globe || !opts.showCities) return;
        const double altitude = std::max(0.05, (double)dist - earthR);
        const double minimumImportance = 14.0e6 * std::pow(altitude / 16.0, 2.2);
        std::unordered_set<long long> usedLabelCells;
        for (const City& city : cities) {
            if (city.importance < minimumImportance) continue;
            const Vector3 position = Vector3Transform(Vector3Scale(city.surfaceDirection, earthR * 1.001f), earthModel.transform);
            if (occludedByBody(cam.position, position, earthR)) continue;
            const Vector2 p = projectToScreen(position);
            if (p.x < 0 || p.x > screenWidth || p.y < 0 || p.y > screenHeight) continue;
            const Color fill = city.capital ? Color{ 255, 214, 110, 255 } : Color{ 236, 241, 250, 255 };
            DrawCircleV(p, 3.2f * opts.markerScale, Fade(BLACK, 0.6f));
            DrawCircleV(p, 2.1f * opts.markerScale, fill);
            const long long cell = (long long)((int)(p.x / 90.0f)) * 100003LL + (int)(p.y / 16.0f);
            if (showLabels && usedLabelCells.insert(cell).second) {
                UiText(city.name.c_str(), (int)p.x + 7, (int)p.y - 5, 12, Fade(BLACK, 0.8f));
                UiText(city.name.c_str(), (int)p.x + 6, (int)p.y - 6, 12, Fade(WHITE, 0.92f));
            }
        }
    };

    auto drawSatelliteOverlay = [&]() {
        if (satMode) {
            for (size_t i = 0; i < entities.size(); ++i) {
                if (!vis[i] || !entities[i].dot) continue;
                DrawRectangle((int)screenPositions[i].x - 1, (int)screenPositions[i].y - 1, 2, 2, (mode == Mode::Sky && !skyLit[i]) ? Fade(entities[i].color, 0.22f) : entities[i].color);
            }
            if (showLabels) {
                for (size_t i = 0; i < entities.size(); ++i) {
                    if (!vis[i] || !entities[i].labeled) continue;
                    UiText(entities[i].name.c_str(), (int)screenPositions[i].x + 9, (int)screenPositions[i].y - 7, 14, Fade(entities[i].color, 0.95f));
                }
                if (mode == Mode::Globe && Vector3DotProduct(Vector3Subtract(moonScenePosition, cam.position), fwd) > 0.0f && !occludedByBody(cam.position, moonScenePosition, earthR)) {
                    Vector2 mp = projectToScreen(moonScenePosition);
                    UiText("Moon", (int)mp.x + 12, (int)mp.y - 7, 14, { 200, 200, 200, 230 });
                }
            }
            for (int idx : { hovered, selected }) {
                if (idx < 0 || !vis[idx]) continue;
                DrawCircleLines((int)screenPositions[idx].x, (int)screenPositions[idx].y, 11, idx == selected ? WHITE : GRAY);
                if (!entities[idx].labeled) UiText(entities[idx].name.c_str(), (int)screenPositions[idx].x + 14, (int)screenPositions[idx].y - 7, 14, WHITE);
            }
        }

    };

    auto drawToolbar = [&]() {
        showQuickToggles = screenWidth > 1420.0f;

        DrawRectangle(0, 0, (int)screenWidth, (int)toolbarHeight, { 9, 12, 20, 232 });
        DrawLine(0, (int)toolbarHeight, (int)screenWidth, (int)toolbarHeight, Fade(WHITE, 0.10f));
        ui.region({ 0, 0, screenWidth, toolbarHeight });
        bx = 8.0f; layersX = 8.0f;
        if (addLeftToolbarButton("Earth", mode == Mode::Globe, "Globe view  (E)")) setMode(Mode::Globe);
        if (addLeftToolbarButton("Map", mode == Mode::Map, "Zoomable map with live data  (M)")) setMode(Mode::Map);
        if (addLeftToolbarButton("Sky", mode == Mode::Sky, "The sky from your ground station  (S)")) setMode(Mode::Sky);
        if (addLeftToolbarButton("Solar System", mode == Mode::Solar, "Planets, comet, asteroid belts  (P)")) setMode(Mode::Solar);
        if (addLeftToolbarButton("Milky Way", mode == Mode::Galaxy, "The galaxy and nearby stars  (W)")) setMode(Mode::Galaxy);
        bx += 10.0f;
        layersX = bx;
        if (addLeftToolbarButton("Layers", layersOpen, "Overlays, base map and filters")) { layersOpen = !layersOpen; if (layersOpen) settingsOpen = false; }
        if (addLeftToolbarButton("Labels", showLabels, "Labels  (L)")) showLabels = !showLabels;
        if (satMode && showQuickToggles && addLeftToolbarButton("Track", showTrack, "Ground track of the selected satellite  (G)")) showTrack = !showTrack;
        if (mode == Mode::Map && showQuickToggles) {
            if (addLeftToolbarButton("Aircraft", adsb.enabled(), "Live ADS-B aircraft  (A)")) adsb.setEnabled(!adsb.enabled());
            if (addLeftToolbarButton("Ships", shipsOn, "Live AIS vessels  (B)")) toggleShips();
            if (addLeftToolbarButton("Radar", opts.radarOn, "Rain radar  (D)")) toggleRadar();
        }
        rx = screenWidth - 8.0f;
        if (addRightToolbarButton("Help", helpOpen, "Keyboard shortcuts  (F1)")) helpOpen = !helpOpen;
        if (addRightToolbarButton("Settings", settingsOpen, "Options  (F2)")) { settingsOpen = !settingsOpen; if (settingsOpen) layersOpen = false; }
        if (addRightToolbarButton("Search", searching, "Search  (/)")) { searching = !searching; query.clear(); matches.clear(); }
        rx -= 10.0f;
        if (addRightToolbarButton("Now", false, "Jump to real time  (N)")) { simTime = nowUnix(); timeScale = 1.0; }
        if (addRightToolbarButton(">>", false, "Faster  (.)")) timeScale = (timeScale < 0 ? -1.0 : 1.0) * std::min(std::fabs(timeScale) * 2.0, 1.6e8);
        {
            const std::string sp = TextFormat("x%.0f", timeScale);
            const float w = (float)UiMeasure(sp.c_str(), 14) + 10.0f;
            rx -= w;
            UiText(sp.c_str(), (int)rx + 5, 9, 14, timeScale < 0 ? Color{ 255, 190, 90, 255 } : WHITE);
            rx -= 2.0f;
        }
        if (addRightToolbarButton(paused ? "Play" : "Pause", paused, "Pause / resume time  (Space)")) paused = !paused;
        if (addRightToolbarButton("<<", false, "Slower  (,)")) timeScale = (timeScale < 0 ? -1.0 : 1.0) * std::max(std::fabs(timeScale) / 2.0, 1.0);
        if (addRightToolbarButton("Time", timeOpen, "Jump to a date and time  (J)")) { timeOpen = !timeOpen; if (timeOpen) timeText = fmtClock(simTime, false).substr(0, 16); }

    };

    auto drawTopLeftReadout = [&]() {
        readoutBottomY = (int)toolbarHeight + 8;
        {
            const size_t nObj = mode == Mode::Solar ? (size_t)NPLANETS + 2 + rocks.size() : mode == Mode::Galaxy ? galaxy.size() + catPos.size() : entities.size();
            UiText(TextFormat("%d FPS   %zu objects   %s", GetFPS(), nObj, modeName(mode)), 12, readoutBottomY, 13, mutedColor);
            readoutBottomY += 19;
            UiText(fmtClock(simTime, opts.localTime).c_str(), 12, readoutBottomY, 18, WHITE);
            readoutBottomY += 26;
            if (follow && selected >= 0) { UiText(TextFormat("Following %s  (F / Esc to release)", entities[selected].name.c_str()), 12, readoutBottomY, 14, { 255, 220, 90, 255 }); readoutBottomY += 20; }
            if (followBody && selBody >= 0) { UiText(TextFormat("Following %s  (F / Esc to release)", candName(selBody).c_str()), 12, readoutBottomY, 14, { 255, 220, 90, 255 }); readoutBottomY += 20; }
            if (followGal && selGal >= 0) { UiText(TextFormat("Following %s  (F / Esc to release)", gal[selGal].name.c_str()), 12, readoutBottomY, 14, { 255, 220, 90, 255 }); readoutBottomY += 20; }
            if (mode == Mode::Galaxy) { UiText(TextFormat("view distance %s", fmtParsec(galCam.dist).c_str()), 12, readoutBottomY, 13, mutedColor); readoutBottomY += 19; }
            if (mode == Mode::Solar) { UiText(TextFormat("view distance %.3g AU", solarCam.dist), 12, readoutBottomY, 13, mutedColor); readoutBottomY += 19; }
            if (mode == Mode::Sky) {
                UiText(TextFormat("Observer %.2f°, %.2f°   Sun altitude %.0f°   looking az %.0f° el %.0f°   fov %.0f°", skyObs.lat, skyObs.lon, sunAltDeg, std::fmod(skyAz / DEG + 360.0, 360.0), skyEl / DEG, skyFov), 12, readoutBottomY, 13, mutedColor);
                readoutBottomY += 19;
            }
        }


    };

    auto drawLayersPopup = [&]() {
        if (layersOpen) {
            const float pw = 292.0f, lx = std::min(layersX, screenWidth - pw - 8.0f), py = toolbarHeight + 6.0f;
            const float ph = mode == Mode::Map ? 392.0f : mode == Mode::Globe ? 408.0f : mode == Mode::Sky ? 250.0f : 70.0f;
            uiShadowBox({ lx, py, pw, ph }, 0.92f);
            ui.region({ lx, py, pw, ph });
            float y = py + 10.0f;
            const float tx = lx + 6.0f, tw = pw - 12.0f;
            if (mode == Mode::Map) {
                drawPopupSectionTitle(lx, y, "BASE MAP");
                ui.stepper(tx, y, tw, "Style", { "Blue Marble", "OSM Dark", "OSM Standard", "NASA MODIS (daily)" }, opts.baseMap); y += 30;
                drawPopupSectionTitle(lx, y, "LIVE DATA OVERLAYS");
                { bool v = adsb.enabled(); if (ui.toggle(tx, y, tw, "Aircraft  (ADS-B)   [A]", v)) adsb.setEnabled(v); y += 24; }
                { bool v = shipsOn; if (ui.toggle(tx, y, tw, "Ships  (AIS: Baltic / Finland)   [B]", v, "Digitraffic open data; add your own NMEA feed in spacemap.cfg")) toggleShips(); y += 24; }
                { bool v = opts.radarOn; if (ui.toggle(tx, y, tw, "Rain radar  (RainViewer)   [D]", v)) toggleRadar(); y += 24; }
                ui.toggle(tx, y, tw, "Cloud imagery  (GOES / Himawari IR)   [K]", opts.cloudsOn, "NASA GIBS; covers the Americas and Asia-Pacific"); y += 24;
                ui.toggle(tx, y, tw, "Wind vectors  (Open-Meteo)   [Z]", opts.windOn); y += 24;
                ui.toggle(tx, y, tw, "Weather at map centre   [X]", wxProbe); y += 28;
                ui.slider(tx, y, tw, "Radar opacity", opts.radarOpacity, 0.2f, 1.0f); y += 30;
                drawPopupSectionTitle(lx, y, "DISPLAY");
                ui.toggle(tx, y, tw, "Labels   [L]", showLabels); y += 24;
                ui.toggle(tx, y, tw, "Ground track   [G]", showTrack); y += 24;
                ui.toggle(tx, y, tw, "City names   [I]", opts.showCities); y += 24;
                ui.toggle(tx, y, tw, "Trails behind aircraft and ships", opts.acTrails); y += 24;
            } else if (mode == Mode::Globe) {
                drawPopupSectionTitle(lx, y, "EARTH");
                ui.toggle(tx, y, tw, "Clouds", opts.showClouds); y += 24;
                ui.toggle(tx, y, tw, "Ocean sun-glint", opts.showSpecular); y += 24;
                ui.toggle(tx, y, tw, "City lights at night", opts.showNightLights); y += 24;
                ui.toggle(tx, y, tw, "Constellation lines   [Q]", opts.showConst); y += 24;
                ui.toggle(tx, y, tw, "City names   [I]", opts.showCities); y += 24;
                drawPopupSectionTitle(lx, y, "SATELLITES");
                ui.toggle(tx, y, tw, "Labels   [L]", showLabels); y += 24;
                ui.toggle(tx, y, tw, "Orbit lines of named objects   [O]", showOrbits); y += 24;
                ui.toggle(tx, y, tw, "LEO mega-constellations   [C]", showConstellation); y += 24;
                ui.toggle(tx, y, tw, "Ground track of selection   [G]", showTrack); y += 28;
                drawPopupSectionTitle(lx, y, "ORBIT CLASSES");
                ui.toggle(tx, y, tw, "Low Earth orbit (LEO)", opts.satLEO); y += 24;
                ui.toggle(tx, y, tw, "Medium Earth orbit (navigation)", opts.satMEO); y += 24;
                ui.toggle(tx, y, tw, "Geostationary (GEO)", opts.satGEO); y += 24;
                ui.toggle(tx, y, tw, "Highly elliptical (HEO)", opts.satHEO); y += 24;
            } else if (mode == Mode::Sky) {
                drawPopupSectionTitle(lx, y, "SKY");
                ui.toggle(tx, y, tw, "Labels   [L]", showLabels); y += 24;
                ui.toggle(tx, y, tw, "Constellation lines and names   [Q]", opts.showConst); y += 24;
                ui.toggle(tx, y, tw, "Only visible passes in the planner", skyVisOnly); y += 28;
                drawPopupSectionTitle(lx, y, "SATELLITES BY ORBIT");
                ui.toggle(tx, y, tw, "Low Earth orbit (LEO)", opts.satLEO); y += 24;
                ui.toggle(tx, y, tw, "Medium Earth orbit (navigation)", opts.satMEO); y += 24;
                ui.toggle(tx, y, tw, "Geostationary (GEO)", opts.satGEO); y += 24;
                ui.toggle(tx, y, tw, "Highly elliptical (HEO)", opts.satHEO); y += 24;
            } else {
                drawPopupSectionTitle(lx, y, "DISPLAY");
                ui.toggle(tx, y, tw, "Labels   [L]", showLabels);
            }
        }

    };

    auto drawSettingsPopup = [&]() {
        static bool settingsWas = false;
        if (settingsOpen) {
            const float pw = 340.0f, lx = 8.0f, py = toolbarHeight + 6.0f, ph = 528.0f;
            uiShadowBox({ lx, py, pw, ph }, 0.94f);
            ui.region({ lx, py, pw, ph });
            float y = py + 10.0f;
            const float tx = lx + 6.0f, tw = pw - 12.0f;
            drawPopupSectionTitle(lx, y, "GENERAL");
            ui.stepper(tx, y, tw, "Units", { "Metric", "Imperial", "Aviation / marine" }, opts.units); y += 28;
            ui.toggle(tx, y, tw, "Show times in local time zone", opts.localTime); y += 24;
            ui.slider(tx, y, tw, "Marker size", opts.markerScale, 0.5f, 2.5f); y += 26;
            ui.slider(tx, y, tw, "Star brightness", opts.starBright, 0.4f, 2.0f); y += 26;
            ui.toggle(tx, y, tw, "Show shortcut hints", opts.showHints); y += 24;
            ui.toggle(tx, y, tw, "Show status bar", opts.showStatus); y += 24;
            if (ui.toggle(tx, y, tw, "Vertical sync", opts.vsync)) { if (opts.vsync) SetWindowState(FLAG_VSYNC_HINT); else ClearWindowState(FLAG_VSYNC_HINT); } y += 28;
            drawPopupSectionTitle(lx, y, "AIRCRAFT");
            ui.toggle(tx, y, tw, "Show aircraft on the ground", opts.acGround); y += 24;
            ui.stepper(tx, y, tw, "Minimum altitude", { "All", ">= 10,000 ft", ">= 20,000 ft", ">= 30,000 ft" }, opts.acMinAlt); y += 30;
            drawPopupSectionTitle(lx, y, "SHIPS");
            ui.toggle(tx, y, tw, "Cargo", opts.shipCargo); y += 24;
            ui.toggle(tx, y, tw, "Tankers", opts.shipTanker); y += 24;
            ui.toggle(tx, y, tw, "Passenger and high-speed craft", opts.shipPass); y += 24;
            ui.toggle(tx, y, tw, "Fishing", opts.shipFish); y += 24;
            ui.toggle(tx, y, tw, "Tugs, pilots, sailing, other", opts.shipOther); y += 24;
            ui.toggle(tx, y, tw, "Hide stationary vessels", opts.shipMoving); y += 30;
            if (ui.button({ tx + 2, y, 150, 26 }, "Reset to defaults", false, "Restore all options")) { const std::string nm = opts.aisNmea; opts.reset(); opts.aisNmea = nm; }
            y += 34;
            UiText("Options are saved to spacemap.cfg when this panel closes.", (int)tx + 2, (int)y, 11, mutedColor);
            UiText(ais.hasNmea() ? TextFormat("AIS NMEA feed: %s (+ Digitraffic)", opts.aisNmea.c_str()) : "AIS: Digitraffic (Baltic). Set ais_nmea=host:port in spacemap.cfg for more.", (int)tx + 2, (int)y + 16, 11, mutedColor);
        }
        if (settingsWas && !settingsOpen) opts.save();
        settingsWas = settingsOpen;

    };

    auto drawHelpOverlay = [&]() {
        if (helpOpen) {
            const float pw = 760.0f, ph = 430.0f, hx = (screenWidth - pw) * 0.5f, hyy = std::max(toolbarHeight + 12.0f, (screenHeight - ph) * 0.5f);
            uiShadowBox({ hx, hyy, pw, ph }, 0.95f);
            ui.region({ hx, hyy, pw, ph });
            UiText("Keyboard & mouse", (int)hx + 20, (int)hyy + 14, 20, WHITE);
            struct KV { const char* k; const char* v; };
            const KV col1[] = { { "VIEWS", "" }, { "E  M  S  P  W", "Earth / Map / Sky / Solar System / Milky Way" }, { "Tab", "next view" }, { "NAVIGATION", "" }, { "Drag", "rotate (3D)  /  pan (map)  /  look around (sky)" },
                                { "Wheel", "zoom (to the cursor on the map)" }, { "Click", "select an object" }, { "Right-click", "place your ground station" }, { "/", "search (satellites, planets, stars)" },
                                { "F", "follow / centre on the selection" }, { "H", "home: station on map, Sun in galaxy" }, { "R", "reset the view" }, { "Esc", "close panels, release, deselect, quit" } };
            const KV col2[] = { { "TIME", "" }, { "Space", "pause" }, { ", .  T  1  N", "slower / faster, reverse, real time, now" }, { "LAYERS", "" }, { "A", "live aircraft (ADS-B)" }, { "B", "ships (AIS)" },
                                { "D  K  Z  X", "rain radar / clouds / wind / weather at centre" }, { "Y", "cycle base map" }, { "L  O  C  G", "labels / orbits / constellations / ground track" },
                                { "U", "cycle units" }, { "J", "jump to a date and time" }, { "Q", "constellation lines" }, { "I", "city names" }, { "F1  F2  F12", "help / settings / screenshot" } };
            auto column = [&](const KV* rows, int n, float x0) {
                float y = hyy + 52.0f;
                for (int i = 0; i < n; ++i) {
                    if (rows[i].v[0] == 0) { y += 6; UiText(rows[i].k, (int)x0, (int)y, 11, { 120, 130, 150, 255 }); y += 20; continue; }
                    UiText(rows[i].k, (int)x0, (int)y, 14, { 130, 190, 255, 255 });
                    UiText(rows[i].v, (int)x0 + 112, (int)y, 14, textColor);
                    y += 23;
                }
            };
            column(col1, (int)(sizeof(col1) / sizeof(col1[0])), hx + 22);
            column(col2, (int)(sizeof(col2) / sizeof(col2[0])), hx + 392);
            UiText("Data: CelesTrak TLE (SGP4) - ADS-B: adsb.lol / OpenSky - AIS: Digitraffic - Radar: RainViewer - Weather: Open-Meteo - Imagery: NASA GIBS, OpenStreetMap",
                   (int)hx + 22, (int)(hyy + ph - 28), 11, mutedColor);
        }

    };

    auto drawMapOverlays = [&]() {
        if (mode == Mode::Map) {
            double cLat2, cLon2;
            screenToLatLon(screenWidth * 0.5, screenHeight * 0.5, cLat2, cLon2);
            float ly = screenHeight - (opts.showStatus ? 22.0f : 0.0f) - 10.0f;
            if (adsb.enabled()) {
                ly -= 38.0f;
                UiText("Aircraft altitude", 14, (int)ly, 11, mutedColor);
                for (int i = 0; i < 150; ++i) DrawRectangle(14 + i, (int)ly + 16, 1, 8, altColor(i / 150.0 * 46000.0));
                UiText("0", 14, (int)ly + 25, 10, mutedColor); UiText("FL460", 14 + 150 - 28, (int)ly + 25, 10, mutedColor);
                ly -= 4.0f;
            }
            if (shipsOn) {
                ly -= 40.0f;
                UiText("Vessels", 14, (int)ly, 11, mutedColor);
                const struct { const char* n; Color c; } kinds[] = { { "Cargo", { 90, 214, 120, 255 } }, { "Tanker", { 242, 92, 82, 255 } }, { "Passenger", { 92, 172, 255, 255 } }, { "Fishing", { 255, 172, 62, 255 } }, { "Other", { 200, 172, 255, 255 } } };
                float kx = 14.0f;
                for (const auto& k : kinds) { DrawRectangle((int)kx, (int)ly + 19, 9, 9, k.c); UiText(k.n, (int)kx + 13, (int)ly + 16, 11, mutedColor); kx += 18.0f + UiMeasure(k.n, 11) + 8.0f; }
                ly -= 4.0f;
            }
            if (opts.radarOn) {
                ly -= 38.0f;
                UiText("Rain intensity", 14, (int)ly, 11, mutedColor);
                const Color rc[] = { { 120, 200, 255, 255 }, { 40, 140, 255, 255 }, { 40, 200, 90, 255 }, { 250, 230, 60, 255 }, { 255, 140, 30, 255 }, { 240, 40, 40, 255 }, { 220, 60, 220, 255 } };
                for (int i = 0; i < 6; ++i) DrawRectangleGradientH(14 + i * 25, (int)ly + 16, 25, 8, rc[i], rc[i + 1]);
                UiText("light", 14, (int)ly + 25, 10, mutedColor); UiText("heavy", 14 + 150 - 26, (int)ly + 25, 10, mutedColor);
            }

            {
                const double mpp = 40075016.686 * std::cos(cLat2 * DEG) / mapWidthPx;
                const double unit = opts.units == 0 ? 1000.0 : opts.units == 1 ? 1609.344 : 1852.0;
                const char* un = opts.units == 0 ? "km" : opts.units == 1 ? "mi" : "nm";
                double target = 130.0 * mpp / unit;
                const char* uname = un;
                double scale = unit;
                if (target < 1.0) { scale = opts.units == 0 ? 1.0 : 0.3048; uname = opts.units == 0 ? "m" : "ft"; target = 130.0 * mpp / scale; }
                const double m10 = std::pow(10.0, std::floor(std::log10(target)));
                const double nice = target / m10 >= 5 ? 5 * m10 : target / m10 >= 2 ? 2 * m10 : m10;
                const float len = (float)(nice * scale / mpp);
                const int sx = (int)screenWidth - 14 - (int)len, sy = (int)screenHeight - (opts.showStatus ? 22 : 0) - 24;
                DrawLine(sx, sy, sx + (int)len, sy, WHITE); DrawLine(sx, sy - 4, sx, sy + 4, WHITE); DrawLine(sx + (int)len, sy - 4, sx + (int)len, sy + 4, WHITE);
                const std::string lab = TextFormat(nice >= 1 ? "%.0f %s" : "%.1f %s", nice, uname);
                UiText(lab.c_str(), sx + (int)len - UiMeasure(lab.c_str(), 12), sy - 18, 12, WHITE);
            }

            if (opts.radarOn && !radarFrames.empty()) {
                const float rw = 330.0f, rxx = (screenWidth - rw) * 0.5f, ryy = screenHeight - (opts.showStatus ? 22.0f : 0.0f) - 44.0f;
                uiShadowBox({ rxx, ryy, rw, 34 }, 0.85f);
                ui.region({ rxx, ryy, rw, 34 });
                if (ui.button({ rxx + 6, ryy + 5, 26, 24 }, "<", false, "Previous frame")) { radarPlay = false; radarIdx = (radarIdx + (int)radarLayers.size() - 1) % (int)radarLayers.size(); }
                if (ui.button({ rxx + 36, ryy + 5, 52, 24 }, radarPlay ? "Pause" : "Play", !radarPlay, "Animate the radar loop")) radarPlay = !radarPlay;
                if (ui.button({ rxx + 92, ryy + 5, 26, 24 }, ">", false, "Next frame")) { radarPlay = false; radarIdx = (radarIdx + 1) % (int)radarLayers.size(); }
                const RadarFrame& f = radarFrames[std::min<size_t>(radarShown, radarFrames.size() - 1)];
                const double ageMin = (nowUnix() - f.time) / 60.0;
                UiText(TextFormat("Radar  %s  (%.0f min ago)", fmtClock(f.time, opts.localTime).substr(11, 5).c_str(), std::max(0.0, ageMin)), (int)rxx + 128, (int)ryy + 9, 13, WHITE);
            }
        }

    };

    auto drawStatusBarAndHints = [&]() {
        if (opts.showStatus) {
            DrawRectangle(0, (int)screenHeight - 22, (int)screenWidth, 22, { 9, 12, 20, 235 });
            DrawLine(0, (int)screenHeight - 22, (int)screenWidth, (int)screenHeight - 22, Fade(WHITE, 0.08f));
            std::string st;
            auto add = [&](const std::string& t) { if (!t.empty()) { if (!st.empty()) st += "   ·   "; st += t; } };
            if (satMode) add(TextFormat("%zu satellites (SGP4: %zu)", entities.size(), (size_t)std::count_if(entities.begin(), entities.end(), [](const Entity& e) { return e.orbit.useSgp4; })));
            if (mode == Mode::Map) {
                if (adsb.enabled()) add(adsb.status());
                if (shipsOn) add(ais.status());
                if (opts.radarOn) add(radarFeed.status());
                if (opts.windOn || wxProbe) add(wx.status());
                if (tiles.pending() > 0) add(TextFormat("%zu tiles loading", tiles.pending()));
            }
            if (mode == Mode::Galaxy) add("HYG catalogue (17k stars) + procedural Milky Way model");
            if (mode == Mode::Sky) add(TextFormat("%s · %zu constellations · drag to look around, wheel to zoom", station.set ? "your station" : "default location", consLabels.size()));
            { const std::string es = imagery.status(); if (!es.empty()) add(es); }
            { const std::string ts = tleUpdater.status(); if (!ts.empty()) add(ts); }
            if (mode == Mode::Solar) add("JPL approximate ephemerides (1800-2050)");
            UiText(st.c_str(), 10, (int)screenHeight - 18, 12, { 170, 180, 198, 255 });
            if (mode == Mode::Map) {
                double la, lo; screenToLatLon(mouse.x, mouse.y, la, lo);
                const std::string cs = TextFormat("%.4f, %.4f   zoom %.1fx", la, lo, mapWidthPx / screenWidth);
                UiText(cs.c_str(), (int)screenWidth - UiMeasure(cs.c_str(), 12) - 10, (int)screenHeight - 18, 12, { 200, 208, 225, 255 });
            }
        }
        if (opts.showHints && !helpOpen && !settingsOpen && !layersOpen && !(mode == Mode::Map && opts.radarOn)) {
            const char* hint = mode == Mode::Map ? "Drag pan  ·  wheel zoom  ·  A aircraft  ·  B ships  ·  D radar  ·  K clouds  ·  Z wind  ·  F1 help"
                             : mode == Mode::Globe ? "Drag rotate  ·  wheel zoom  ·  click a satellite  ·  right-click: ground station  ·  / search  ·  F1 help"
                             : "Drag rotate  ·  wheel zoom  ·  click to select  ·  / search  ·  F follow  ·  F1 help";
            const int hw = UiMeasure(hint, 12);
            UiText(hint, (int)(screenWidth - hw) / 2, (int)screenHeight - (opts.showStatus ? 22 : 0) - 20, 12, Fade(WHITE, 0.45f));
        }

    };

    auto drawInfoPanels = [&]() {
        const int px = (int)screenWidth - 300;
        const int py0 = (int)toolbarHeight + 10;
        const Color classCol[] = { { 215, 225, 240, 255 }, { 90, 255, 120, 255 }, { 200, 160, 255, 255 }, { 255, 240, 120, 255 } };
        const char* classNames[] = { "Low Earth orbit (LEO)", "Medium Earth orbit (MEO)", "Geostationary / geosynchronous", "Highly elliptical (HEO)" };
        hoverTitle.clear(); hoverDetail.clear();
        {
            PanelData pd;
            bool have = false;
            auto addRow = [&](const char* k, const std::string& v, Color c = { 235, 238, 245, 255 }) { pd.rows.push_back({ k, v, c, false }); };
            auto addSection = [&](const char* t) { pd.rows.push_back({ t, "", WHITE, true }); };
            if (satMode && selected >= 0) {
                have = true;
                const Entity& e = entities[selected];
                const double r = e.altKm + earth.radiusKm;
                const double rx2 = e.pos.x / kSceneUnitsPerKm, ry2 = -e.pos.z / kSceneUnitsPerKm, rz2 = e.pos.y / kSceneUnitsPerKm;
                const double lat = std::atan2(rz2, std::hypot(rx2, ry2)) / DEG;
                double lon = std::fmod((std::atan2(ry2, rx2) - earthRotationAngle) / DEG, 360.0);
                if (lon > 180.0) lon -= 360.0; else if (lon < -180.0) lon += 360.0;
                const bool lit = sunlit(Vec3d{ rx2, ry2, rz2 }, sunDirection, earth.radiusKm);
                pd.title = e.name; pd.tc = e.color;
                pd.sub = std::string(kindName(e.kind)) + (e.group.empty() ? "" : "  ·  " + e.group);
                addSection("POSITION");
                addRow("Altitude", fmtDistKm(opts, e.altKm));
                addRow("Speed", opts.units == 1 ? std::string(TextFormat("%.2f mi/s", e.orbit.speedAt(r) * 0.621371)) : std::string(TextFormat("%.2f km/s", e.orbit.speedAt(r))));
                addRow("Lat / Lon", TextFormat("%.2f° %c   %.2f° %c", std::fabs(lat), lat >= 0 ? 'N' : 'S', std::fabs(lon), lon >= 0 ? 'E' : 'W'));
                addRow("Lighting", lit ? "Sunlit" : "In Earth's shadow", lit ? Color{ 255, 232, 150, 255 } : Color{ 150, 160, 190, 255 });
                addSection("ORBIT");
                addRow("Class", classNames[e.oclass], classCol[e.oclass]);
                addRow("Period", TextFormat("%.1f min", e.orbit.period() / 60.0));
                addRow("Perigee", fmtDistKm(opts, e.orbit.a * (1.0 - e.orbit.e) - earth.radiusKm));
                addRow("Apogee", fmtDistKm(opts, e.orbit.a * (1.0 + e.orbit.e) - earth.radiusKm));
                addRow("Inclination", TextFormat("%.2f°", e.orbit.inc / DEG));
                addRow("Eccentricity", TextFormat("%.4f", e.orbit.e));
                if (e.revPerDay > 0) addRow("Revolutions", TextFormat("%.2f per day", e.revPerDay));
                addSection("CATALOGUE");
                if (e.norad) addRow("NORAD ID", TextFormat("%d", e.norad));
                if (!e.intl.empty()) addRow("Int'l designator", TextFormat("%s  (launched %d)", e.intl.c_str(), e.launchYear));
                if (e.rev) addRow("Orbit number", TextFormat("%d", e.rev));
                addRow("Elements age", TextFormat("%.1f days", std::fabs(simTime - e.orbit.epoch) / 86400.0));
                addRow("Propagator", e.orbit.useSgp4 ? "SGP4" : "Kepler + J2", e.orbit.useSgp4 ? positiveColor : Color{ 255, 200, 120, 255 });
                if (e.bstar != 0.0) addRow("B* drag term", TextFormat("%.2e", e.bstar));
                if (station.set) {
                    addSection("FROM YOUR STATION");
                    const Look lk = lookAngles(station, Vec3d{ rx2, ry2, rz2 }, earthRotationAngle, earth.radiusKm);
                    if (lk.el > 0) addRow("Now", TextFormat("VISIBLE  el %.0f°  az %.0f°  %s", lk.el / DEG, lk.az / DEG, fmtDistKm(opts, lk.range).c_str()), positiveColor);
                    else addRow("Now", TextFormat("below horizon (el %.0f°)", lk.el / DEG), { 150, 150, 150, 255 });
                    for (size_t i = 0; i < std::min<size_t>(passes.size(), 4); ++i) {
                        const Pass& ps = passes[i];
                        addRow(TextFormat("Pass %zu", i + 1), TextFormat("%s–%s  max %.0f°%s", fmtHm(ps.aos).c_str(), fmtHm(ps.los).c_str(), ps.maxEl / DEG, ps.vis ? "  visible" : ""), ps.vis ? positiveColor : textColor);
                    }
                }
            } else if (mode == Mode::Map && !selectedAc.empty()) {
                for (size_t i = 0; i < aircraftList.size(); ++i) {
                    if (aircraftList[i].hex != selectedAc) continue;
                    have = true;
                    const Aircraft& a = aircraftList[i];
                    const char* airline = airlineName(a.call);
                    const std::string typeName = aircraftTypeName(a.type);
                    pd.title = a.call.empty() ? a.hex : a.call;
                    pd.tc = a.hasAlt ? altColor(a.altFt) : WHITE;
                    pd.sub = airline[0] ? airline : (a.reg.empty() ? "Aircraft (ADS-B)" : "Aircraft  ·  " + a.reg);
                    addSection("AIRCRAFT");
                    addRow("Type", typeName.empty() ? (a.type.empty() ? "?" : a.type) : typeName + "  (" + a.type + ")");
                    if (!a.reg.empty()) addRow("Registration", a.reg);
                    if (hexCountry(a.hex)[0]) addRow("Registered in", hexCountry(a.hex));
                    addSection("FLIGHT");
                    addRow("Altitude", a.ground ? "On the ground" : a.hasAlt ? fmtAltFt(opts, a.altFt) : "?");
                    if (!a.ground) addRow("Vertical", a.vrate > 250 ? TextFormat("Climbing  %+.0f ft/min", a.vrate) : a.vrate < -250 ? TextFormat("Descending  %+.0f ft/min", a.vrate) : "Level flight", a.vrate > 250 ? Color{ 120, 230, 150, 255 } : a.vrate < -250 ? Color{ 255, 170, 90, 255 } : textColor);
                    addRow("Speed", fmtSpeedKt(opts, a.gs));
                    addRow("Heading", TextFormat("%.0f°", a.track));
                    addRow("Position", TextFormat("%.3f, %.3f", aircraftLatLon[i].first, aircraftLatLon[i].second));
                    addSection("TRANSPONDER");
                    addRow("ICAO hex", a.hex);
                    if (!a.squawk.empty()) addRow("Squawk", a.squawk == "7700" ? a.squawk + "  EMERGENCY" : a.squawk == "7600" ? a.squawk + "  radio failure" : a.squawk == "7500" ? a.squawk + "  hijack" : a.squawk,
                                              (a.squawk == "7700" || a.squawk == "7600" || a.squawk == "7500") ? Color{ 255, 90, 90, 255 } : textColor);
                    addRow("Data age", TextFormat("%.0f s", std::max(0.0, steadyNow - a.stamp)));
                    break;
                }
            } else if (mode == Mode::Map && selectedShip) {
                for (size_t i = 0; i < ships.size(); ++i) {
                    if (ships[i].mmsi != selectedShip) continue;
                    have = true;
                    const Vessel& v = ships[i];
                    const int cat = ais::category(v.shipType);
                    pd.title = v.name.empty() ? std::string(TextFormat("MMSI %d", v.mmsi)) : v.name;
                    pd.tc = v.shipType == 0 ? Color{ 170, 176, 188, 255 } : cat == 0 ? Color{ 90, 214, 120, 255 } : cat == 1 ? Color{ 242, 92, 82, 255 } : cat == 2 ? Color{ 92, 172, 255, 255 } : cat == 3 ? Color{ 255, 172, 62, 255 } : Color{ 200, 172, 255, 255 };
                    pd.sub = std::string(v.shipType ? ais::shipTypeName(v.shipType) : "Vessel (AIS)") + (ais::mmsiCountry(v.mmsi)[0] ? std::string("  ·  ") + ais::mmsiCountry(v.mmsi) : "");
                    addSection("VOYAGE");
                    addRow("Status", ais::navStatusName(v.navStat));
                    if (!v.dest.empty()) addRow("Destination", v.dest);
                    if (!v.eta.empty()) addRow("ETA", v.eta + "  (MM-DD HH:MM UTC)");
                    addSection("MOTION");
                    addRow("Speed", fmtSpeedKt(opts, v.sog));
                    addRow("Course over ground", TextFormat("%.0f°", v.cog));
                    if (v.heading < 360) addRow("Heading", TextFormat("%d°", v.heading));
                    addRow("Position", TextFormat("%.4f, %.4f", shipLatLon[i].first, shipLatLon[i].second));
                    addSection("VESSEL");
                    if (v.len > 0) addRow("Length × beam", opts.units == 0 ? std::string(TextFormat("%d × %d m", v.len, v.beam)) : std::string(TextFormat("%.0f × %.0f ft", v.len * 3.28084, v.beam * 3.28084)));
                    if (v.draught > 0) addRow("Draught", fmtLen(opts, v.draught));
                    addRow("MMSI", TextFormat("%d", v.mmsi));
                    if (v.imo) addRow("IMO number", TextFormat("%d", v.imo));
                    if (!v.callsign.empty()) addRow("Call sign", v.callsign);
                    addRow("Source", TextFormat("%s  ·  %.0f s old", v.src.c_str(), std::max(0.0, steadyNow - v.stamp)));
                    break;
                }
            } else if (mode == Mode::Solar && selBody >= 0) {
                have = true;
                const int i = selBody;
                const double T = (simTime / 86400.0 - 10957.5) / 36525.0;
                const Vec3d pp = bodyPos[i], e0 = bodyPos[2];
                const double r = std::sqrt(pp.x * pp.x + pp.y * pp.y + pp.z * pp.z);
                const double de = std::sqrt((pp.x - e0.x) * (pp.x - e0.x) + (pp.y - e0.y) * (pp.y - e0.y) + (pp.z - e0.z) * (pp.z - e0.z));
                pd.title = candName(i);
                pd.tc = i == BODY_SUN ? Color{ 255, 230, 150, 255 } : i == BODY_HALLEY ? Color{ 170, 255, 220, 255 } : Color{ PLANETS[i].r, PLANETS[i].g, PLANETS[i].b, 255 };
                pd.sub = i == BODY_SUN ? "G2V main-sequence star" : i == BODY_HALLEY ? "Periodic comet 1P/Halley" : i == 8 ? "Dwarf planet" : "Planet";
                if (i == BODY_SUN) {
                    addSection("STAR"); addRow("Radius", "695,700 km"); addRow("Distance", "1.00 AU  (Earth)"); addRow("Light time", "8.3 min to Earth");
                } else {
                    const KeplerEl el = i == BODY_HALLEY ? halleyBody.el : planetElements(i, T);
                    addSection("POSITION");
                    addRow("From Sun", TextFormat("%.3f AU", r));
                    addRow("", opts.units == 0 ? std::string(TextFormat("%.1f million km", r * AU_KM / 1e6)) : std::string(TextFormat("%.1f million mi", r * AU_KM * 0.621371 / 1e6)));
                    if (i != 2) { addRow("From Earth", TextFormat("%.3f AU", de)); addRow("Light time", TextFormat("%.1f min", de / LIGHT_AU_PER_MIN)); }
                    addSection("ORBIT");
                    addRow("Period", TextFormat("%.2f years", std::pow(el.a, 1.5)));
                    addRow("Semi-major axis", TextFormat("%.3f AU", el.a));
                    addRow("Eccentricity", TextFormat("%.4f", el.e));
                    addRow("Inclination", TextFormat("%.2f°", el.iDeg));
                    addRow("Orbital speed", TextFormat("%.1f km/s", 29.7847 * std::sqrt(std::max(0.0, 2.0 / r - 1.0 / el.a))));
                    if (i < NPLANETS) { addSection("BODY"); addRow("Radius", fmtDistKm(opts, PLANETS[i].radiusKm)); }
                }
            } else if (mode == Mode::Galaxy && selGal >= 0) {
                have = true;
                const GalObj& o = gal[selGal];
                const Vector3 dv = Vector3Subtract(o.pos, sunGal);
                const double ds = Vector3Length(dv), dgc = Vector3Length(o.pos);
                pd.title = o.name; pd.tc = o.col;
                pd.sub = o.kind == 0 ? "Star  ·  HYG catalogue" : o.kind == 2 ? "Galaxy" : "Landmark";
                addSection("DISTANCE");
                addRow("From the Sun", ds < 1.0 ? "-" : fmtParsec(ds));
                addRow("From the centre", fmtParsec(dgc));
                if (o.kind == 0) {
                    addSection("STAR");
                    addRow("Apparent magnitude", TextFormat("%.2f", o.mag));
                    addRow("B-V colour index", TextFormat("%.2f", o.bv));
                    const double tK = 4600.0 * (1.0 / (0.92 * o.bv + 1.7) + 1.0 / (0.92 * o.bv + 0.62));
                    addRow("Temperature", TextFormat("~%.0f K", tK));
                }
            }
            if (have) uiDrawPanel(pd, px, py0, 288);
        }

    };

    auto drawWeatherCard = [&]() {
        if (mode == Mode::Map && wxProbe) {
            const WeatherPoint w = wx.center();
            PanelData pd;
            pd.title = "Weather"; pd.tc = { 130, 200, 255, 255 };
            pd.sub = w.ok ? std::string(TextFormat("%.2f, %.2f  ·  %s", w.lat, w.lon, weatherCodeName(w.code))) : "loading...";
            if (w.ok) {
                pd.rows.push_back({ "Temperature", TextFormat("%s   (feels %s)", fmtTemp(opts, w.temp).c_str(), fmtTemp(opts, w.feels).c_str()), textColor, false });
                pd.rows.push_back({ "Wind", TextFormat("%s  from %.0f°", fmtSpeedKt(opts, w.windKt).c_str(), w.windDir), textColor, false });
                pd.rows.push_back({ "Gusts", fmtSpeedKt(opts, w.gustKt), w.gustKt > 35 ? Color{ 255, 170, 90, 255 } : textColor, false });
                pd.rows.push_back({ "Cloud cover", TextFormat("%.0f %%", w.cloud), textColor, false });
                pd.rows.push_back({ "Precipitation", TextFormat("%.1f mm", w.precip), w.precip > 0.1 ? Color{ 120, 190, 255, 255 } : textColor, false });
                pd.rows.push_back({ "Humidity", TextFormat("%.0f %%", w.humidity), textColor, false });
                pd.rows.push_back({ "Pressure", TextFormat("%.0f hPa", w.pressure), textColor, false });
            }
            uiDrawPanel(pd, 12, readoutBottomY + 6, 270);
        }

    };

    auto drawSkyPassPlanner = [&]() {
        if (mode == Mode::Sky && station.set) {
            if (skyPassVer != stationVer || std::fabs(simTime - skyPassT) > 1800.0) {
                skyPasses.clear();
                for (int ei : notable) for (const Pass& ps : computePasses(entities[ei], station, simTime, earth.radiusKm)) skyPasses.push_back({ ei, ps });
                std::sort(skyPasses.begin(), skyPasses.end(), [](const SkyPass& a, const SkyPass& b) { return a.ps.aos < b.ps.aos; });
                skyPassVer = stationVer; skyPassT = simTime;
            }
            std::vector<const SkyPass*> list;
            for (const SkyPass& sp : skyPasses) if (!skyVisOnly || sp.ps.vis) list.push_back(&sp);
            const int shown = (int)std::min<size_t>(list.size(), 9);
            const float pw = 318.0f, pxx = 12.0f, pyy = (float)readoutBottomY + 8.0f, ph2 = 54.0f + std::max(1, shown) * 24.0f;
            uiShadowBox({ pxx, pyy, pw, ph2 }, 0.8f);
            ui.region({ pxx, pyy, pw, ph2 });
            UiText("Next passes over your station", (int)pxx + 14, (int)pyy + 9, 15, WHITE);
            UiText("UTC  ·  click to jump to the pass", (int)pxx + 14, (int)pyy + 29, 11, mutedColor);
            if (list.empty()) UiText(skyVisOnly ? "no visible passes in the next 24 h" : "no passes in the next 24 h", (int)pxx + 14, (int)pyy + 54, 13, mutedColor);
            for (int i = 0; i < shown; ++i) {
                const SkyPass& sp = *list[i];
                const Rectangle row{ pxx + 6, pyy + 50.0f + i * 24.0f, pw - 12, 22 };
                const bool h = ui.hover(row);
                if (h) DrawRectangleRounded(row, 0.25f, 4, Fade(WHITE, 0.08f));
                if (ui.clicked(row)) { simTime = sp.ps.aos - 30.0; selected = sp.ent; follow = true; }
                const Color c = sp.ps.vis ? positiveColor : textColor;
                UiText(TextFormat("%s", fmtHm(sp.ps.aos).c_str()), (int)row.x + 8, (int)row.y + 4, 13, c);
                UiText(entities[sp.ent].name.substr(0, 16).c_str(), (int)row.x + 52, (int)row.y + 4, 13, c);
                UiText(TextFormat("%.0f°%s", sp.ps.maxEl / DEG, sp.ps.vis ? " vis" : ""), (int)row.x + 218, (int)row.y + 4, 13, c);
                UiText(TextFormat("%.0f min", (sp.ps.los - sp.ps.aos) / 60.0), (int)row.x + 262, (int)row.y + 4, 12, mutedColor);
            }
        }

    };

    auto drawTimeDialog = [&]() {
        if (timeOpen) {
            const float dw = 430.0f, dx = (screenWidth - dw) * 0.5f, dy = toolbarHeight + 12.0f;
            uiShadowBox({ dx, dy, dw, 118.0f }, 0.95f);
            ui.region({ dx, dy, dw, 118.0f });
            UiText("Jump to date and time (UTC)", (int)dx + 16, (int)dy + 10, 16, WHITE);
            DrawRectangleRounded({ dx + 16, dy + 38, dw - 32, 30 }, 0.2f, 4, { 28, 34, 50, 255 });
            DrawRectangleRoundedLines({ dx + 16, dy + 38, dw - 32, 30 }, 0.2f, 4, Fade({ 255, 220, 90, 255 }, 0.8f));
            UiText(TextFormat("%s%s", timeText.c_str(), ((int)(GetTime() * 2) % 2) ? "_" : " "), (int)dx + 26, (int)dy + 43, 18, WHITE);
            UiText("YYYY-MM-DD HH:MM  then Enter", (int)dx + 16, (int)dy + 72, 11, mutedColor);
            float bxx = dx + 16;
            auto addTimeStepButton = [&](const char* label, double dsec) { if (ui.button({ bxx, dy + 86, 60, 24 }, label)) { simTime += dsec; timeText = fmtClock(simTime, false).substr(0, 16); } bxx += 64; };
            addTimeStepButton("-1 d", -86400); addTimeStepButton("-1 h", -3600); addTimeStepButton("+1 h", 3600); addTimeStepButton("+1 d", 86400);
            if (ui.button({ bxx, dy + 86, 60, 24 }, "Now")) { simTime = nowUnix(); timeText = fmtClock(simTime, false).substr(0, 16); }
        }

    };

    auto drawHoverTooltip = [&]() {
        if (!ui.overUi && !searching) {
            if (hovered >= 0 && satMode) { hoverTitle = entities[hovered].name; hoverDetail = TextFormat("%s · %.2f km/s", fmtDistKm(opts, entities[hovered].altKm).c_str(), entities[hovered].orbit.speedAt(entities[hovered].altKm + earth.radiusKm)); }
            else if (hoveredAc >= 0) { const Aircraft& a = aircraftList[hoveredAc]; hoverTitle = a.call.empty() ? a.hex : a.call; hoverDetail = std::string(aircraftTypeName(a.type)[0] ? aircraftTypeName(a.type) : a.type.c_str()) + (a.hasAlt ? "  ·  " + fmtAltFt(opts, a.altFt) : ""); }
            else if (hoveredShip >= 0) { const Vessel& v = ships[hoveredShip]; hoverTitle = v.name.empty() ? std::string(TextFormat("MMSI %d", v.mmsi)) : v.name; hoverDetail = std::string(ais::shipTypeName(v.shipType)) + "  ·  " + fmtSpeedKt(opts, v.sog); }
            else if (hovBody >= 0) { hoverTitle = candName(hovBody); hoverDetail = "click for details"; }
            else if (hovGal >= 0) { hoverTitle = gal[hovGal].name; hoverDetail = fmtParsec(Vector3Length(Vector3Subtract(gal[hovGal].pos, sunGal))); }
            if (!hoverTitle.empty()) {
                const int tw2 = std::max(UiMeasure(hoverTitle.c_str(), 14), UiMeasure(hoverDetail.c_str(), 12)) + 18;
                const float tx2 = std::min(mouse.x + 16, screenWidth - tw2 - 8.0f), ty2 = std::min(mouse.y + 18, screenHeight - 60.0f);
                uiShadowBox({ tx2, ty2, (float)tw2, 42 }, 0.9f);
                UiText(hoverTitle.c_str(), (int)tx2 + 9, (int)ty2 + 5, 14, WHITE);
                UiText(hoverDetail.c_str(), (int)tx2 + 9, (int)ty2 + 23, 12, mutedColor);
            }
        }

    };

    auto drawSearchOverlay = [&]() {
        if (searching) {
            const int w = 360, x = GetScreenWidth() / 2 - w / 2, y = 46;
            const int shown = (int)std::min<size_t>(matches.size(), 8);
            const int h = 40 + shown * 22 + (query.empty() || matches.empty() ? 0 : 8);
            DrawRectangle(x, y, w, h, Fade(BLACK, 0.8f));
            DrawRectangleLines(x, y, w, h, { 255, 220, 90, 255 });
            UiText(TextFormat("Search %s: %s%s", mode == Mode::Solar ? "planets" : mode == Mode::Galaxy ? "stars" : "satellites", query.c_str(), ((int)(GetTime() * 2) % 2) ? "_" : " "), x + 10, y + 10, 20, RAYWHITE);
            for (int i = 0; i < shown; ++i) {
                if (i == matchSel) DrawRectangle(x + 4, y + 38 + i * 22, w - 8, 22, Fade({ 255, 220, 90, 255 }, 0.25f));
                const std::string nm = candName(matches[i]);
                const bool sat = mode == Mode::Globe || mode == Mode::Map;
                UiText(TextFormat("%s%s", nm.c_str(), sat && entities[matches[i]].norad ? TextFormat("   #%d", entities[matches[i]].norad) : ""), x + 12, y + 41 + i * 22, 16, candColor(matches[i]));
            }
            if (!query.empty() && matches.empty()) UiText("no matches", x + 12, y + 41, 16, GRAY);
            else if (matches.size() > 8) UiText(TextFormat("+%zu more", matches.size() - 8), x + w - 90, y + 14, 14, GRAY);
        }
    };

    while (!WindowShouldClose()) {
        beginFrame();
        if (reportBenchmark()) break;
        swapInHighResolutionEarth();
        if (runScriptedTour()) break;
        if (handleKeyboardAndSearch()) break;
        handleMouseNavigation();
        if (!paused) simTime += frameSeconds * timeScale;

        updateSatellitePositions();
        updateAstronomy();
        updateCameraAndObserverFrame();
        projectSatellitesAndFindHover();
        refreshMapDataLayers();
        pickSolarAndGalaxyObjects();
        handleClicks();
        updatePassPredictions();
        BeginDrawing();
        ClearBackground(mode == Mode::Galaxy ? Color{ 1, 1, 4, 255 } : Color{ 2, 3, 8, 255 });
        tiles.beginFrame((unsigned)frame);


        switch (mode) {
            case Mode::Globe: drawGlobeScene(); break;
            case Mode::Map: drawMapScene(); break;
            case Mode::Solar: drawSolarSystemScene(); break;
            case Mode::Sky: drawSkyScene(); break;
            case Mode::Galaxy: drawGalaxyScene(); break;
        }
        drawSatelliteOverlay();
        drawGlobeCities();
        if (viewFade > 0.0f) {
            DrawRectangle(0, 0, (int)screenWidth, (int)screenHeight, Fade(BLACK, viewFade));
            viewFade = std::max(0.0f, viewFade - frameSeconds * 2.6f);
        }
        drawToolbar();
        drawTopLeftReadout();
        drawLayersPopup();
        drawSettingsPopup();
        drawHelpOverlay();
        drawMapOverlays();
        drawStatusBarAndHints();
        drawInfoPanels();
        drawWeatherCard();
        drawSkyPassPlanner();
        drawTimeDialog();
        drawHoverTooltip();
        drawSearchOverlay();
        ui.drawTooltip();
        ui.end();
        if (!pendingShot.empty()) { saveShot(pendingShot); pendingShot.clear(); }
        if (IsKeyPressed(KEY_F12)) saveShot("spacemap.png");
        EndDrawing();
    }

    CloseWindow();
    std::fflush(stdout);
    std::_Exit(0);
}
