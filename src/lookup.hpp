#pragma once
// "Learn more" lookups for the selected satellite, aircraft, ship or mission, run on one background thread.
//   * Wikipedia (public API, no key): article summary, link and lead photo. A result is only accepted when its title
//     contains one of the caller's match words and its text mentions one of the caller's keywords, so a ship called
//     "Aurora" does not turn into the article about the aurora.
//   * Planespotters.net (public photo API, no key): a photo of the actual airframe by ICAO hex, with credit and link.
//   * adsbdb.com (public, no key): aircraft owner / manufacturer / photo and the flight route for a callsign.
//   * CelesTrak SATCAT + SatNOGS DB (public, no key): satellite owner, launch site, status, radio frequencies and photo.
//   * Wikidata (public, no key): ship facts (builder, operator, tonnage ...) found by IMO number.
// Results, including misses, are cached on disk under info/ so each object is looked up once.
#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "net.hpp"

struct InfoCard {
    bool ready = false, found = false;
    std::string summary, wikiUrl, imageFile, credit, creditUrl;
    std::vector<std::pair<std::string, std::string>> facts;  // label, value
    std::vector<std::pair<std::string, std::string>> links;  // label, url (extra links the lookup discovered)
    bool hasRoute = false;                                    // flight route of an aircraft
    std::string fromName, toName;
    double fromLat = 0, fromLon = 0, toLat = 0, toLon = 0;
};

struct InfoRequest {
    std::string key;                    // unique per object, e.g. "sat:25544"
    std::vector<std::string> queries;   // Wikipedia search phrases, best first
    std::vector<std::string> match;     // lower-case words; the article title must contain one
    std::vector<std::string> keywords;  // lower-case words; the article text must contain one (empty = any)
    std::string photoHex;               // aircraft ICAO hex for Planespotters
    std::string kind;                   // "sat", "ac" or "ship": which extra sources to ask
    std::string norad, cospar, imo, callsign;
    std::string fallbackImage, fallbackCredit;  // set by the lookup itself, used when Wikipedia has no photo
};

namespace webinfo {

inline std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
        else if (c == ' ') o += '+';
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}

inline std::string pathEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}

inline std::string lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }

// A small JSON reader, enough for the two APIs above.
struct JV {
    char t = 'n';  // n null, s string, d number, o object, a array, b bool
    std::string s;
    double d = 0;
    std::vector<std::pair<std::string, JV>> o;
    std::vector<JV> a;
    const JV* get(const char* k) const { for (const auto& p : o) if (p.first == k) return &p.second; return nullptr; }
    std::string str(const char* k) const { const JV* v = get(k); return v && v->t == 's' ? v->s : std::string(); }
    double num(const char* k, double fallback = 0) const { const JV* v = get(k); return v && v->t == 'd' ? v->d : fallback; }
};

struct JParser {
    const std::string& s;
    size_t i = 0;
    explicit JParser(const std::string& text) : s(text) {}
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i; }
    static void utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 63)); }
        else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63)); }
        else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 63)); o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63)); }
    }
    unsigned hex4() { unsigned v = 0; for (int k = 0; k < 4 && i < s.size(); ++k, ++i) { const char c = s[i]; v = v * 16 + (unsigned)(std::isdigit((unsigned char)c) ? c - '0' : (std::tolower(c) - 'a' + 10)); } return v; }
    bool str(std::string& out) {
        if (i >= s.size() || s[i] != '"') return false;
        ++i;
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) return false;
            c = s[i++];
            switch (c) {
                case 'n': out += '\n'; break; case 't': out += '\t'; break; case 'r': break; case 'b': case 'f': break;
                case 'u': { unsigned cp = hex4(); if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') { i += 2; const unsigned lo = hex4(); cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); } utf8(out, cp); break; }
                default: out += c;
            }
        }
        if (i >= s.size()) return false;
        ++i;
        return true;
    }
    bool value(JV& v, int depth = 0) {
        if (depth > 24) return false;
        ws();
        if (i >= s.size()) return false;
        const char c = s[i];
        if (c == '"') { v.t = 's'; return str(v.s); }
        if (c == '{') {
            v.t = 'o'; ++i; ws();
            if (i < s.size() && s[i] == '}') { ++i; return true; }
            while (i < s.size()) {
                ws(); std::string k; if (!str(k)) return false; ws();
                if (i >= s.size() || s[i] != ':') return false;
                ++i; JV child; if (!value(child, depth + 1)) return false;
                v.o.emplace_back(std::move(k), std::move(child)); ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; return true; }
                return false;
            }
            return false;
        }
        if (c == '[') {
            v.t = 'a'; ++i; ws();
            if (i < s.size() && s[i] == ']') { ++i; return true; }
            while (i < s.size()) {
                JV child; if (!value(child, depth + 1)) return false;
                v.a.push_back(std::move(child)); ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; return true; }
                return false;
            }
            return false;
        }
        if (s.compare(i, 4, "true") == 0) { v.t = 'b'; v.d = 1; i += 4; return true; }
        if (s.compare(i, 5, "false") == 0) { v.t = 'b'; i += 5; return true; }
        if (s.compare(i, 4, "null") == 0) { i += 4; return true; }
        char* end = nullptr;
        v.d = std::strtod(s.c_str() + i, &end);
        if (end == s.c_str() + i) return false;
        v.t = 'd'; i = (size_t)(end - s.c_str());
        return true;
    }
};

