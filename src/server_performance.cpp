#include "server_performance.hpp"

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <VersionHelpers.h>
    #include <powrprof.h>
    #include <processthreadsapi.h>

namespace {

// Built-in "High performance" power scheme.
constexpr GUID kHighPerformanceScheme = {
    0x8c5e7fda, 0xe8bf, 0x4a96,
    {0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c}
};

void append_warning(std::string& warning, const char* operation, DWORD error) {
    if (!warning.empty()) warning += "; ";
    warning += operation;
    warning += " failed (Windows error ";
    warning += std::to_string(error);
    warning += ")";
}

bool set_process_high_qos() {
    PROCESS_POWER_THROTTLING_STATE state{};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    state.StateMask = 0; // Explicit HighQoS: execution-speed throttling off.
    return SetProcessInformation(
        GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state)) != FALSE;
}

bool set_thread_high_qos(HANDLE thread) {
    THREAD_POWER_THROTTLING_STATE state{};
    state.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    state.StateMask = 0; // Explicit HighQoS: execution-speed throttling off.
    return SetThreadInformation(
        thread, ThreadPowerThrottling, &state, sizeof(state)) != FALSE;
}

} // namespace

struct ScopedServerPerformance::Impl {
    GUID original_scheme{};
    bool have_original_scheme = false;
    bool scheme_changed = false;

    DWORD original_process_priority = 0;
    bool process_priority_changed = false;

    PROCESS_POWER_THROTTLING_STATE original_process_qos{};
    bool have_original_process_qos = false;
    bool process_qos_changed = false;

    int original_thread_priority = THREAD_PRIORITY_NORMAL;
    bool thread_priority_changed = false;

    THREAD_POWER_THROTTLING_STATE original_thread_qos{};
    bool have_original_thread_qos = false;
    bool thread_qos_changed = false;
};

bool is_windows_server_os() {
    return IsWindowsServer() != FALSE;
}

bool configure_current_thread_for_performance() {
    HANDLE thread = GetCurrentThread();
    const bool qos_ok = set_thread_high_qos(thread);
    const bool priority_ok = SetThreadPriority(thread, THREAD_PRIORITY_HIGHEST) != FALSE;
    return qos_ok && priority_ok;
}

ScopedServerPerformance::ScopedServerPerformance(bool enable) {
    status_.server_os = is_windows_server_os();
    status_.requested = enable && status_.server_os;
    if (!status_.requested) return;

    impl_ = std::make_unique<Impl>();
    status_.active = true;

    GUID* active_scheme = nullptr;
    DWORD power_error = PowerGetActiveScheme(nullptr, &active_scheme);
    if (power_error == ERROR_SUCCESS && active_scheme) {
        impl_->original_scheme = *active_scheme;
        impl_->have_original_scheme = true;
        LocalFree(active_scheme);

        if (IsEqualGUID(impl_->original_scheme, kHighPerformanceScheme)) {
            status_.high_performance_scheme = true;
        } else {
            power_error = PowerSetActiveScheme(nullptr, &kHighPerformanceScheme);
            if (power_error == ERROR_SUCCESS) {
                impl_->scheme_changed = true;
                status_.high_performance_scheme = true;
            } else {
                append_warning(status_.warning, "High Performance power scheme", power_error);
            }
        }
    } else {
        if (active_scheme) LocalFree(active_scheme);
        append_warning(status_.warning, "PowerGetActiveScheme", power_error);
    }

    impl_->original_process_priority = GetPriorityClass(GetCurrentProcess());
    if (impl_->original_process_priority == 0) {
        append_warning(status_.warning, "GetPriorityClass", GetLastError());
    } else if (impl_->original_process_priority == HIGH_PRIORITY_CLASS ||
               impl_->original_process_priority == REALTIME_PRIORITY_CLASS) {
        status_.high_priority = true;
    } else if (SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS)) {
        impl_->process_priority_changed = true;
        status_.high_priority = true;
    } else {
        append_warning(status_.warning, "SetPriorityClass", GetLastError());
    }

    impl_->original_process_qos.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    impl_->have_original_process_qos = GetProcessInformation(
        GetCurrentProcess(), ProcessPowerThrottling,
        &impl_->original_process_qos, sizeof(impl_->original_process_qos)) != FALSE;
    const bool process_qos_ok = set_process_high_qos();
    if (process_qos_ok) {
        impl_->process_qos_changed = true;
    } else {
        append_warning(status_.warning, "SetProcessInformation(HighQoS)", GetLastError());
    }

    HANDLE thread = GetCurrentThread();
    impl_->original_thread_priority = GetThreadPriority(thread);
    if (impl_->original_thread_priority != THREAD_PRIORITY_ERROR_RETURN &&
        impl_->original_thread_priority < THREAD_PRIORITY_HIGHEST) {
        if (SetThreadPriority(thread, THREAD_PRIORITY_HIGHEST)) {
            impl_->thread_priority_changed = true;
        } else {
            append_warning(status_.warning, "SetThreadPriority", GetLastError());
        }
    }

    impl_->original_thread_qos.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    impl_->have_original_thread_qos = GetThreadInformation(
        thread, ThreadPowerThrottling,
        &impl_->original_thread_qos, sizeof(impl_->original_thread_qos)) != FALSE;
    const bool thread_qos_ok = set_thread_high_qos(thread);
    if (thread_qos_ok) {
        impl_->thread_qos_changed = true;
    } else {
        append_warning(status_.warning, "SetThreadInformation(HighQoS)", GetLastError());
    }
    status_.high_qos = process_qos_ok && thread_qos_ok;
}

ScopedServerPerformance::~ScopedServerPerformance() {
    if (!impl_) return;

    HANDLE thread = GetCurrentThread();
    if (impl_->thread_qos_changed) {
        if (impl_->have_original_thread_qos) {
            SetThreadInformation(thread, ThreadPowerThrottling,
                                 &impl_->original_thread_qos,
                                 sizeof(impl_->original_thread_qos));
        } else {
            THREAD_POWER_THROTTLING_STATE managed{};
            managed.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
            SetThreadInformation(thread, ThreadPowerThrottling, &managed, sizeof(managed));
        }
    }
    if (impl_->thread_priority_changed) {
        SetThreadPriority(thread, impl_->original_thread_priority);
    }

    if (impl_->process_qos_changed) {
        if (impl_->have_original_process_qos) {
            SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling,
                                  &impl_->original_process_qos,
                                  sizeof(impl_->original_process_qos));
        } else {
            PROCESS_POWER_THROTTLING_STATE managed{};
            managed.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
            SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling,
                                  &managed, sizeof(managed));
        }
    }
    if (impl_->process_priority_changed) {
        SetPriorityClass(GetCurrentProcess(), impl_->original_process_priority);
    }
    if (impl_->scheme_changed && impl_->have_original_scheme) {
        PowerSetActiveScheme(nullptr, &impl_->original_scheme);
    }
}

#else

struct ScopedServerPerformance::Impl {};

bool is_windows_server_os() {
    return false;
}

bool configure_current_thread_for_performance() {
    return true;
}

ScopedServerPerformance::ScopedServerPerformance(bool) {}
ScopedServerPerformance::~ScopedServerPerformance() = default;

#endif
