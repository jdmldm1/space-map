#pragma once
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

struct CodeName { const char* code; const char* name; };

inline const char* lookup(const CodeName* t, size_t n, const std::string& code) {
    for (size_t i = 0; i < n; ++i) if (code == t[i].code) return t[i].name;
    return "";
}

inline const char* airlineName(const std::string& callsign) {
    static const CodeName t[] = {
        { "UAL", "United Airlines" }, { "DAL", "Delta Air Lines" }, { "AAL", "American Airlines" }, { "SWA", "Southwest Airlines" }, { "JBU", "JetBlue" },
        { "ASA", "Alaska Airlines" }, { "NKS", "Spirit Airlines" }, { "FFT", "Frontier Airlines" }, { "HAL", "Hawaiian Airlines" }, { "SKW", "SkyWest" },
        { "RPA", "Republic Airways" }, { "ENY", "Envoy Air" }, { "EDV", "Endeavor Air" }, { "PDT", "Piedmont Airlines" }, { "FDX", "FedEx Express" }, { "UPS", "UPS Airlines" },
        { "GTI", "Atlas Air" }, { "ACA", "Air Canada" }, { "WJA", "WestJet" }, { "BAW", "British Airways" }, { "VIR", "Virgin Atlantic" }, { "EZY", "easyJet" },
        { "RYR", "Ryanair" }, { "DLH", "Lufthansa" }, { "AFR", "Air France" }, { "KLM", "KLM" }, { "IBE", "Iberia" }, { "TAP", "TAP Air Portugal" },
        { "SAS", "Scandinavian Airlines" }, { "FIN", "Finnair" }, { "NAX", "Norwegian Air Shuttle" }, { "SWR", "Swiss" }, { "AUA", "Austrian" }, { "BEL", "Brussels Airlines" },
        { "EIN", "Aer Lingus" }, { "THY", "Turkish Airlines" }, { "PGT", "Pegasus" }, { "AEE", "Aegean" }, { "ITY", "ITA Airways" }, { "VLG", "Vueling" }, { "WZZ", "Wizz Air" },
        { "UAE", "Emirates" }, { "ETD", "Etihad" }, { "QTR", "Qatar Airways" }, { "SVA", "Saudia" }, { "ELY", "El Al" }, { "MSR", "EgyptAir" }, { "ETH", "Ethiopian Airlines" },
        { "SAA", "South African Airways" }, { "SIA", "Singapore Airlines" }, { "CPA", "Cathay Pacific" }, { "ANA", "All Nippon Airways" }, { "JAL", "Japan Airlines" },
        { "KAL", "Korean Air" }, { "AAR", "Asiana" }, { "CCA", "Air China" }, { "CES", "China Eastern" }, { "CSN", "China Southern" }, { "HDA", "Dragonair" },
        { "AIC", "Air India" }, { "IGO", "IndiGo" }, { "THA", "Thai Airways" }, { "MAS", "Malaysia Airlines" }, { "GIA", "Garuda Indonesia" }, { "PAL", "Philippine Airlines" },
        { "QFA", "Qantas" }, { "ANZ", "Air New Zealand" }, { "VOZ", "Virgin Australia" }, { "LAN", "LATAM" }, { "TAM", "LATAM Brasil" }, { "AVA", "Avianca" },
        { "AMX", "Aeromexico" }, { "VOI", "Volaris" }, { "CMP", "Copa Airlines" }, { "GLO", "Gol" }, { "AZU", "Azul" }, { "RCH", "US Air Force (AMC)" }, { "CNV", "US Navy" },
        { "PAT", "US Army" }, { "NOW", "Netjets Europe" }, { "EJA", "NetJets" }, { "LXJ", "Flexjet" }, { "CJT", "Cargojet" }, { "ABX", "ABX Air" }, { "CLX", "Cargolux" },
        { "BOX", "AeroLogic" }, { "DHL", "DHL" }, { "GEC", "Lufthansa Cargo" }, { "UPS", "UPS Airlines" }, { "FDX", "FedEx Express" },
    };
    if (callsign.size() < 4) return "";
    for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); ++i) if (std::strncmp(callsign.c_str(), t[i].code, 3) == 0 && std::isdigit((unsigned char)callsign[3])) return t[i].name;
    return "";
}