inline bool parseJson(const std::string& text, JV& out) { JParser p(text); return p.value(out); }

inline std::string imageExtension(const std::string& url) {
    const std::string u = lower(url.substr(0, url.find('?')));
    for (const char* e : { ".jpg", ".jpeg", ".png" }) { const std::string ext = e; if (u.size() > ext.size() && u.compare(u.size() - ext.size(), ext.size(), ext) == 0) return ext; }
    return "";
}


inline std::string ownerName(const std::string& c) {
    static const std::pair<const char*, const char*> t[] = {
        { "US", "United States" }, { "CIS", "Russia / former USSR" }, { "PRC", "China" }, { "JPN", "Japan" }, { "FR", "France" }, { "IND", "India" }, { "ESA", "European Space Agency" },
        { "GER", "Germany" }, { "UK", "United Kingdom" }, { "ITA", "Italy" }, { "CAN", "Canada" }, { "ISS", "International Space Station partners" }, { "SES", "SES (Luxembourg)" },
        { "ITSO", "Intelsat" }, { "EUTE", "Eutelsat" }, { "O3B", "O3b Networks" }, { "ORB", "Orbcomm" }, { "GLOB", "Globalstar" }, { "IRID", "Iridium" }, { "AB", "Arab Satellite Org." },
        { "ISRA", "Israel" }, { "KOR", "South Korea" }, { "ROC", "Taiwan" }, { "SKOR", "South Korea" }, { "AUS", "Australia" }, { "BRAZ", "Brazil" }, { "ARGN", "Argentina" }, { "MEX", "Mexico" },
        { "SPN", "Spain" }, { "NOR", "Norway" }, { "SWED", "Sweden" }, { "FIN", "Finland" }, { "TURK", "Turkey" }, { "UAE", "United Arab Emirates" }, { "SAUD", "Saudi Arabia" },
        { "IRAN", "Iran" }, { "INDO", "Indonesia" }, { "THAI", "Thailand" }, { "VTNM", "Vietnam" }, { "NZ", "New Zealand" }, { "POL", "Poland" }, { "UKR", "Ukraine" }, { "EGYP", "Egypt" },
        { "NIG", "Nigeria" }, { "SAFR", "South Africa" }, { "NATO", "NATO" }, { "EUME", "EUMETSAT" }, { "GLOB", "Globalstar" }, { "TBD", "Unknown" },
    };
    for (const auto& e : t) if (c == e.first) return e.second;
    return c;
}

