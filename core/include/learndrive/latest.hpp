#pragma once
#include <condition_variable>
#include <mutex>
#include <optional>
#include <utility>
namespace ld {
// Ownership stays in T (e.g. shared_ptr<OS frame lease>). Replacement releases
// the stale lease immediately. One pending frame plus one in-flight frame.
template<class T> class Latest {
public:
    bool push(T frame) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(closed_) return false;
        if(pending_) ++dropped_;
        pending_=std::move(frame); cv_.notify_one(); return true;
    }
    std::optional<T> take() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock,[&]{return closed_||pending_.has_value();});
        auto result=std::move(pending_); pending_.reset(); return result;
    }
    void close() { std::lock_guard<std::mutex> lock(mutex_); closed_=true; pending_.reset(); cv_.notify_all(); }
    size_t dropped() const { std::lock_guard<std::mutex> lock(mutex_); return dropped_; }
private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::optional<T> pending_;
    bool closed_{};
    size_t dropped_{};
};
}