inline const char* aircraftTypeName(const std::string& icao) {
    static const CodeName t[] = {
        { "A318", "Airbus A318" }, { "A319", "Airbus A319" }, { "A320", "Airbus A320" }, { "A321", "Airbus A321" }, { "A20N", "Airbus A320neo" }, { "A21N", "Airbus A321neo" },
        { "A19N", "Airbus A319neo" }, { "A332", "Airbus A330-200" }, { "A333", "Airbus A330-300" }, { "A338", "Airbus A330-800" }, { "A339", "Airbus A330-900" },
        { "A342", "Airbus A340-200" }, { "A343", "Airbus A340-300" }, { "A346", "Airbus A340-600" }, { "A359", "Airbus A350-900" }, { "A35K", "Airbus A350-1000" },
        { "A388", "Airbus A380-800" }, { "A306", "Airbus A300-600" }, { "A310", "Airbus A310" }, { "A225", "Antonov An-225" }, { "A124", "Antonov An-124" },
        { "B712", "Boeing 717" }, { "B722", "Boeing 727-200" }, { "B732", "Boeing 737-200" }, { "B733", "Boeing 737-300" }, { "B734", "Boeing 737-400" }, { "B735", "Boeing 737-500" },
        { "B736", "Boeing 737-600" }, { "B737", "Boeing 737-700" }, { "B738", "Boeing 737-800" }, { "B739", "Boeing 737-900" }, { "B37M", "Boeing 737 MAX 7" },
        { "B38M", "Boeing 737 MAX 8" }, { "B39M", "Boeing 737 MAX 9" }, { "B3XM", "Boeing 737 MAX 10" }, { "B744", "Boeing 747-400" }, { "B748", "Boeing 747-8" },
        { "B74S", "Boeing 747SP" }, { "B752", "Boeing 757-200" }, { "B753", "Boeing 757-300" }, { "B762", "Boeing 767-200" }, { "B763", "Boeing 767-300" }, { "B764", "Boeing 767-400" },
        { "B772", "Boeing 777-200" }, { "B77L", "Boeing 777-200LR" }, { "B77W", "Boeing 777-300ER" }, { "B773", "Boeing 777-300" }, { "B778", "Boeing 777-8" },
        { "B779", "Boeing 777-9" }, { "B788", "Boeing 787-8" }, { "B789", "Boeing 787-9" }, { "B78X", "Boeing 787-10" }, { "BCS1", "Airbus A220-100" }, { "BCS3", "Airbus A220-300" },
        { "E170", "Embraer E170" }, { "E75L", "Embraer E175" }, { "E75S", "Embraer E175" }, { "E190", "Embraer E190" }, { "E195", "Embraer E195" }, { "E290", "Embraer E190-E2" },
        { "E295", "Embraer E195-E2" }, { "E145", "Embraer ERJ-145" }, { "E135", "Embraer ERJ-135" }, { "E50P", "Embraer Phenom 300" }, { "E55P", "Embraer Praetor 500" },
        { "CRJ2", "Bombardier CRJ-200" }, { "CRJ7", "Bombardier CRJ-700" }, { "CRJ9", "Bombardier CRJ-900" }, { "CRJX", "Bombardier CRJ-1000" }, { "DH8D", "Dash 8 Q400" },
        { "DH8C", "Dash 8-300" }, { "DH8A", "Dash 8-100" }, { "AT72", "ATR 72" }, { "AT76", "ATR 72-600" }, { "AT75", "ATR 72-500" }, { "AT45", "ATR 42-500" }, { "AT46", "ATR 42-600" },
        { "SF34", "Saab 340" }, { "SB20", "Saab 2000" }, { "MD11", "McDonnell Douglas MD-11" }, { "MD82", "McDonnell Douglas MD-82" }, { "MD83", "McDonnell Douglas MD-83" },
        { "MD88", "McDonnell Douglas MD-88" }, { "MD90", "McDonnell Douglas MD-90" }, { "DC10", "McDonnell Douglas DC-10" }, { "C130", "Lockheed C-130 Hercules" },
        { "C17", "Boeing C-17 Globemaster III" }, { "C5M", "Lockheed C-5M Galaxy" }, { "K35R", "Boeing KC-135 Stratotanker" }, { "KC10", "McDonnell Douglas KC-10" },
        { "C172", "Cessna 172" }, { "C182", "Cessna 182" }, { "C208", "Cessna 208 Caravan" }, { "C25A", "Cessna Citation CJ2" }, { "C25B", "Cessna Citation CJ3" },
        { "C510", "Cessna Citation Mustang" }, { "C525", "Cessna CitationJet" }, { "C550", "Cessna Citation II" }, { "C560", "Cessna Citation V" }, { "C56X", "Cessna Citation Excel" },
        { "C680", "Cessna Citation Sovereign" }, { "C700", "Cessna Citation Longitude" }, { "C750", "Cessna Citation X" }, { "PC12", "Pilatus PC-12" }, { "PC24", "Pilatus PC-24" },
        { "SR22", "Cirrus SR22" }, { "SR20", "Cirrus SR20" }, { "PA28", "Piper PA-28" }, { "PA46", "Piper Malibu" }, { "BE20", "Beechcraft King Air 200" }, { "BE9L", "Beechcraft King Air 90" },
        { "BE58", "Beechcraft Baron" }, { "TBM9", "Daher TBM 900" }, { "TBM8", "Daher TBM 850" }, { "GLF4", "Gulfstream IV" }, { "GLF5", "Gulfstream V" }, { "GLF6", "Gulfstream G650" },
        { "GLEX", "Bombardier Global Express" }, { "GL5T", "Bombardier Global 5000" }, { "GL7T", "Bombardier Global 7500" }, { "CL60", "Bombardier Challenger 600" },
        { "CL35", "Bombardier Challenger 350" }, { "LJ45", "Learjet 45" }, { "LJ35", "Learjet 35" }, { "FA7X", "Dassault Falcon 7X" }, { "FA50", "Dassault Falcon 50" },
        { "F900", "Dassault Falcon 900" }, { "H25B", "Hawker 800" }, { "R44", "Robinson R44" }, { "R22", "Robinson R22" }, { "EC35", "Eurocopter EC135" }, { "A139", "AgustaWestland AW139" },
        { "S76", "Sikorsky S-76" }, { "B06", "Bell 206 JetRanger" }, { "B407", "Bell 407" }, { "B412", "Bell 412" }, { "H60", "Sikorsky UH-60 Black Hawk" },
        { "SU95", "Sukhoi Superjet 100" }, { "RJ85", "Avro RJ85" }, { "B461", "BAe 146-100" }, { "B463", "BAe 146-300" }, { "F100", "Fokker 100" }, { "F70", "Fokker 70" },
        { "CONC", "Concorde" }, { "GLID", "Glider" }, { "BALL", "Balloon" }, { "ULAC", "Ultralight" },
    };
    return lookup(t, sizeof(t) / sizeof(t[0]), icao);
}