inline std::string launchSiteName(const std::string& c) {
    static const std::pair<const char*, const char*> t[] = {
        { "AFETR", "Cape Canaveral, Florida (USA)" }, { "AFWTR", "Vandenberg, California (USA)" }, { "TYMSC", "Baikonur Cosmodrome (Kazakhstan)" }, { "PLMSC", "Plesetsk Cosmodrome (Russia)" },
        { "FRGUI", "Kourou, French Guiana" }, { "JSC", "Jiuquan (China)" }, { "TSC", "Taiyuan (China)" }, { "XSC", "Xichang (China)" }, { "WSC", "Wenchang (China)" },
        { "SRILR", "Satish Dhawan, Sriharikota (India)" }, { "TNSTA", "Tanegashima (Japan)" }, { "KSCUT", "Uchinoura (Japan)" }, { "WLPIS", "Wallops Island, Virginia (USA)" },
        { "KWAJ", "Kwajalein Atoll (Marshall Islands)" }, { "VOSTO", "Vostochny Cosmodrome (Russia)" }, { "SEAL", "Sea Launch platform" }, { "RLLB", "Rocket Lab LC-1, Mahia (New Zealand)" },
        { "SVOBO", "Svobodny (Russia)" }, { "YAVNE", "Palmachim, Yavne (Israel)" }, { "NSC", "Naro (South Korea)" }, { "HGSTR", "Hammaguir (Algeria)" }, { "WOMRA", "Woomera (Australia)" },
        { "SEMLS", "Semnan (Iran)" }, { "KYMSC", "Kapustin Yar (Russia)" }, { "AFWTR", "Vandenberg, California (USA)" }, { "DLS", "Dongfang Sea Launch (China)" }, { "CAS", "Canary Islands" },
        { "ERAS", "Eastern Range, Cape Canaveral (USA)" }, { "WSEAL", "Sea Launch platform" }, { "OREN", "Orenburg (Russia)" }, { "SUBL", "Submarine launch" }, { "UNK", "Unknown" },
    };
    for (const auto& e : t) if (c == e.first) return e.second;
    return c;
}

inline std::string opsStatusName(const std::string& c) {
    if (c == "+") return "Operational"; if (c == "-") return "Non-operational"; if (c == "P") return "Partially operational"; if (c == "B") return "Backup / standby";
    if (c == "S") return "Spare"; if (c == "X") return "Extended mission"; if (c == "D") return "Decayed"; return "";
}

inline std::string objectTypeName(const std::string& c) {
    if (c == "PAY") return "Payload (working satellite)"; if (c == "R/B") return "Rocket body"; if (c == "DEB") return "Debris"; return "";
}

// Days since 1970-01-01 for a civil date.
inline long long daysFromCivil(long long y, unsigned m, unsigned d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

// "1998-11-20" -> "1998-11-20  (28.0 years ago)"
inline std::string dateWithAge(const std::string& iso) {
    if (iso.size() < 10) return iso;
    const int y = std::atoi(iso.substr(0, 4).c_str()), m = std::atoi(iso.substr(5, 2).c_str()), d = std::atoi(iso.substr(8, 2).c_str());
    if (y < 1900 || m < 1 || m > 12 || d < 1 || d > 31) return iso;
    const double years = (unixSeconds() - (double)daysFromCivil(y, (unsigned)m, (unsigned)d) * 86400.0) / (365.25 * 86400.0);
    char b[64];
    if (years < 0) std::snprintf(b, sizeof b, "%s", iso.substr(0, 10).c_str());
    else if (years < 1.0) std::snprintf(b, sizeof b, "%s  (%.0f months ago)", iso.substr(0, 10).c_str(), years * 12.0);
    else std::snprintf(b, sizeof b, "%s  (%.1f years ago)", iso.substr(0, 10).c_str(), years);
    return b;
}

inline std::string withCommas(double v) {
    char b[48];
    std::snprintf(b, sizeof b, "%.0f", v);
    std::string s = b, out;
    for (size_t i = 0; i < s.size(); ++i) { out += s[i]; const size_t left = s.size() - i - 1; if (left && left % 3 == 0 && std::isdigit((unsigned char)s[i])) out += ','; }
    return out;
}

}  // namespace webinfo

class InfoLookup {
public:
    explicit InfoLookup(std::string dir) : dir_(std::move(dir)) { th_ = std::thread([this] { loop(); }); }
    ~InfoLookup() { { std::lock_guard<std::mutex> lk(m_); quit_ = true; } cv_.notify_all(); if (th_.joinable()) th_.detach(); }

