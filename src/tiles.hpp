#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "net.hpp"
#include "raylib.h"

class TileCache {
public:
    explicit TileCache(std::string cacheDir) : dir_(std::move(cacheDir)) {}
    ~TileCache() {
        { std::lock_guard<std::mutex> lk(m_); stop_ = true; }
        cv_.notify_all();
        for (auto& t : workers_) t.detach();
    }

    int addLayer(const std::string& subdir, const std::string& urlTemplate, const std::string& ext) {
        std::lock_guard<std::mutex> lk(m_);
        for (size_t i = 0; i < layers_.size(); ++i) if (layers_[i].subdir == subdir) return (int)i;
        layers_.push_back({ subdir, urlTemplate, ext });
        return (int)layers_.size() - 1;
    }

    void start(int n = 2) {
        if (!workers_.empty()) return;
        for (int i = 0; i < n; ++i) workers_.emplace_back([this] { run(); });
    }

    void beginFrame(unsigned frame) { frame_ = frame; budget_ = 4; }

    const Texture2D* get(int layer, int z, int x, int y, bool request, float* alpha = nullptr) {
        const uint64_t k = key(layer, z, x, y);
        std::lock_guard<std::mutex> lk(m_);
        auto it = tiles_.find(k);
        if (it == tiles_.end()) {
            if (!request) return nullptr;
            tiles_[k] = Tile{};
            queue_.push_front(k);
            cv_.notify_one();
            return nullptr;
        }
        Tile& t = it->second;
        if (t.state == 2 && request && steadySeconds() - t.failedAt > 45.0) {
            t = Tile{};
            queue_.push_front(k);
            cv_.notify_one();
            return nullptr;
        }
        if (t.state == 1 && budget_ > 0) {
            --budget_;
            t.tex = LoadTexture(path(layer, z, x, y).c_str());
            if (t.tex.id) { SetTextureFilter(t.tex, TEXTURE_FILTER_BILINEAR); t.state = 3; t.loadedAt = steadySeconds(); }
            else { t.state = 2; t.failedAt = steadySeconds(); }
        }
        if (t.state == 3) {
            t.last = frame_;
            if (alpha) *alpha = std::min(1.0f, (float)((steadySeconds() - t.loadedAt) / 0.35));
            return &t.tex;
        }
        if (request && t.state == 0) {
            for (auto q = queue_.begin(); q != queue_.end(); ++q)
                if (*q == k) { queue_.erase(q); queue_.push_front(k); break; }
        }
        return nullptr;
    }

    void evict(size_t maxLoaded = 400) {
        std::lock_guard<std::mutex> lk(m_);
        size_t loaded = 0;
        for (auto& kv : tiles_) loaded += kv.second.state == 3;
        while (loaded > maxLoaded) {
            auto oldest = tiles_.end();
            for (auto it = tiles_.begin(); it != tiles_.end(); ++it)
                if (it->second.state == 3 && (oldest == tiles_.end() || it->second.last < oldest->second.last)) oldest = it;
            if (oldest == tiles_.end() || oldest->second.last + 2 >= frame_) break;
            UnloadTexture(oldest->second.tex);
            tiles_.erase(oldest);
            --loaded;
        }
    }

    size_t pending() { std::lock_guard<std::mutex> lk(m_); return queue_.size() + active_; }
    size_t loaded() { std::lock_guard<std::mutex> lk(m_); size_t n = 0; for (auto& kv : tiles_) n += kv.second.state == 3; return n; }

private:
    struct Layer { std::string subdir, url, ext; };
    struct Tile { int state = 0; Texture2D tex{}; unsigned last = 0; double failedAt = 0, loadedAt = 0; };

    static uint64_t key(int layer, int z, int x, int y) { return ((uint64_t)layer << 54) | ((uint64_t)z << 48) | ((uint64_t)x << 24) | (uint64_t)y; }
    static void unkey(uint64_t k, int& layer, int& z, int& x, int& y) { layer = (int)(k >> 54); z = (int)((k >> 48) & 63); x = (int)((k >> 24) & 0xFFFFFF); y = (int)(k & 0xFFFFFF); }
    std::string path(int layer, int z, int x, int y) const {  // caller must not hold m_ when layers_ can change; layers_ only grows, copy is cheap
        return dir_ + "/" + layers_[layer].subdir + "/" + std::to_string(z) + "/" + std::to_string(x) + "/" + std::to_string(y) + layers_[layer].ext;
    }
    std::string url(int layer, int z, int x, int y) const {
        std::string u = layers_[layer].url;
        auto sub = [&](const char* tag, const std::string& v) { size_t p; while ((p = u.find(tag)) != std::string::npos) u.replace(p, std::string(tag).size(), v); };
        sub("{z}", std::to_string(z)); sub("{x}", std::to_string(x)); sub("{y}", std::to_string(y));
        sub("{s}", std::string(1, "abcd"[(x + y) & 3]));
        return u;
    }

    void run() {
        for (;;) {
            uint64_t k;
            std::string p, u;
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [&] { return stop_ || !queue_.empty(); });
                if (stop_) return;
                k = queue_.front(); queue_.pop_front(); ++active_;
                int layer, z, x, y; unkey(k, layer, z, x, y);
                p = path(layer, z, x, y); u = url(layer, z, x, y);
            }
            std::error_code ec;
            const bool ok = (std::filesystem::exists(p, ec) && std::filesystem::file_size(p, ec) > 0) || curlGet(u, p, 10);
            std::lock_guard<std::mutex> lk(m_);
            --active_;
            auto it = tiles_.find(k);
            if (it != tiles_.end()) { it->second.state = ok ? 1 : 2; it->second.failedAt = steadySeconds(); }
        }
    }

    std::string dir_;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<uint64_t> queue_;
    std::unordered_map<uint64_t, Tile> tiles_;
    std::vector<Layer> layers_;
    std::vector<std::thread> workers_;
    bool stop_ = false;
    size_t active_ = 0;
    unsigned frame_ = 0;
    int budget_ = 4;
};