inline const char* hexCountry(const std::string& hex) {
    if (hex.size() < 6) return "";
    const unsigned long v = std::strtoul(hex.c_str(), nullptr, 16);
    struct Block { unsigned long lo, hi; const char* c; };
    static const Block b[] = {
        { 0xA00000, 0xAFFFFF, "United States" }, { 0x400000, 0x43FFFF, "United Kingdom" }, { 0x3C0000, 0x3FFFFF, "Germany" }, { 0x380000, 0x3BFFFF, "France" },
        { 0x300000, 0x33FFFF, "Italy" }, { 0x340000, 0x37FFFF, "Spain" }, { 0x480000, 0x487FFF, "Netherlands" }, { 0x440000, 0x447FFF, "Austria" }, { 0x448000, 0x44FFFF, "Belgium" },
        { 0x458000, 0x45FFFF, "Denmark" }, { 0x460000, 0x467FFF, "Finland" }, { 0x468000, 0x46FFFF, "Greece" }, { 0x478000, 0x47FFFF, "Norway" }, { 0x488000, 0x48FFFF, "Poland" },
        { 0x490000, 0x497FFF, "Portugal" }, { 0x4A0000, 0x4A7FFF, "Sweden" }, { 0x4B0000, 0x4B7FFF, "Switzerland" }, { 0x4B8000, 0x4BFFFF, "Turkey" }, { 0x4CA000, 0x4CAFFF, "Ireland" },
        { 0x100000, 0x1FFFFF, "Russia" }, { 0x780000, 0x7BFFFF, "China" }, { 0x7C0000, 0x7FFFFF, "Australia" }, { 0x800000, 0x83FFFF, "India" }, { 0x840000, 0x87FFFF, "Japan" },
        { 0x880000, 0x887FFF, "Thailand" }, { 0x894000, 0x894FFF, "Qatar" }, { 0x896000, 0x896FFF, "United Arab Emirates" }, { 0x710000, 0x717FFF, "Saudi Arabia" },
        { 0x738000, 0x73FFFF, "Israel" }, { 0x70C000, 0x70C7FF, "Oman" }, { 0x718000, 0x71FFFF, "South Korea" }, { 0xC00000, 0xC3FFFF, "Canada" }, { 0x0D0000, 0x0D7FFF, "Mexico" },
        { 0xE00000, 0xE3FFFF, "Argentina" }, { 0xE40000, 0xE7FFFF, "Brazil" }, { 0xE80000, 0xE80FFF, "Chile" }, { 0xC80000, 0xC87FFF, "New Zealand" }, { 0x008000, 0x00FFFF, "South Africa" },
        { 0x018000, 0x01FFFF, "Egypt" }, { 0x010000, 0x017FFF, "Egypt" }, { 0x060000, 0x067FFF, "Morocco" }, { 0x0A0000, 0x0A7FFF, "Algeria" }, { 0x500000, 0x5003FF, "Albania" },
        { 0x4D0000, 0x4D03FF, "Luxembourg" }, { 0x4A8000, 0x4AFFFF, "Czechia" }, { 0x498000, 0x49FFFF, "Czechia" }, { 0x4C0000, 0x4C7FFF, "Serbia" }, { 0x508000, 0x50FFFF, "Ukraine" },
        { 0x728000, 0x72FFFF, "Iraq" }, { 0x730000, 0x737FFF, "Iran" }, { 0x740000, 0x747FFF, "Jordan" }, { 0x750000, 0x757FFF, "Malaysia" }, { 0x758000, 0x75FFFF, "Philippines" },
        { 0x760000, 0x767FFF, "Pakistan" }, { 0x768000, 0x76FFFF, "Singapore" }, { 0x770000, 0x777FFF, "Sri Lanka" }, { 0x888000, 0x88FFFF, "Vietnam" }, { 0x8A0000, 0x8A7FFF, "Indonesia" },
    };
    for (const Block& k : b) if (v >= k.lo && v <= k.hi) return k.c;
    return "";
}

