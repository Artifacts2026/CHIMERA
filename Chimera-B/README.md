[![Build Status](https://travis-ci.org/baidu/braft.svg?branch=master)](https://travis-ci.org/baidu/braft)

---

# Overview
An industrial-grade C++ implementation of [RAFT consensus algorithm](https://raft.github.io/) and [replicated state machine](https://en.wikipedia.org/wiki/State_machine_replication) based on [brpc](https://github.com/brpc/brpc). braft is designed and implemented for scenarios demanding for high workload and low overhead of latency, with the consideration for easy-to-understand concepts so that engineers inside Baidu can build their own distributed systems individually and correctly.

It's widely used inside Baidu to build highly-available systems, such as:
* Storage systems: Key-Value, Block, Object, File ...
* SQL storages: HA MySQL cluster, distributed transactions, NewSQL systems ...
* Meta services: Various master modules, Lock services ...

# Getting Started

* Build [brpc](https://github.com/brpc/brpc/blob/master/docs/cn/getting_started.md) which is the main dependency of braft.

* Compile braft with cmake
  
  ```shell
  $ mkdir bld && cd bld && cmake .. && make
  ```

* Play braft with [examples](./example).

* Installing from vcpkg
  
  You can download and install `braft` using the [vcpkg](https://github.com/Microsoft/vcpkg) dependency manager:
  ```sh
  git clone https://github.com/Microsoft/vcpkg.git
  cd vcpkg
  ./bootstrap-vcpkg.sh
  ./vcpkg integrate install
  ./vcpkg install braft
  ```
  The `braft` port in vcpkg is kept up to date by Microsoft team members and community contributors. If the version is out of date, please [create an issue or pull   request](https://github.com/Microsoft/vcpkg) on the vcpkg repository.

# Docs

* Read [overview](./docs/cn/overview.md) to know what you can do with braft.
* Read [benchmark](./docs/cn/benchmark.md) to have a quick view about performance of braft
* [Build Service based on braft](./docs/cn/server.md)
* [Access Service based on braft](./docs/cn/client.md)
* [Cli tools](./docs/cn/cli.md)
* [Replication Model](./docs/cn/replication.md)
* Consensus protocol:
  * [RAFT](./docs/cn/raft_protocol.md)
  * [Paxos](./docs/cn/paxos_protocol.md)
  * [ZAB](./docs/cn/zab_protocol.md)
  * [QJM](./docs/cn/qjm.md)

# Discussion

* Add Weixin id ***zhengpf__87*** or ***xiongk_2049*** with a verification message '**braft**', then you will be invited into the discussion group. 

## TDX Optimization

For better performance in TDX virtualized environments, new_engraft supports busy-spin optimization to reduce VM Exit overhead. The optimization can be enabled by setting the following flag:

```bash
--enable_raft_busy_spin=true
```

This optimization replaces sleep-based waiting with busy-spinning for short I/O operations, which can significantly reduce VM Exit overhead in TDX environments. The maximum spin time can be configured with:

```bash
--raft_busy_spin_timeout_us=1000
```

The following components are optimized for TDX:
1. Log operations (fsync)
2. Replicator waiting for logs
3. FSM apply operations
4. Sequential log access optimization for reduced VM Exit
   - Added batch reader to improve sequential read patterns
   - Removed random shuffling in replica selection to maintain sequential access

This optimization is only recommended for TDX environments where VM Exit overhead is significant. In regular environments, it may increase CPU usage without providing performance benefits.
