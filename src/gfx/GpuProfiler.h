#pragma once
#include "gfx/GL.h"

#include <string>
#include <vector>

namespace isle {

// Per-pass GPU timing with GL_TIME_ELAPSED queries. Results are read back a few
// frames late so the CPU never stalls waiting on the GPU.
class GpuProfiler {
public:
    struct Entry {
        std::string name;
        double ms = 0.0;  // exponentially smoothed
    };

    ~GpuProfiler();
    void beginFrame();
    void begin(const char* name);
    void end();
    void endFrame() { ++frame_; }
    // Blocks until every in-flight query has resolved (benchmark / screenshot mode only).
    void resolveAll();

    const std::vector<Entry>& entries() const { return entries_; }
    double totalMs() const;

    // Exact averages since the last reset (used by the benchmark mode).
    struct Stat {
        std::string name;
        double sum;
        int count;
        double avg() const { return count ? sum / count : 0.0; }
    };
    const std::vector<Stat>& stats() const { return stats_; }
    void resetStats() { stats_.clear(); }

private:
    void record(const std::string& name, double ms);
    static constexpr int kLatency = 4;
    std::vector<Stat> stats_;
    struct Scope {
        std::string name;
        GLuint query;
    };
    std::vector<Scope> pending_[kLatency];
    std::vector<GLuint> pool_;
    std::vector<Entry> entries_;
    unsigned frame_ = 0;
};

} // namespace isle