inline const char* satGroup(const std::string& n) {
    auto has = [&](const char* k) { return n.find(k) != std::string::npos; };
    if (has("STARLINK")) return "Starlink (SpaceX)";
    if (has("ONEWEB")) return "OneWeb";
    if (has("IRIDIUM")) return "Iridium NEXT";
    if (has("GLOBALSTAR")) return "Globalstar";
    if (has("ORBCOMM")) return "Orbcomm";
    if (has("KUIPER")) return "Project Kuiper";
    if (has("NAVSTAR") || has("GPS ")) return "GPS (USA)";
    if (has("GALILEO")) return "Galileo (EU)";
    if (has("BEIDOU") || has("COMPASS")) return "BeiDou (China)";
    if (has("GLONASS")) return "GLONASS (Russia)";
    if (has("TDRS")) return "NASA TDRS relay";
    if (has("GOES")) return "NOAA GOES weather";
    if (has("NOAA")) return "NOAA weather";
    if (has("METOP")) return "EUMETSAT MetOp";
    if (has("METEOSAT")) return "EUMETSAT Meteosat";
    if (has("HIMAWARI")) return "JMA Himawari";
    if (has("LANDSAT")) return "USGS/NASA Landsat";
    if (has("SENTINEL")) return "ESA Sentinel (Copernicus)";
    if (has("TERRA") || has("AQUA")) return "NASA Earth Observing";
    if (has("HST")) return "NASA/ESA Hubble";
    if (has("ISS") || has("ZARYA")) return "International Space Station";
    if (has("CSS") || has("TIANHE")) return "Chinese Space Station";
    if (has("COSMOS")) return "Russian (Cosmos)";
    if (has("YAOGAN")) return "China (Yaogan)";
    if (has("SHENZHOU") || has("TIANZHOU")) return "China crewed programme";
    if (has("DRAGON")) return "SpaceX Dragon";
    if (has("SOYUZ") || has("PROGRESS")) return "Roscosmos";
    if (has("CYGNUS")) return "Northrop Grumman Cygnus";
    return "";
}
