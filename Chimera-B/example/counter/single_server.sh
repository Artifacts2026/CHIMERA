#!/bin/bash

# Copyright (c) 2018 Baidu.com, Inc. All Rights Reserved
# 
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# 
#     http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# source shflags from current directory
mydir="${BASH_SOURCE%/*}"
if [[ ! -d "$mydir" ]]; then mydir="$PWD"; fi
. $mydir/../shflags

# define command-line flags
DEFINE_string crash_on_fatal 'true' 'Crash on fatal log'
DEFINE_integer bthread_concurrency '18' 'Number of worker pthreads'
DEFINE_string sync 'true' 'fsync each time'
DEFINE_string valgrind 'false' 'Run in valgrind'
DEFINE_integer max_segment_size '8388608' 'Max segment size'
DEFINE_integer server_num '3' 'Number of servers'
DEFINE_boolean clean 0 'Remove old "runtime" dir before running'
DEFINE_integer port 8100 "Port of the first server"
DEFINE_string use_tdx_encryption 'true' 'Whether to use TDX encryption for logs and snapshots'
DEFINE_string raft_log_persist_mode 'false' 'Whether to persist logs: true (default) or false (only cache, not persist)'
DEFINE_string raft_enable_tdx_snapshot_buffering 'true' 'Whether to enable TDX snapshot buffering'
DEFINE_string raft_enable_tdx_memory_locality 'true' 'Whether to enable TDX memory locality'
DEFINE_string raft_enable_tdx_batch_apply 'true' 'Whether to enable batch apply'

# parse the command-line
FLAGS "$@" || exit 1
eval set -- "${FLAGS_ARGV}"

# The alias for printing to stderr
alias error=">&2 echo counter: "

# hostname prefers ipv6
IP=`hostname -i | awk '{print $2}'`

if [ "$FLAGS_valgrind" == "true" ] && [ $(which valgrind) ] ; then
    VALGRIND="valgrind --tool=memcheck --leak-check=full"
fi

raft_peers=""
for ((i=0; i<$FLAGS_server_num; ++i)); do
    raft_peers="${raft_peers}${IP}:$((${FLAGS_port}+i)):0,"
done

# if [ "$FLAGS_clean" == "0" ]; then
#     rm -rf runtime
# fi

export TCMALLOC_SAMPLE_PARAMETER=524288

cd runtime/0
${VALGRIND} ./counter_server \
    -bthread_concurrency=${FLAGS_bthread_concurrency}\
    -crash_on_fatal_log=${FLAGS_crash_on_fatal} \
    -raft_max_segment_size=${FLAGS_max_segment_size} \
    -raft_sync=${FLAGS_sync} \
    -port=$((${FLAGS_port}+i)) -conf="${raft_peers}" \
    -use_tdx_encryption=${FLAGS_use_tdx_encryption} \
    -raft_log_persist_mode=${FLAGS_raft_log_persist_mode} \
    -raft_enable_tdx_snapshot_buffering=${FLAGS_raft_enable_tdx_snapshot_buffering} \
    -raft_enable_tdx_memory_locality=${FLAGS_raft_enable_tdx_memory_locality} \
    -raft_enable_tdx_batch_apply=${FLAGS_raft_enable_tdx_batch_apply} \
    -recover=true \
    > std.log 2>&1 &