    // Current state for an object; starts the lookup the first time it is asked for.
    InfoCard get(const InfoRequest& req) {
        std::lock_guard<std::mutex> lk(m_);
        auto it = cards_.find(req.key);
        if (it != cards_.end()) return it->second;
        cards_[req.key] = InfoCard{};
        queue_.push_back(req);
        while (queue_.size() > 6) { cards_.erase(queue_.front().key); queue_.pop_front(); }  // the user clicked on: drop the stale ones
        cv_.notify_one();
        return InfoCard{};
    }

private:
    std::string dir_;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<InfoRequest> queue_;
    std::map<std::string, InfoCard> cards_;
    bool quit_ = false, netError_ = false;
    std::thread th_;

    std::string base(const std::string& key) const { char b[24]; std::snprintf(b, sizeof b, "%016zx", std::hash<std::string>{}(key)); return dir_ + b; }

    static std::string oneLine(std::string s) { for (char& c : s) if (c == '\n' || c == '\r' || c == '\t') c = ' '; return s; }

    bool loadCache(const std::string& key, InfoCard& c) {
        namespace fs = std::filesystem;
        std::error_code ec;
        const std::string meta = base(key) + ".txt";
        if (!fs::exists(meta, ec)) return false;
        std::istringstream in(readFile(meta));
        std::string line;
        double stamp = 0;
        bool found = false;
        while (std::getline(in, line)) {
            if (line.size() < 2) continue;
            const std::string v = line.substr(2);
            if (line[0] == 'T') stamp = std::atof(v.c_str());
            else if (line[0] == 'F') found = v == "1";
            else if (line[0] == 'S') c.summary = v;
            else if (line[0] == 'U') c.wikiUrl = v;
            else if (line[0] == 'I') c.imageFile = v;
            else if (line[0] == 'C') c.credit = v;
            else if (line[0] == 'L') c.creditUrl = v;
            else if (line[0] == 'K' || line[0] == 'W') {
                const size_t tab = v.find('\t');
                if (tab != std::string::npos) (line[0] == 'K' ? c.facts : c.links).emplace_back(v.substr(0, tab), v.substr(tab + 1));
            } else if (line[0] == 'R') {
                std::vector<std::string> p;
                size_t a = 0;
                for (size_t i = 0; i <= v.size(); ++i) if (i == v.size() || v[i] == '\t') { p.push_back(v.substr(a, i - a)); a = i + 1; }
                if (p.size() >= 6) { c.hasRoute = true; c.fromName = p[0]; c.toName = p[1]; c.fromLat = std::atof(p[2].c_str()); c.fromLon = std::atof(p[3].c_str()); c.toLat = std::atof(p[4].c_str()); c.toLon = std::atof(p[5].c_str()); }
            }
        }
        const double age = unixSeconds() - stamp;
        if (age > (found ? 14.0 : 1.0) * 86400.0) return false;
        if (!c.imageFile.empty() && !fs::exists(c.imageFile, ec)) c.imageFile.clear();
        c.found = found; c.ready = true;
        return true;
    }

    void saveCache(const std::string& key, const InfoCard& c) {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::create_directories(dir_, ec);
        std::ofstream f(base(key) + ".txt", std::ios::binary);
        f << "T " << (long long)unixSeconds() << "\nF " << (c.found ? 1 : 0) << "\nS " << oneLine(c.summary) << "\nU " << c.wikiUrl << "\nI " << c.imageFile << "\nC " << oneLine(c.credit) << "\nL " << c.creditUrl << "\n";
        for (const auto& k : c.facts) f << "K " << oneLine(k.first) << '\t' << oneLine(k.second) << "\n";
        for (const auto& k : c.links) f << "W " << k.first << '\t' << k.second << "\n";
        if (c.hasRoute) f << "R " << oneLine(c.fromName) << '\t' << oneLine(c.toName) << '\t' << c.fromLat << '\t' << c.fromLon << '\t' << c.toLat << '\t' << c.toLon << "\n";
    }

    bool fetchJson(const std::string& url, webinfo::JV& out) {
        const std::string tmp = dir_ + "tmp.json";
        if (!curlCapture(url, tmp, 15) || !webinfo::parseJson(readFile(tmp), out)) { netError_ = true; return false; }
        return true;
    }

    bool downloadImage(const std::string& url, const std::string& stem, std::string& outFile) {
        const std::string ext = webinfo::imageExtension(url);
        if (ext.empty()) return false;
        const std::string path = stem + ext;
        if (!curlGet(url, path, 20)) return false;
        outFile = path;
        return true;
    }

