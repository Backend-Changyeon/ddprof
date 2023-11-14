// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dd_profiling.h"

#include "allocation_tracker.hpp"
#include "constants.hpp"
#include "daemonize.hpp"
#include "ddprof_cmdline.hpp"
#include "defer.hpp"
#include "ipc.hpp"
#include "lib_embedded_data.h"
#include "lib_logger.hpp"
#include "logger_setup.hpp"
#include "signal_helper.hpp"
#include "symbol_overrides.hpp"
#include "syscalls.hpp"

#include <cassert>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace ddprof {

namespace {
int ddprof_start_profiling_internal();

struct ProfilerState {
  bool initialized = false;
  bool started = false;
  bool allocation_profiling_started = false;
  bool follow_execs = true;
  pid_t profiler_pid = 0;

  decltype(&::getenv) getenv = &::getenv;
  decltype(&::putenv) putenv = &::putenv;
  decltype(&::setenv) setenv = &::setenv;
  decltype(&::unsetenv) unsetenv = &::unsetenv;

  // Pointer on [01] char for "<k_profiler_active_env_variable>=[01]" env
  // variable. This allows to modify environment without calling putenv / setenv
  // in a thread safe way.
  char *profiler_active_indicator;

  static constexpr size_t profiler_active_len =
      std::char_traits<char>::length(k_profiler_active_env_variable) +
      std::char_traits<char>::length("=0");

  // profiler_active_str holds the string
  // "<k_profiler_active_env_variable>=[01]"
  char profiler_active_str[profiler_active_len + 1];
};

ProfilerState g_state;

void retrieve_original_env_functions() {
  /* Bash defines its own getenv/putenv functions
     Use dlsym with RTLD_NEXT to retrieve original functions from libc.
   */
  auto *getenv_ptr =
      reinterpret_cast<decltype(&::getenv)>(dlsym(RTLD_NEXT, "getenv"));
  if (getenv_ptr) {
    g_state.getenv = getenv_ptr;
  }
  auto *putenv_ptr =
      reinterpret_cast<decltype(&::putenv)>(dlsym(RTLD_NEXT, "putenv"));
  if (putenv_ptr) {
    g_state.putenv = putenv_ptr;
  }
  auto *unsetenv_ptr =
      reinterpret_cast<decltype(&::unsetenv)>(dlsym(RTLD_NEXT, "unsetenv"));
  if (unsetenv_ptr) {
    g_state.unsetenv = unsetenv_ptr;
  }
  auto *setenv_ptr =
      reinterpret_cast<decltype(&::setenv)>(dlsym(RTLD_NEXT, "setenv"));
  if (setenv_ptr) {
    g_state.setenv = setenv_ptr;
  }
}

void init_profiler_library_active() {
  char *s = g_state.getenv(k_profiler_active_env_variable);

  if (!s || strlen(s) != 1 || (s[0] != '0' && s[0] != '1')) {
    (void)snprintf(g_state.profiler_active_str,
                   std::size(g_state.profiler_active_str), "%s=0",
                   k_profiler_active_env_variable);
    g_state.putenv(g_state.profiler_active_str);
    g_state.profiler_active_indicator = g_state.profiler_active_str +
        std::char_traits<char>::length(k_profiler_active_env_variable) + 1;
  } else {
    g_state.profiler_active_indicator = s;
  }
}

void init_state() {
  if (g_state.initialized) {
    return;
  }

  retrieve_original_env_functions();
  init_profiler_library_active();

  auto *follow_execs_env = g_state.getenv(k_allocation_profiling_follow_execs);
  g_state.follow_execs = !(follow_execs_env && arg_no(follow_execs_env));
  g_state.initialized = true;
}

// return true if this profiler is active for this process or one of its parent
bool is_profiler_library_active() {
  if (!g_state.initialized) {
    // should not happen
    return false;
  }
  return *g_state.profiler_active_indicator == '1';
}

void set_profiler_library_active() {
  if (!g_state.initialized) {
    // should not happen
    return;
  }
  *g_state.profiler_active_indicator = '1';
}

void set_profiler_library_inactive() {
  if (!g_state.initialized) {
    // should not happen
    return;
  }
  *g_state.profiler_active_indicator = '0';
}

void allocation_profiling_stop() {
  if (g_state.allocation_profiling_started) {
    AllocationTracker::allocation_tracking_free();
    g_state.allocation_profiling_started = false;
  }
}

// Return socket created by ddprof when injecting lib if present
std::string get_ddprof_socket_path() {
  const char *socket_str = g_state.getenv(k_profiler_lib_socket_env_variable);
  return socket_str ? socket_str : "";
}

// Get profiler socket path from pipefd
std::string get_ddprof_socket_path(int pipefd) {
  char socket_str[PATH_MAX];
  auto r = ::read(pipefd, socket_str, sizeof(socket_str));
  return r <= 0 ? "" : socket_str;
}

bool contains_lib(std::string_view ldpreload_str, std::string_view libname) {
  auto pos = ldpreload_str.find(libname);
  if (pos == std::string_view::npos) {
    return false;
  }

  if (pos > 0 && ldpreload_str[pos - 1] != '/') {
    return false;
  }

  auto remaining_str = ldpreload_str.substr(pos + libname.size());
  if (!remaining_str.empty()) {
    char const c = remaining_str.front();
    // space and colon are the allowed separators in LD_PRELOAD, dash is present
    // when hash is appended to libdd-profiling-embedded.so
    return c == ' ' || c == ':' || c == '-';
  }

  return true;
}

bool is_preloaded() {
  const char *ldpreload_str = g_state.getenv("LD_PRELOAD");
  if (!ldpreload_str) {
    return false;
  }
  return contains_lib(ldpreload_str, k_libdd_profiling_name) ||
      contains_lib(ldpreload_str, k_libdd_profiling_embedded_name) ||
      contains_lib(ldpreload_str, k_libdd_loader_name);
}

struct ProfilerAutoStart {
  ProfilerAutoStart(const ProfilerAutoStart &) = delete;
  ProfilerAutoStart &operator=(const ProfilerAutoStart &) = delete;

