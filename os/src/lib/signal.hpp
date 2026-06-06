/// @file signal.hpp
/// @brief Signal definitions used by the kernel for user exception delivery.

#pragma once

#include <types.hpp>

namespace kernel {

/// @brief Standard POSIX-like signal numbers.
enum class Signal : uint64_t {
    SIG_NONE    = 0,
    SIGHUP      = 1,
    SIGINT      = 2,
    SIGQUIT     = 3,
    SIGILL      = 4,
    SIGTRAP     = 5,
    SIGABRT     = 6,
    SIGBUS      = 7,
    SIGFPE      = 8,
    SIGKILL     = 9,
    SIGUSR1     = 10,
    SIGSEGV     = 11,
    SIGUSR2     = 12,
    SIGPIPE     = 13,
    SIGALRM     = 14,
    SIGTERM     = 15,
    SIGSTKFLT   = 16,
    SIGCHLD     = 17,
    SIGCONT     = 18,
    SIGSTOP     = 19,
    SIGTSTP     = 20,
    SIGSYS      = 31,
};

/// @brief Signal handler type (user-space function pointer).
using sighandler_t = void (*)(int signal);

/// @brief Default signal actions taken when no user handler is installed.
enum class SignalAction : uint8_t {
    TERMINATE,
    IGNORE,
    STOP,
    CONT,
};

/// @brief Maps a CPU exception vector to a signal number and action.
struct ExceptionSignalMap {
    Signal signal;
    SignalAction action;
    const char* name;
};

/// @brief Returns the signal mapping for a given CPU exception vector.
static inline ExceptionSignalMap exception_to_signal(uint64_t vector) {
    switch (vector) {
        case 0:  return {Signal::SIGFPE,  SignalAction::TERMINATE, "SIGFPE"};   // #DE
        case 4:  return {Signal::SIGILL,  SignalAction::TERMINATE, "SIGILL"};   // #OF
        case 5:  return {Signal::SIGSEGV, SignalAction::TERMINATE, "SIGSEGV"};  // #BR
        case 6:  return {Signal::SIGILL,  SignalAction::TERMINATE, "SIGILL"};   // #UD
        case 7:  return {Signal::SIGSEGV, SignalAction::TERMINATE, "SIGSEGV"};  // #NM
        case 8:  return {Signal::SIGSEGV, SignalAction::TERMINATE, "SIGSEGV"};  // #DF
        case 10: return {Signal::SIGSEGV, SignalAction::TERMINATE, "SIGSEGV"};  // #TS
        case 11: return {Signal::SIGSEGV, SignalAction::TERMINATE, "SIGSEGV"};  // #NP
        case 12: return {Signal::SIGSEGV, SignalAction::TERMINATE, "SIGSEGV"};  // #SS
        case 13: return {Signal::SIGSEGV, SignalAction::TERMINATE, "SIGSEGV"};  // #GP
        case 14: return {Signal::SIGSEGV, SignalAction::TERMINATE, "SIGSEGV"};  // #PF
        case 16: return {Signal::SIGFPE,  SignalAction::TERMINATE, "SIGFPE"};   // #MF
        case 17: return {Signal::SIGSEGV, SignalAction::TERMINATE, "SIGSEGV"};  // #AC
        case 19: return {Signal::SIGFPE,  SignalAction::TERMINATE, "SIGFPE"};   // #XF
        default: return {Signal::SIGSYS,  SignalAction::TERMINATE, "SIGSYS"};
    }
}

/// @brief Maximum number of signal handlers per task (one per signal number).
static constexpr size_t MAX_SIGNAL_HANDLERS = 32;

/// @brief Global recovery IP for safe user memory access.
///        Non-zero means we are in a user memory access recovery zone.
///        If a fault occurs and this is set, execution is redirected here.
extern "C" uint64_t g_user_access_recover_ip;

} // namespace kernel