    static bool containsAny(const std::string& hay, const std::vector<std::string>& words) {
        if (words.empty()) return true;
        for (const std::string& w : words) if (!w.empty() && hay.find(w) != std::string::npos) return true;
        return false;
    }
    // Whole-word match, so "ship" does not match "championship" and "tug" does not match "tugboat names".
    static bool containsWord(const std::string& hay, const std::vector<std::string>& words) {
        if (words.empty()) return true;
        for (const std::string& w : words) {
            if (w.empty()) continue;
            for (size_t at = hay.find(w); at != std::string::npos; at = hay.find(w, at + 1)) {
                const bool before = at == 0 || !std::isalnum((unsigned char)hay[at - 1]);
                const size_t end = at + w.size();
                size_t e2 = end;
                if (e2 < hay.size() && hay[e2] == 's') ++e2;  // plurals: "satellites", "ships"
                const bool after = end >= hay.size() || !std::isalnum((unsigned char)hay[end]) || e2 >= hay.size() || !std::isalnum((unsigned char)hay[e2]);
                if (before && after) return true;
            }
        }
        return false;
    }

    void wikipedia(const InfoRequest& r, InfoCard& c) {
        for (const std::string& q : r.queries) {
            if (q.empty()) continue;
            const std::string url = "https://en.wikipedia.org/w/api.php?action=query&format=json&generator=search&gsrnamespace=0&gsrlimit=4&redirects=1&prop=pageimages%7Cextracts%7Cinfo"
                                    "&piprop=thumbnail&pithumbsize=480&exintro=1&explaintext=1&exsentences=4&exlimit=max&inprop=url&gsrsearch=" + webinfo::urlEncode(q);
            webinfo::JV root;
            if (!fetchJson(url, root)) continue;
            const webinfo::JV* query = root.get("query");
            const webinfo::JV* pages = query ? query->get("pages") : nullptr;
            if (!pages) continue;
            const webinfo::JV* best = nullptr;
            double bestIndex = 1e9;
            for (const auto& p : pages->o) {
                const webinfo::JV& page = p.second;
                const std::string title = webinfo::lower(page.str("title")), text = webinfo::lower(page.str("extract"));
                if (text.empty() || title.find("disambiguation") != std::string::npos || text.find("may refer to") != std::string::npos || text.find("commonly refers to") != std::string::npos) continue;
                if (!containsAny(title, r.match) || !containsWord(text, r.keywords)) continue;
                const webinfo::JV* idx = page.get("index");
                const double i = idx ? idx->d : 99;
                if (i < bestIndex) { bestIndex = i; best = &page; }
            }
            if (!best) continue;
            c.summary = best->str("extract");
            c.wikiUrl = best->str("fullurl");
            c.found = true;
            if (c.imageFile.empty()) {
                const webinfo::JV* th = best->get("thumbnail");
                const std::string src = th ? th->str("source") : std::string();
                std::string file;
                if (!src.empty() && downloadImage(src, base(r.key), file)) { c.imageFile = file; c.credit = r.kind == "ac" ? "Image: Wikipedia (this aircraft type, not this airframe)" : "Image: Wikipedia / Wikimedia Commons"; c.creditUrl = c.wikiUrl; }
            }
            return;
        }
    }

    static void fact(InfoCard& c, const std::string& k, const std::string& v) { if (!v.empty()) c.facts.emplace_back(k, v); }

