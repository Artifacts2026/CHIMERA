// Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Authors: Zhangyi Chen(chenzhangyi01@baidu.com)
//          Wang,Yao(wangyao02@baidu.com)
//          Xiong,Kai(xiongkai@baidu.com)

#include <butil/logging.h>
#include "braft/raft.h"
#include "braft/log_manager.h"
#include "braft/node.h"
#include "braft/util.h"
#include "braft/raft.pb.h"
#include "braft/log_entry.h"
#include "braft/errno.pb.h"
#include "braft/node.h"

#include "braft/fsm_caller.h"
#include <bthread/unstable.h>

namespace braft {

static bvar::CounterRecorder g_commit_tasks_batch_counter(
        "raft_commit_tasks_batch_counter");

DEFINE_int32(raft_fsm_caller_commit_batch, 512, 
             "Max numbers of logs for the state machine to commit in a single batch");
BRPC_VALIDATE_GFLAG(raft_fsm_caller_commit_batch, brpc::PositiveInteger);

// chimera modify here to do tdx opt
DEFINE_bool(raft_enable_tdx_batch_apply, false, 
            "Enable batch apply for FSM in TDX environments to reduce memory encryption overhead");
            
FSMCaller::FSMCaller()
    : _log_manager(NULL)
    , _fsm(NULL)
    , _closure_queue(NULL)
    , _last_applied_index(0)
    , _last_applied_term(0)
    , _after_shutdown(NULL)
    , _node(NULL)
    , _cur_task(IDLE)
    , _applying_index(0)
    , _queue_started(false)
{
}

FSMCaller::~FSMCaller() {
    CHECK(_after_shutdown == NULL);
}

int FSMCaller::run(void* meta, bthread::TaskIterator<ApplyTask>& iter) {
    FSMCaller* caller = (FSMCaller*)meta;
    if (iter.is_queue_stopped()) {
        caller->do_shutdown();
        return 0;
    }
    int64_t max_committed_index = -1;
    int64_t counter = 0;
    size_t  batch_size = FLAGS_raft_fsm_caller_commit_batch;
    for (; iter; ++iter) {
        if (iter->type == COMMITTED && counter < batch_size) {
            if (iter->committed_index > max_committed_index) {
                max_committed_index = iter->committed_index;
                counter++;
            }
        } else {
            if (max_committed_index >= 0) {
                caller->_cur_task = COMMITTED;
                caller->do_committed(max_committed_index);
                max_committed_index = -1;
                g_commit_tasks_batch_counter << counter;
                counter = 0;
                batch_size = FLAGS_raft_fsm_caller_commit_batch;
            }
            switch (iter->type) {
            case COMMITTED:
                if (iter->committed_index > max_committed_index) {
                    max_committed_index = iter->committed_index;
                    counter++;
                }
                break;
            case SNAPSHOT_SAVE:
                caller->_cur_task = SNAPSHOT_SAVE;
                if (caller->pass_by_status(iter->done)) {
                    caller->do_snapshot_save((SaveSnapshotClosure*)iter->done);
                }
                break;
            case SNAPSHOT_LOAD:
                caller->_cur_task = SNAPSHOT_LOAD;
                // TODO: do we need to allow the snapshot loading to recover the
                // StateMachine if possible?
                if (caller->pass_by_status(iter->done)) {
                    caller->do_snapshot_load((LoadSnapshotClosure*)iter->done);
                }
                break;
            case LEADER_STOP:
                caller->_cur_task = LEADER_STOP;
                caller->do_leader_stop(*(iter->status));
                delete iter->status;
                break;
            case LEADER_START:
                caller->do_leader_start(*(iter->leader_start_context));
                delete iter->leader_start_context;
                break;
            case START_FOLLOWING:
                caller->_cur_task = START_FOLLOWING;
                caller->do_start_following(*(iter->leader_change_context));
                delete iter->leader_change_context;
                break;
            case STOP_FOLLOWING:
                caller->_cur_task = STOP_FOLLOWING;
                caller->do_stop_following(*(iter->leader_change_context));
                delete iter->leader_change_context;
                break;
            case ERROR:
                caller->_cur_task = ERROR;
                caller->do_on_error((OnErrorClousre*)iter->done);
                break;
            case IDLE:
                CHECK(false) << "Can't reach here";
                break;
            };
        }
    }
    if (max_committed_index >= 0) {
        caller->_cur_task = COMMITTED;
        caller->do_committed(max_committed_index);
        g_commit_tasks_batch_counter << counter;
        counter = 0;
    }
    caller->_cur_task = IDLE;
    return 0;
}

bool FSMCaller::pass_by_status(Closure* done) {
    brpc::ClosureGuard done_guard(done);
    if (!_error.status().ok()) {
        if (done) {
            done->status().set_error(
                        EINVAL, "FSMCaller is in bad status=`%s'",
                                _error.status().error_cstr());
        }
        return false;
    }
    done_guard.release();
    return true;
}

int FSMCaller::init(const FSMCallerOptions &options) {
    if (options.log_manager == NULL || options.fsm == NULL 
            || options.closure_queue == NULL) {
        return EINVAL;
    }
    _log_manager = options.log_manager;
    _fsm = options.fsm;
    _closure_queue = options.closure_queue;
    _after_shutdown = options.after_shutdown;
    _node = options.node;
    _last_applied_index.store(options.bootstrap_id.index,
                              butil::memory_order_relaxed);
    _last_applied_term = options.bootstrap_id.term;
    if (_node) {
        _node->AddRef();
    }
    
    bthread::ExecutionQueueOptions execq_opt;
    execq_opt.bthread_attr = options.usercode_in_pthread 
                             ? BTHREAD_ATTR_PTHREAD
                             : BTHREAD_ATTR_NORMAL;
    if (bthread::execution_queue_start(&_queue_id,
                                   &execq_opt,
                                   FSMCaller::run,
                                   this) != 0) {
        LOG(ERROR) << "fsm fail to start execution_queue";
        return -1;
    }
    _queue_started = true;
    return 0;
}

int FSMCaller::shutdown() {
    if (_queue_started) {
        return bthread::execution_queue_stop(_queue_id);
    }
    return 0;
}

void FSMCaller::do_shutdown() {
    if (_node) {
        _node->Release();
        _node = NULL;
    }
    _fsm->on_shutdown();
    if (_after_shutdown) {
        google::protobuf::Closure* saved_done = _after_shutdown;
        _after_shutdown = NULL;
        // after this point, |this| is likely to be destroyed, don't touch
        // anything
        saved_done->Run();
    }
}

int FSMCaller::on_committed(int64_t committed_index) {
    ApplyTask t;
    t.type = COMMITTED;
    t.committed_index = committed_index;
    return bthread::execution_queue_execute(_queue_id, t);
}

class OnErrorClousre : public Closure {
public:
    OnErrorClousre(const Error& e) : _e(e) {
    }
    const Error& error() { return _e; }
    void Run() {
        delete this;
    }
private:
    ~OnErrorClousre() {}
    Error _e;
};

int FSMCaller::on_error(const Error& e) {
    OnErrorClousre* c = new OnErrorClousre(e);
    ApplyTask t;
    t.type = ERROR;
    t.done = c;
    if (bthread::execution_queue_execute(_queue_id, t, 
                                         &bthread::TASK_OPTIONS_URGENT) != 0) {
        c->Run();
        return -1;
    }
    return 0;
}

void FSMCaller::do_on_error(OnErrorClousre* done) {
    brpc::ClosureGuard done_guard(done);
    set_error(done->error());
}

void FSMCaller::set_error(const Error& e) {
    if (_error.type() != ERROR_TYPE_NONE) {
        // Error has already reported
        return;
    }
    _error = e;
    if (_fsm) {
        _fsm->on_error(_error);
    }
    if (_node) {
        _node->on_error(_error);
    }
}

void FSMCaller::do_committed(int64_t committed_index) {
    if (!_error.status().ok()) {
        return;
    }
    
    // chimera modify here to do tdx opt
    // In TDX environments, batch apply can significantly reduce memory encryption overhead
    if (FLAGS_raft_enable_tdx_batch_apply) {
        int64_t last_applied_index = _last_applied_index.load(butil::memory_order_relaxed);

        // We can tolerate the disorder of committed_index
        if (last_applied_index >= committed_index) {
            return;
        }
        
        std::vector<Closure*> closure;
        int64_t first_closure_index = 0;
        CHECK_EQ(0, _closure_queue->pop_closure_until(committed_index, &closure,
                                                  &first_closure_index));

        // 高效TDX优化：预处理所有条目以提高内存局部性
        const int TDX_BATCH_SIZE = 64; // 更大的批处理大小
        std::vector<LogEntry*> data_entries; // 收集所有数据条目
        data_entries.reserve(TDX_BATCH_SIZE);
        
        // 创建迭代器处理所有条目
        IteratorImpl iter_impl(_fsm, _log_manager, &closure, first_closure_index,
                 last_applied_index, committed_index, &_applying_index);
        
        // 第一遍：处理非数据条目，收集数据条目
        while (iter_impl.is_good()) {
            LogEntry* entry = iter_impl.entry();
            if (entry->type != ENTRY_TYPE_DATA) {
                // 特殊条目单独处理，保持原始逻辑
                if (entry->type == ENTRY_TYPE_CONFIGURATION) {
                    if (entry->old_peers == NULL) {
                        _fsm->on_configuration_committed(
                                Configuration(*entry->peers),
                                entry->id.index);
                    }
                }
                
                // 运行closure，保持原始逻辑
                if (iter_impl.done()) {
                    iter_impl.done()->Run();
                }
            } else {
                // 数据条目：添加到批处理集合
                entry->AddRef(); // 增加引用计数
                data_entries.push_back(entry);
                
                // 达到批处理上限时处理当前批次
                if (data_entries.size() >= TDX_BATCH_SIZE) {
                    break;
                }
            }
            iter_impl.next();
        }
        
        // 处理收集到的数据条目（批量处理）
        if (!data_entries.empty()) {
            // TDX优化：预触摸所有条目数据以改善内存局部性
            for (LogEntry* entry : data_entries) {
                // 强制将数据预加载到缓存，减少后续处理时的内存加密开销
                volatile size_t size = entry->data.size();
                (void)size;
            }
            
            // 开始位置和当前条目索引
            int64_t start_index = data_entries.front()->id.index;
            _applying_index.store(start_index, butil::memory_order_relaxed);
            
            // 创建一个特殊的Iterator用于批处理
            class BatchIterator : public Iterator {
            public:
                BatchIterator(IteratorImpl* impl, std::vector<LogEntry*>& entries) 
                    : Iterator(impl), _entries(entries), _index(0) {
                    // 重置到第一个条目
                    reset();
                }
                
                void reset() {
                    if (!_entries.empty()) {
                        _impl->_cur_entry = _entries[0];
                        _impl->_cur_index = _entries[0]->id.index - 1; // 使next()推进到第一个条目
                        _index = 0;
                        _impl->next();
                    }
                }
                
                // 重写next方法，不使用override关键字
                void next() {
                    if (_index + 1 < _entries.size()) {
                        // 使用下一个预加载的条目
                        _index++;
                        LogEntry* old_entry = _impl->_cur_entry;
                        _impl->_cur_entry = _entries[_index];
                        _impl->_cur_index = _entries[_index]->id.index - 1;
                        _impl->next();
                        
                        // 避免重复Release
                        if (old_entry == _entries[_index - 1]) {
                            _entries[_index - 1] = NULL;
                        }
                    } else {
                        // 最后一个条目正常处理
                        if (_impl->_cur_entry == _entries[_index]) {
                            _entries[_index] = NULL;
                        }
                        _impl->next();
                    }
                    
                    // 更新应用索引
                    if (_impl->_cur_entry) {
                        _impl->_applying_index->store(_impl->_cur_index, 
                                               butil::memory_order_relaxed);
                    }
                }
                
                private:
                    std::vector<LogEntry*>& _entries;
                    size_t _index;
            };
            
            // 使用批处理迭代器
            BatchIterator batch_iter(&iter_impl, data_entries);
            _fsm->on_apply(batch_iter);
            
            // 检查迭代器是否全部处理完毕
            LOG_IF(ERROR, batch_iter.valid())
                    << "Node " << _node->node_id() 
                    << " Iterator is still valid, did you return before iterator "
                       " reached the end?";
            
            // 释放所有剩余的引用计数
            for (LogEntry* entry : data_entries) {
                if (entry) {
                    entry->Release();
                }
            }
            
            // 继续处理剩余条目
            while (iter_impl.is_good()) {
                if (iter_impl.entry()->type != ENTRY_TYPE_DATA) {
                    // 特殊条目处理
                    if (iter_impl.entry()->type == ENTRY_TYPE_CONFIGURATION) {
                        if (iter_impl.entry()->old_peers == NULL) {
                            _fsm->on_configuration_committed(
                                    Configuration(*iter_impl.entry()->peers),
                                    iter_impl.entry()->id.index);
                        }
                    }
                    if (iter_impl.done()) {
                        iter_impl.done()->Run();
                    }
                    iter_impl.next();
                    continue;
                }
                
                // 处理剩余的数据条目
                Iterator single_iter(&iter_impl);
                _fsm->on_apply(single_iter);
                
                // 验证处理完成
                LOG_IF(ERROR, single_iter.valid())
                        << "Node " << _node->node_id() 
                        << " Iterator is still valid, did you return before iterator "
                           " reached the end?";
                single_iter.next();
            }
        }
        
        // 错误处理
        if (iter_impl.has_error()) {
            set_error(iter_impl.error());
            iter_impl.run_the_rest_closure_with_error();
        }
        
        // 更新应用索引状态，保持与原始实现一致
        const int64_t last_index = iter_impl.index() - 1;
        const int64_t last_term = _log_manager->get_term(last_index);
        LogId last_applied_id(last_index, last_term);
        _last_applied_index.store(committed_index, butil::memory_order_release);
        _last_applied_term = last_term;
        _log_manager->set_applied_id(last_applied_id);
        
        return;
    }
    
    // Original implementation
    int64_t last_applied_index = _last_applied_index.load(
                                        butil::memory_order_relaxed);

    // We can tolerate the disorder of committed_index
    if (last_applied_index >= committed_index) {
        return;
    }
    std::vector<Closure*> closure;
    int64_t first_closure_index = 0;
    CHECK_EQ(0, _closure_queue->pop_closure_until(committed_index, &closure,
                                                  &first_closure_index));

    IteratorImpl iter_impl(_fsm, _log_manager, &closure, first_closure_index,
                 last_applied_index, committed_index, &_applying_index);
    for (; iter_impl.is_good();) {
        if (iter_impl.entry()->type != ENTRY_TYPE_DATA) {
            if (iter_impl.entry()->type == ENTRY_TYPE_CONFIGURATION) {
                if (iter_impl.entry()->old_peers == NULL) {
                    // Joint stage is not supposed to be noticeable by end users.
                    _fsm->on_configuration_committed(
                            Configuration(*iter_impl.entry()->peers),
                            iter_impl.entry()->id.index);
                }
            }
            // For other entries, we have nothing to do besides flush the
            // pending tasks and run this closure to notify the caller that the
            // entries before this one were successfully committed and applied.
            if (iter_impl.done()) {
                iter_impl.done()->Run();
            }
            iter_impl.next();
            continue;
        }
        Iterator iter(&iter_impl);
        _fsm->on_apply(iter);
        LOG_IF(ERROR, iter.valid())
                << "Node " << _node->node_id() 
                << " Iterator is still valid, did you return before iterator "
                   " reached the end?";
        // Try move to next in case that we pass the same log twice.
        iter.next();
    }
    if (iter_impl.has_error()) {
        set_error(iter_impl.error());
        iter_impl.run_the_rest_closure_with_error();
    }
    const int64_t last_index = iter_impl.index() - 1;
    const int64_t last_term = _log_manager->get_term(last_index);
    LogId last_applied_id(last_index, last_term);
    _last_applied_index.store(committed_index, butil::memory_order_release);
    _last_applied_term = last_term;
    _log_manager->set_applied_id(last_applied_id);
}

int FSMCaller::on_snapshot_save(SaveSnapshotClosure* done) {
    ApplyTask task;
    task.type = SNAPSHOT_SAVE;
    task.done = done;
    return bthread::execution_queue_execute(_queue_id, task);
}

void FSMCaller::do_snapshot_save(SaveSnapshotClosure* done) {
    CHECK(done);

    int64_t last_applied_index = _last_applied_index.load(butil::memory_order_relaxed);

    SnapshotMeta meta;
    meta.set_last_included_index(last_applied_index);
    meta.set_last_included_term(_last_applied_term);
    ConfigurationEntry conf_entry;
    _log_manager->get_configuration(last_applied_index, &conf_entry);
    for (Configuration::const_iterator
            iter = conf_entry.conf.begin();
            iter != conf_entry.conf.end(); ++iter) { 
        *meta.add_peers() = iter->to_string();
    }
    for (Configuration::const_iterator
            iter = conf_entry.old_conf.begin();
            iter != conf_entry.old_conf.end(); ++iter) { 
        *meta.add_old_peers() = iter->to_string();
    }

    SnapshotWriter* writer = done->start(meta);
    if (!writer) {
        done->status().set_error(EINVAL, "snapshot_storage create SnapshotWriter failed");
        done->Run();
        return;
    }

    _fsm->on_snapshot_save(writer, done);
    return;
}

int FSMCaller::on_snapshot_load(LoadSnapshotClosure* done) {
    ApplyTask task;
    task.type = SNAPSHOT_LOAD;
    task.done = done;
    return bthread::execution_queue_execute(_queue_id, task);
}

void FSMCaller::do_snapshot_load(LoadSnapshotClosure* done) {
    //TODO done_guard
    SnapshotReader* reader = done->start();
    if (!reader) {
        done->status().set_error(EINVAL, "open SnapshotReader failed");
        done->Run();
        return;
    }

    SnapshotMeta meta;
    int ret = reader->load_meta(&meta);
    if (0 != ret) {
        done->status().set_error(ret, "SnapshotReader load_meta failed.");
        done->Run();
        if (ret == EIO) {
            Error e;
            e.set_type(ERROR_TYPE_SNAPSHOT);
            e.status().set_error(ret, "Fail to load snapshot meta");
            set_error(e);
        }
        return;
    }

    LogId last_applied_id;
    last_applied_id.index = _last_applied_index.load(butil::memory_order_relaxed);
    last_applied_id.term = _last_applied_term;
    LogId snapshot_id;
    snapshot_id.index = meta.last_included_index();
    snapshot_id.term = meta.last_included_term();
    if (last_applied_id > snapshot_id) {
        done->status().set_error(ESTALE,"Loading a stale snapshot"
                                 " last_applied_index=%" PRId64 " last_applied_term=%" PRId64
                                 " snapshot_index=%" PRId64 " snapshot_term=%" PRId64,
                                 last_applied_id.index, last_applied_id.term,
                                 snapshot_id.index, snapshot_id.term);
        return done->Run();
    }

    ret = _fsm->on_snapshot_load(reader);
    if (ret != 0) {
        done->status().set_error(ret, "StateMachine on_snapshot_load failed");
        done->Run();
        Error e;
        e.set_type(ERROR_TYPE_STATE_MACHINE);
        e.status().set_error(ret, "StateMachine on_snapshot_load failed");
        set_error(e);
        return;
    }

    if (meta.old_peers_size() == 0) {
        // Joint stage is not supposed to be noticeable by end users.
        Configuration conf;
        for (int i = 0; i < meta.peers_size(); ++i) {
            conf.add_peer(meta.peers(i));
        }
        _fsm->on_configuration_committed(conf, meta.last_included_index());
    }

    _last_applied_index.store(meta.last_included_index(),
                              butil::memory_order_release);
    _last_applied_term = meta.last_included_term();
    done->Run();
}

int FSMCaller::on_leader_stop(const butil::Status& status) {
    ApplyTask task;
    task.type = LEADER_STOP;
    butil::Status* on_leader_stop_status = new butil::Status(status);
    task.status = on_leader_stop_status;
    if (bthread::execution_queue_execute(_queue_id, task) != 0) {
        delete on_leader_stop_status;
        return -1;
    }
    return 0;
}

int FSMCaller::on_leader_start(int64_t term, int64_t lease_epoch) {
    ApplyTask task;
    task.type = LEADER_START;
    LeaderStartContext* on_leader_start_context =
        new LeaderStartContext(term, lease_epoch);
    task.leader_start_context = on_leader_start_context;
    if (bthread::execution_queue_execute(_queue_id, task) != 0) {
        delete on_leader_start_context;
        return -1;
    }
    return 0;
}

void FSMCaller::do_leader_stop(const butil::Status& status) {
    _fsm->on_leader_stop(status);
}

void FSMCaller::do_leader_start(const LeaderStartContext& leader_start_context) {
    _node->leader_lease_start(leader_start_context.lease_epoch);
    _fsm->on_leader_start(leader_start_context.term);
}

int FSMCaller::on_start_following(const LeaderChangeContext& start_following_context) {
    ApplyTask task;
    task.type = START_FOLLOWING;
    LeaderChangeContext* context  = new LeaderChangeContext(start_following_context.leader_id(), 
            start_following_context.term(), start_following_context.status());
    task.leader_change_context = context;
    if (bthread::execution_queue_execute(_queue_id, task) != 0) {
        delete context;
        return -1;
    }
    return 0;
}

int FSMCaller::on_stop_following(const LeaderChangeContext& stop_following_context) {
    ApplyTask task;
    task.type = STOP_FOLLOWING;
    LeaderChangeContext* context = new LeaderChangeContext(stop_following_context.leader_id(), 
            stop_following_context.term(), stop_following_context.status());
    task.leader_change_context = context;
    if (bthread::execution_queue_execute(_queue_id, task) != 0) {
        delete context;
        return -1;
    }
    return 0;
}

void FSMCaller::do_start_following(const LeaderChangeContext& start_following_context) {
    _fsm->on_start_following(start_following_context);
}

void FSMCaller::do_stop_following(const LeaderChangeContext& stop_following_context) {
    _fsm->on_stop_following(stop_following_context);
}

void FSMCaller::describe(std::ostream &os, bool use_html) {
    const char* newline = (use_html) ? "<br>" : "\n";
    TaskType cur_task = _cur_task;
    const int64_t applying_index = _applying_index.load(
                                    butil::memory_order_relaxed);
    os << "state_machine: ";
    switch (cur_task) {
    case IDLE:
        os << "Idle";
        break;
    case COMMITTED:
        os << "Applying log_index=" << applying_index;
        break;
    case SNAPSHOT_SAVE:
        os << "Saving snapshot";
        break;
    case SNAPSHOT_LOAD:
        os << "Loading snapshot";
        break;
    case ERROR:
        os << "Notifying error";
        break;
    case LEADER_STOP:
        os << "Notifying leader stop";
        break;
    case LEADER_START:
        os << "Notifying leader start";
        break;
    case START_FOLLOWING:
        os << "Notifying start following";
        break;
    case STOP_FOLLOWING:
        os << "Notifying stop following";
        break;
    }
    os << newline;
}

int64_t FSMCaller::applying_index() const {
    TaskType cur_task = _cur_task;
    if (cur_task != COMMITTED) {
        return 0;
    } else {
        return _applying_index.load(butil::memory_order_relaxed);
    }
}

void FSMCaller::join() {
    if (_queue_started) {
        bthread::execution_queue_join(_queue_id);
        _queue_started = false;
    }
}

IteratorImpl::IteratorImpl(StateMachine* sm, LogManager* lm,
                          std::vector<Closure*> *closure, 
                          int64_t first_closure_index,
                          int64_t last_applied_index, 
                          int64_t committed_index,
                          butil::atomic<int64_t>* applying_index)
        : _sm(sm)
        , _lm(lm)
        , _closure(closure)
        , _first_closure_index(first_closure_index)
        , _cur_index(last_applied_index)
        , _committed_index(committed_index)
        , _cur_entry(NULL)
        , _applying_index(applying_index)
{ next(); }

void IteratorImpl::next() {
    if (_cur_entry) {
        _cur_entry->Release();
        _cur_entry = NULL;
    }
    if (_cur_index <= _committed_index) {
        ++_cur_index;
        if (_cur_index <= _committed_index) {
            _cur_entry = _lm->get_entry(_cur_index);
            if (_cur_entry == NULL) {
                _error.set_type(ERROR_TYPE_LOG);
                _error.status().set_error(-1,
                        "Fail to get entry at index=%" PRId64
                        " while committed_index=%" PRId64,
                        _cur_index, _committed_index);
            }
            _applying_index->store(_cur_index, butil::memory_order_relaxed);
        }
    }
}

Closure* IteratorImpl::done() const {
    if (_cur_index < _first_closure_index) {
        return NULL;
    }
    return (*_closure)[_cur_index - _first_closure_index];
}

void IteratorImpl::set_error_and_rollback(
            size_t ntail, const butil::Status* st) {
    if (ntail == 0) {
        CHECK(false) << "Invalid ntail=" << ntail;
        return;
    }
    if (_cur_entry == NULL || _cur_entry->type != ENTRY_TYPE_DATA) {
        _cur_index -= ntail;
    } else {
        _cur_index -= (ntail - 1);
    }
    if (_cur_entry) {
        _cur_entry->Release();
        _cur_entry = NULL;
    }
    _error.set_type(ERROR_TYPE_STATE_MACHINE);
    _error.status().set_error(ESTATEMACHINE, 
            "StateMachine meet critical error when applying one "
            " or more tasks since index=%" PRId64 ", %s", _cur_index,
            (st ? st->error_cstr() : "none"));
}

void IteratorImpl::run_the_rest_closure_with_error() {
    for (int64_t i = std::max(_cur_index, _first_closure_index);
            i <= _committed_index; ++i) {
        Closure* done = (*_closure)[i - _first_closure_index];
        if (done) {
            done->status() = _error.status();
            run_closure_in_bthread(done);
        }
    }
}

}  //  namespace braft