  ProfilerAutoStart() noexcept {
    init_state();

    // Note that library needs to be linked with `--no-as-needed` when using
    // autostart, otherwise linker will completely remove library from DT_NEEDED
    // and library will not be loaded.
    bool autostart = false;
    const char *autostart_env =
        g_state.getenv(k_profiler_auto_start_env_variable);
    if ((autostart_env && arg_yes(autostart_env)) || is_preloaded()) {
      // if library is preloaded, autostart profiling since there is no way
      // otherwise to start profiling
      autostart = true;
    }

    init_profiler_library_active();

    // autostart if library is injected by ddprof
    if (autostart) {
      try {
        ddprof_start_profiling_internal();
      } catch (...) {} // NOLINT(bugprone-empty-catch)
    }
  }

  ~ProfilerAutoStart() = default;
};

ProfilerAutoStart g_autostart;

int exec_ddprof(pid_t target_pid, pid_t parent_pid,
                int pipefd_to_library) noexcept {
  char ddprof_str[] = "ddprof";

  char pid_buf[std::numeric_limits<pid_t>::digits10 + 1];
  (void)snprintf(pid_buf, sizeof(pid_buf), "%d", target_pid);
  char pipefd_buf[std::numeric_limits<int>::digits10 + 1];
  (void)snprintf(pipefd_buf, sizeof(pipefd_buf), "%d", pipefd_to_library);

  char pid_opt_str[] = "-p";
  char pipefd_opt_str[] = "--pipefd";

  // cppcheck-suppress variableScope
  char *argv[] = {ddprof_str,     pid_opt_str, pid_buf,
                  pipefd_opt_str, pipefd_buf,  nullptr};

  // unset LD_PRELOAD, otherwise if libdd_profiling.so was preloaded, it
  // would trigger a fork bomb
  g_state.unsetenv("LD_PRELOAD");

  kill(parent_pid, SIGTERM);

  if (const char *ddprof_exe =
          g_state.getenv(k_profiler_ddprof_exe_env_variable);
      ddprof_exe) {
    execve(ddprof_exe, argv, environ);
  } else {
    auto exe_data = profiler_exe_data();
    if (exe_data.size == 0) {
      return -1;
    }
    int fd = memfd_create(ddprof_str, 1U /*MFD_CLOEXEC*/);
    if (fd == -1) {
      return -1;
    }
    defer { close(fd); };

    if (write(fd, exe_data.data, exe_data.size) !=
        static_cast<ssize_t>(exe_data.size)) {
      return -1;
    }
    fexecve(fd, argv, environ);
  }

  return -1;
}

void notify_fork() {
  AllocationTracker::notify_fork();
  reinstall_timer_after_fork();
}

int ddprof_start_profiling_internal() {
  // Refuse to start profiler if already started by this process or if active in
  // one of its ancestors
  if (g_state.started ||
      (!g_state.follow_execs && is_profiler_library_active())) {
    return -1;
  }

  // Library communicates with profiler through a unix socket.
  // Socket creation is the responsibility of the profiler.
  // By default ddprof creates an random abstract socket, that does not exist on
  // the filesystem: "\0/tmp/ddprof-<pid>-<random_string>.sock" but unix socket
  // path can be overridden with profiler --socket input option.
  // Profiler worker process accepts and handles connections on this socket in a
  // separate thread and sends ring buffer information upon request.
  //
  // Overview of the communication process (wrapper mode):
  //  * Profiler starts, set `DD_PROFILING_NATIVE_LIB_SOCKET` env variable with
  //    socket path and daemonizes.
  //  * Target process starts and blocks on intermediate process termination
  //  * Profiler creates socket and listen on it, and then unblock target
  //    process by killing intermediate process.
  //  * Profiler forks into worker process and worker process starts accepting
  //    connections.
  //  * Library get socket path from `DD_PROFILING_NATIVE_LIB_SOCKET`, connects
  //    to it, retrieve ring buffer information and closes connection.
  //  * Exec'd processes from target process inherit
  //  DD_PROFILING_NATIVE_LIB_SOCKET
  //    env variable and connect to profiler in the same manner.
  //
  // Overview of the communication process (library mode):
  //  * Library starts, checks if `DD_PROFILING_NATIVE_LIB_SOCKET` is set.
  //  * If `DD_PROFILING_NATIVE_LIB_SOCKET` is set:
  //    * Library connects to socket, retrieve ring buffer information and
  //      closes connection.
  //  * Otherwise:
  //    * Library daemonizes into the profiler.
  //    * Profiler blocks on intermediate process termination.
  //    * Library unblocks profiler by killing intermediate process.
  //    * Library blocks on reading socket path from pipe created during
  //     daemonization.
  //    * Profiler starts, creates socket and listen on socket.
  //    * Profiler creates socket, listen on it and then sends socket path to
  //      library through pipe created during daemonization.
  //      Pipe allows library to retrieve socket path from profiler and to
  //      ensure that library tries to connect to socket after it has been
  //      created by profiler.
  //    * Library connects to socket, retrieve ring buffer information and
  //      closes connection.
  //    * Library sets environment variable `DD_PROFILING_NATIVE_LIB_SOCKET`
  //      with socket path so that socket is used by future exec'd processes.
  //    * Exec'd processes from target process inherit
  //      `DD_PROFILING_NATIVE_LIB_SOCKET` env variable and connect to profiler
  //      in the same manner as in wrapper mode.

  auto socket_path = get_ddprof_socket_path();
  pid_t const target_pid = getpid();

  if (socket_path.empty()) {
    // no socket -> library will spawn a profiler
    auto daemonize_res = daemonize();
    if (daemonize_res.state == DaemonizeResult::Error) {
      return -1;
    }

    if (daemonize_res.state == DaemonizeResult::IntermediateProcess) {
      _exit(0);
    }

    if (daemonize_res.state == DaemonizeResult::DaemonProcess) {
      // executed by daemonized process
      exec_ddprof(target_pid, daemonize_res.temp_pid,
                  daemonize_res.pipe_fd.get());
      exit(1);
    }

    // executed by initial process

    // wait for profiler process to be ready
    socket_path = get_ddprof_socket_path(daemonize_res.pipe_fd.get());
    if (socket_path.empty()) {
      return -1;
    }

    g_state.setenv(k_profiler_lib_socket_env_variable, socket_path.c_str(), 1);
  }

  try {
    ReplyMessage info;
    auto client_socket = create_client_socket(socket_path);
    if (!client_socket) {
      return -1;
    }

    if (!IsDDResOK(get_profiler_info(std::move(client_socket),
                                     kDefaultSocketTimeout, &info))) {
      return -1;
    }

    g_state.profiler_pid = info.pid;
    if (info.allocation_profiling_rate != 0) {
      uint32_t flags{0};
      // Negative profiling rate is interpreted as deterministic sampling rate
      if (info.allocation_profiling_rate < 0) {
        flags |= AllocationTracker::kDeterministicSampling;
        info.allocation_profiling_rate = -info.allocation_profiling_rate;
      }

      if (info.allocation_flags & (1 << ReplyMessage::kLiveCallgraph)) {
        // tracking deallocations to allow a live view
        flags |= AllocationTracker::kTrackDeallocations;
      }

      if (IsDDResOK(AllocationTracker::allocation_tracking_init(
              info.allocation_profiling_rate, flags, info.stack_sample_size,
              info.ring_buffer))) {
        // \fixme{nsavoire} pthread_create should probably be overridden
        // at load time since we need to capture stack end addresses of all
        // threads in case allocation profiling is started later on
        setup_overrides(
            std::chrono::milliseconds{info.initial_loaded_libs_check_delay_ms},
            std::chrono::milliseconds{info.loaded_libs_check_interval_ms});
        // \fixme{nsavoire} what should we do when allocation tracker init
        // fails ?
        g_state.allocation_profiling_started = true;
      } else {
        LOG_ONCE("Error: %s", "Failure to start allocation profiling\n");
      }
    }
  } catch (const DDException &e) { return -1; }

  if (g_state.allocation_profiling_started) {
    int const res = pthread_atfork(nullptr, nullptr, notify_fork);
    if (res) {
      LOG_ONCE("Error:%s", "Unable to setup notify fork");
      assert(0);
    }
  }
  g_state.started = true;
  set_profiler_library_active();
  return 0;
}

} // namespace
} // namespace ddprof

int ddprof_start_profiling() {
  try {
    return ddprof::ddprof_start_profiling_internal();
  } catch (...) {} // NOLINT(bugprone-empty-catch)
  return -1;
}

void ddprof_stop_profiling(int timeout_ms) {
  using namespace ddprof;

  if (!g_state.started) {
    return;
  }

  defer {
    g_state.started = false;
    set_profiler_library_inactive();
  };

  if (g_state.allocation_profiling_started) {
    allocation_profiling_stop();
  }

  auto time_limit =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

  kill(g_state.profiler_pid, SIGTERM);
  const auto time_slice = std::chrono::milliseconds(10);

  while (std::chrono::steady_clock::now() < time_limit) {
    std::this_thread::sleep_for(time_slice);

    // check if profiler process is still alive
    if (!process_is_alive(g_state.profiler_pid)) {
      return;
    }
  }

  // timeout reached and profiler process is still not dead
  // Do a forceful kill
  kill(g_state.profiler_pid, SIGKILL);
}