    void satellite(InfoRequest& r, InfoCard& c) {
        if (r.norad.empty()) return;
        webinfo::JV root;
        if (fetchJson("https://celestrak.org/satcat/records.php?CATNR=" + r.norad + "&FORMAT=JSON", root) && root.t == 'a' && !root.a.empty()) {
            const webinfo::JV& o = root.a[0];
            fact(c, "Owner", webinfo::ownerName(o.str("OWNER")));
            fact(c, "Object type", webinfo::objectTypeName(o.str("OBJECT_TYPE")));
            fact(c, "Status", webinfo::opsStatusName(o.str("OPS_STATUS_CODE")));
            fact(c, "Launched", webinfo::dateWithAge(o.str("LAUNCH_DATE")));
            fact(c, "Launch site", webinfo::launchSiteName(o.str("LAUNCH_SITE")));
            if (!o.str("DECAY_DATE").empty()) fact(c, "Decayed", o.str("DECAY_DATE"));
            if (o.get("RCS") && o.get("RCS")->t == 'd') {
                const double rcs = o.num("RCS");
                char b[64];
                std::snprintf(b, sizeof b, "%.2f m\xC2\xB2  (%s)", rcs, rcs < 0.1 ? "small" : rcs < 1.0 ? "medium" : "large");
                fact(c, "Radar size", b);
            }
            c.found = true;
        }
        webinfo::JV sat;
        if (fetchJson("https://db.satnogs.org/api/satellites/?norad_cat_id=" + r.norad, sat) && sat.t == 'a' && !sat.a.empty()) {
            const webinfo::JV& o = sat.a[0];
            fact(c, "Also known as", o.str("names"));
            if (!o.str("countries").empty()) fact(c, "Countries", o.str("countries"));
            if (!o.str("website").empty()) c.links.emplace_back("Operator website", o.str("website"));
            const std::string img = o.str("image");
            if (!img.empty()) { r.fallbackImage = "https://db-satnogs.freetls.fastly.net/media/" + img; r.fallbackCredit = "Image: SatNOGS DB"; }
            c.found = true;
        }
        webinfo::JV tx;
        if (fetchJson("https://db.satnogs.org/api/transmitters/?satellite__norad_cat_id=" + r.norad + "&format=json", tx) && tx.t == 'a') {
            int shown = 0;
            for (const webinfo::JV& t : tx.a) {
                if (shown >= 2) break;
                const double dl = t.num("downlink_low");
                if (dl <= 0 || t.str("status") != "active") continue;
                char b[160];
                std::snprintf(b, sizeof b, "%.4f MHz  %s%s%s", dl / 1e6, t.str("mode").c_str(), t.str("description").empty() ? "" : "  - ", t.str("description").c_str());
                fact(c, shown == 0 ? "Radio downlink" : " ", b);
                ++shown;
            }
            if (shown) c.links.emplace_back("SatNOGS radio data", "https://db.satnogs.org/satellite/" + r.norad);
        }
    }

    void aircraft(InfoRequest& r, InfoCard& c) {
        if (!r.photoHex.empty()) {
            webinfo::JV root;
            if (fetchJson("https://api.adsbdb.com/v0/aircraft/" + webinfo::lower(r.photoHex), root)) {
                const webinfo::JV* resp = root.get("response");
                const webinfo::JV* ac = resp ? resp->get("aircraft") : nullptr;
                if (ac) {
                    fact(c, "Owner / operator", ac->str("registered_owner"));
                    fact(c, "Manufacturer", ac->str("manufacturer"));
                    if (!ac->str("url_photo").empty()) { r.fallbackImage = ac->str("url_photo"); r.fallbackCredit = "Photo: planespotters.net via adsbdb"; }
                    c.found = true;
                }
            }
        }
        if (!r.callsign.empty()) {
            webinfo::JV root;
            if (fetchJson("https://api.adsbdb.com/v0/callsign/" + webinfo::urlEncode(r.callsign), root)) {
                const webinfo::JV* resp = root.get("response");
                const webinfo::JV* fr = resp ? resp->get("flightroute") : nullptr;
                if (fr) {
                    const webinfo::JV* al = fr->get("airline");
                    if (al) fact(c, "Airline", al->str("name"));
                    const webinfo::JV *o = fr->get("origin"), *d = fr->get("destination");
                    if (o && d) {
                        auto label = [](const webinfo::JV& a) { return (a.str("municipality").empty() ? a.str("name") : a.str("municipality")) + " (" + (a.str("iata_code").empty() ? a.str("icao_code") : a.str("iata_code")) + ")"; };
                        c.hasRoute = true;
                        c.fromName = label(*o); c.toName = label(*d);
                        c.fromLat = o->num("latitude"); c.fromLon = o->num("longitude"); c.toLat = d->num("latitude"); c.toLon = d->num("longitude");
                        c.found = true;
                    }
                }
            }
        }
    }

    static std::string stripTags(const std::string& html) {
        std::string o;
        bool tag = false;
        for (char ch : html) { if (ch == '<') tag = true; else if (ch == '>') tag = false; else if (!tag) o += ch; }
        return o;
    }

