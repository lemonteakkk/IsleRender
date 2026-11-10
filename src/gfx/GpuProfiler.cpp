#include "gfx/GpuProfiler.h"

namespace isle {

GpuProfiler::~GpuProfiler() {
    for (auto& frame : pending_)
        for (auto& s : frame) pool_.push_back(s.query);
    if (!pool_.empty()) glDeleteQueries(GLsizei(pool_.size()), pool_.data());
}

void GpuProfiler::record(const std::string& name, double ms) {
    Entry* e = nullptr;
    for (auto& x : entries_)
        if (x.name == name) e = &x;
    if (!e) entries_.push_back({name, ms});
    else e->ms = e->ms * 0.9 + ms * 0.1;

    Stat* s = nullptr;
    for (auto& x : stats_)
        if (x.name == name) s = &x;
    if (!s) {
        stats_.push_back({name, 0.0, 0});
        s = &stats_.back();
    }
    s->sum += ms;
    s->count += 1;
}

void GpuProfiler::beginFrame() {
    auto& old = pending_[frame_ % kLatency];
    for (auto& s : old) {
        GLuint available = 0;
        glGetQueryObjectuiv(s.query, GL_QUERY_RESULT_AVAILABLE, &available);
        if (available) {
            GLuint64 ns = 0;
            glGetQueryObjectui64v(s.query, GL_QUERY_RESULT, &ns);
            record(s.name, double(ns) * 1e-6);
        }
        pool_.push_back(s.query);
    }
    old.clear();
}

void GpuProfiler::resolveAll() {
    glFinish();
    for (int i = 0; i < kLatency; ++i) {
        // oldest first, so the smoothed value ends on the most recent frame
        auto& frame = pending_[(frame_ + unsigned(i)) % kLatency];
        for (auto& s : frame) {
            GLuint64 ns = 0;
            glGetQueryObjectui64v(s.query, GL_QUERY_RESULT, &ns);  // blocking
            record(s.name, double(ns) * 1e-6);
            pool_.push_back(s.query);
        }
        frame.clear();
    }
}

void GpuProfiler::begin(const char* name) {
    GLuint q;
    if (pool_.empty()) {
        glGenQueries(1, &q);
    } else {
        q = pool_.back();
        pool_.pop_back();
    }
    glBeginQuery(GL_TIME_ELAPSED, q);
    pending_[frame_ % kLatency].push_back({name, q});
}

void GpuProfiler::end() { glEndQuery(GL_TIME_ELAPSED); }

double GpuProfiler::totalMs() const {
    double t = 0;
    for (const auto& e : entries_) t += e.ms;
    return t;
}

} // namespace isle
