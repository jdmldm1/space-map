#pragma once
// "Learn more" lookups for the selected satellite, aircraft, ship or mission, run on one background thread.
//   * Wikipedia (public API, no key): article summary, link and lead photo. A result is only accepted when its title
//     contains one of the caller's match words and its text mentions one of the caller's keywords, so a ship called
//     "Aurora" does not turn into the article about the aurora.
//   * Planespotters.net (public photo API, no key): a photo of the actual airframe by ICAO hex, with credit and link.
// Results, including misses, are cached on disk under info/ so each object is looked up once.
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
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
};

struct InfoRequest {
    std::string key;                    // unique per object, e.g. "sat:25544"
    std::vector<std::string> queries;   // Wikipedia search phrases, best first
    std::vector<std::string> match;     // lower-case words; the article title must contain one
    std::vector<std::string> keywords;  // lower-case words; the article text must contain one (empty = any)
    std::string photoHex;               // aircraft ICAO hex for Planespotters
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
                if (!containsAny(title, r.match) || !containsAny(text, r.keywords)) continue;
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
                if (!src.empty() && downloadImage(src, base(r.key), file)) { c.imageFile = file; c.credit = "Image: Wikipedia / Wikimedia Commons"; c.creditUrl = c.wikiUrl; }
            }
            return;
        }
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
                if (!req.photoHex.empty()) planespotters(req, card);
                wikipedia(req, card);
                card.ready = true;
                if (card.found || !netError_) saveCache(req.key, card);  // a failed connection is retried next run
            }
            std::lock_guard<std::mutex> lk(m_);
            if (cards_.count(req.key)) cards_[req.key] = card;
        }
    }
};
