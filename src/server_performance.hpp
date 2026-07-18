#pragma once

#include <memory>
#include <string>

// Status of the temporary Windows Server performance policy used by compute tests.
// On non-Windows and workstation Windows builds the guard remains inactive.
struct ServerPerformanceStatus {
    bool server_os = false;
    bool requested = false;
    bool active = false;
    bool high_performance_scheme = false;
    bool high_qos = false;
    bool high_priority = false;
    std::string warning;
};

// Returns true for Windows Server editions (including Server Core and domain controllers).
bool is_windows_server_os();

// Applies HighQoS and high thread priority to the calling benchmark worker.
// The setting only lives for the lifetime of that worker thread.
bool configure_current_thread_for_performance();

// Temporarily optimizes Windows Server for a compute benchmark and restores the previous
// process/thread settings and power scheme when it leaves scope.
class ScopedServerPerformance {
public:
    explicit ScopedServerPerformance(bool enable);
    ~ScopedServerPerformance();

    ScopedServerPerformance(const ScopedServerPerformance&) = delete;
    ScopedServerPerformance& operator=(const ScopedServerPerformance&) = delete;
    ScopedServerPerformance(ScopedServerPerformance&&) = delete;
    ScopedServerPerformance& operator=(ScopedServerPerformance&&) = delete;

    const ServerPerformanceStatus& status() const { return status_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    ServerPerformanceStatus status_;
};
