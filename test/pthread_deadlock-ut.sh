#!/usr/bin/env bash

set -euo pipefail
timeout 5s ./ddprof --do_export no -e 'sALLOC period=1' ./test/pthread_deadlock