    // Wikimedia Commons keeps a category "IMO nnnnnnn" for many ships; it holds one ship category with the photos.
    // The match is by IMO number, so it is exact.
    void commonsShip(InfoRequest& r) {
        if (r.imo.empty()) return;
        webinfo::JV cat;
        if (!fetchJson("https://commons.wikimedia.org/w/api.php?action=query&format=json&list=categorymembers&cmtype=subcat&cmlimit=1&cmtitle=Category:IMO%20" + r.imo, cat)) return;
        const webinfo::JV* q = cat.get("query");
        const webinfo::JV* members = q ? q->get("categorymembers") : nullptr;
        if (!members || members->a.empty()) return;
        const std::string title = members->a[0].str("title");
        if (title.empty()) return;
        webinfo::JV files;
        if (!fetchJson("https://commons.wikimedia.org/w/api.php?action=query&format=json&generator=categorymembers&gcmnamespace=6&gcmlimit=6&prop=imageinfo&iiprop=url%7Cextmetadata&iiurlwidth=480&gcmtitle=" + webinfo::urlEncode(title), files)) return;
        const webinfo::JV* fq = files.get("query");
        const webinfo::JV* pages = fq ? fq->get("pages") : nullptr;
        if (!pages) return;
        for (const auto& kv : pages->o) {
            const webinfo::JV* infos = kv.second.get("imageinfo");
            if (!infos || infos->a.empty()) continue;
            const webinfo::JV& ii = infos->a[0];
            const std::string thumb = ii.str("thumburl");
            if (webinfo::imageExtension(thumb).empty()) continue;
            const webinfo::JV* meta = ii.get("extmetadata");
            std::string artist = meta && meta->get("Artist") ? stripTags(meta->get("Artist")->str("value")) : std::string();
            const std::string licence = meta && meta->get("LicenseShortName") ? meta->get("LicenseShortName")->str("value") : std::string();
            if (artist.size() > 40) artist = artist.substr(0, 40);
            r.fallbackImage = thumb;
            r.fallbackCredit = "Photo: " + (artist.empty() ? std::string("Wikimedia Commons") : artist + " / Wikimedia Commons") + (licence.empty() ? "" : " (" + licence + ")");
            return;
        }
    }

