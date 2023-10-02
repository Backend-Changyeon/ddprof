// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "daemonize.hpp"

#include <csignal> //
#include <cstdlib>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ddprof {

namespace {
void handle_signal(int /*sig*/) {}
DaemonizeResult daemonize_error() {
  return {DaemonizeResult::Error, -1, -1, -1};
}
} // namespace

DaemonizeResult daemonize() {
  int pipefd[2];
  if (pipe2(pipefd, O_CLOEXEC) == -1) {
    return daemonize_error();
  }

  const pid_t parent_pid = getpid();
  pid_t temp_pid = fork(); // "middle" (temporary) PID

  if (temp_pid == -1) {
    return daemonize_error();
  }

  if (temp_pid == 0) { // If I'm the temp PID enter branch
    close(pipefd[0]);

    temp_pid = getpid();
    pid_t child_pid = fork();
    if (child_pid != 0) { // If I'm the temp PID again, enter branch

      struct sigaction sa;
      if (sigemptyset(&sa.sa_mask) == -1) {
        exit(1);
      }

      sa.sa_handler = &handle_signal;
      sa.sa_flags = 0;
      if (sigaction(SIGTERM, &sa, nullptr) == -1) {
        exit(1);
      }

      // Block until our child exits or sends us a SIGTERM signal.
      // In the happy path, child will send us a SIGTERM signal, that we catch
      // and then exit normally (to free resources and make valgrind happy).
      waitpid(child_pid, nullptr, 0);
      return {DaemonizeResult::IntermediateProcess, temp_pid, parent_pid,
              child_pid};
    }

    child_pid = getpid();
    if (write(pipefd[1], &child_pid, sizeof(child_pid)) != sizeof(child_pid)) {
      exit(1);
    }
    close(pipefd[1]);
    // If I'm the child PID, then leave and attach profiler
    return {DaemonizeResult::DaemonProcess, temp_pid, parent_pid, child_pid};
  }

  close(pipefd[1]);

  pid_t grandchild_pid;
  if (read(pipefd[0], &grandchild_pid, sizeof(grandchild_pid)) !=
      sizeof(grandchild_pid)) {
    return daemonize_error();
  }

  // If I'm the target PID, then now it's time to wait until my
  // child, the middle PID, returns.
  waitpid(temp_pid, nullptr, 0);
  return {DaemonizeResult::InitialProcess, temp_pid, parent_pid,
          grandchild_pid};
}

} // namespace ddprof