    // Ship facts from Wikidata, found by IMO number. Also yields the exact Wikipedia article and a Commons photo.
    void ship(InfoRequest& r, InfoCard& c) {
        if (r.imo.empty()) return;
        webinfo::JV found;
        if (!fetchJson("https://www.wikidata.org/w/api.php?action=query&list=search&format=json&srsearch=haswbstatement:P458=" + r.imo, found)) return;
        const webinfo::JV* q = found.get("query");
        const webinfo::JV* list = q ? q->get("search") : nullptr;
        if (!list || list->a.empty()) return;
        const std::string qid = list->a[0].str("title");
        if (qid.empty() || qid[0] != 'Q') return;
        webinfo::JV ent;
        if (!fetchJson("https://www.wikidata.org/w/api.php?action=wbgetentities&format=json&props=claims%7Csitelinks&sitefilter=enwiki&ids=" + qid, ent)) return;
        const webinfo::JV* es = ent.get("entities");
        const webinfo::JV* e = es ? es->get(qid.c_str()) : nullptr;
        const webinfo::JV* claims = e ? e->get("claims") : nullptr;
        if (!claims) return;
        auto value = [&](const char* prop) -> const webinfo::JV* {
            const webinfo::JV* cl = claims->get(prop);
            if (!cl || cl->a.empty()) return nullptr;
            const webinfo::JV* snak = cl->a[0].get("mainsnak");
            const webinfo::JV* dv = snak ? snak->get("datavalue") : nullptr;
            return dv ? dv->get("value") : nullptr;
        };
        auto itemId = [&](const char* prop) { const webinfo::JV* v = value(prop); return v ? v->str("id") : std::string(); };
        auto amount = [&](const char* prop) { const webinfo::JV* v = value(prop); return v ? std::atof(v->str("amount").c_str()) : 0.0; };
        const std::string builder = itemId("P176"), operatorId = itemId("P137"), owner = itemId("P127");
        std::string ids;
        for (const std::string& id : { builder, operatorId, owner }) if (!id.empty() && ids.find(id) == std::string::npos) ids += (ids.empty() ? "" : "%7C") + id;
        std::map<std::string, std::string> names;
        if (!ids.empty()) {
            webinfo::JV lab;
            if (fetchJson("https://www.wikidata.org/w/api.php?action=wbgetentities&format=json&props=labels&languages=en&ids=" + ids, lab)) {
                const webinfo::JV* le = lab.get("entities");
                if (le) for (const auto& kv : le->o) { const webinfo::JV* ls = kv.second.get("labels"); const webinfo::JV* en = ls ? ls->get("en") : nullptr; if (en) names[kv.first] = en->str("value"); }
            }
        }
        const webinfo::JV* when = value("P729");
        const webinfo::JV* inc = value("P571");
        const std::string date = when ? when->str("time") : inc ? inc->str("time") : std::string();
        if (date.size() > 5) fact(c, "In service / built", date.substr(1, 4));
        fact(c, "Builder", names[builder]);
        fact(c, "Operator", names[operatorId]);
        fact(c, "Owner", names[owner]);
        if (amount("P1093") > 0) fact(c, "Gross tonnage", webinfo::withCommas(amount("P1093")) + " GT");
        const webinfo::JV* mass = value("P2067");
        if (mass && mass->str("unit").find("Q191118") != std::string::npos && amount("P2067") > 0) fact(c, "Deadweight", webinfo::withCommas(amount("P2067")) + " t");
        const webinfo::JV* links = e->get("sitelinks");
        const webinfo::JV* en = links ? links->get("enwiki") : nullptr;
        if (en && !en->str("title").empty()) { r.queries.insert(r.queries.begin(), en->str("title")); r.match = { webinfo::lower(en->str("title")) }; r.keywords.clear(); }
        const webinfo::JV* img = value("P18");
        if (img && img->t == 's') { r.fallbackImage = "https://commons.wikimedia.org/wiki/Special:FilePath/" + webinfo::pathEncode(img->s) + "?width=480"; r.fallbackCredit = "Image: Wikimedia Commons"; }
        c.links.emplace_back("Wikidata record", "https://www.wikidata.org/wiki/" + qid);
        if (!c.facts.empty()) c.found = true;
    }

    void planespotters(const InfoRequest& r, InfoCard& c) {
        webinfo::JV root;
        if (!fetchJson("https://api.planespotters.net/pub/photos/hex/" + r.photoHex, root)) return;
        const webinfo::JV* photos = root.get("photos");
        if (!photos || photos->a.empty()) return;
        const webinfo::JV& p = photos->a[0];
        const webinfo::JV* big = p.get("thumbnail_large");
        const std::string src = big ? big->str("src") : std::string();
        std::string file;
        if (!src.empty() && downloadImage(src, base(r.key), file)) {
            c.imageFile = file; c.found = true;
            c.credit = "Photo: " + (p.str("photographer").empty() ? std::string("planespotters.net") : p.str("photographer") + " / planespotters.net");
            c.creditUrl = p.str("link");
        }
    }

    void loop() {
        while (true) {
            InfoRequest req;
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [this] { return quit_ || !queue_.empty(); });
                if (quit_) return;
                req = queue_.front(); queue_.pop_front();
            }
            InfoCard card;
            if (!loadCache(req.key, card)) {
                card = InfoCard{};
                netError_ = false;
                if (req.kind == "sat") satellite(req, card);
                else if (req.kind == "ac") aircraft(req, card);
                else if (req.kind == "ship") { ship(req, card); if (req.fallbackImage.empty()) commonsShip(req); }
                if (!req.photoHex.empty()) planespotters(req, card);
                wikipedia(req, card);
                if (card.imageFile.empty() && !req.fallbackImage.empty()) {
                    std::string file;
                    if (downloadImage(req.fallbackImage, base(req.key), file)) { card.imageFile = file; card.credit = req.fallbackCredit; card.found = true; }
                }
                card.ready = true;
                if (card.found || !netError_) saveCache(req.key, card);  // a failed connection is retried next run
            }
            std::lock_guard<std::mutex> lk(m_);
            if (cards_.count(req.key)) cards_[req.key] = card;
        }
    }
};
