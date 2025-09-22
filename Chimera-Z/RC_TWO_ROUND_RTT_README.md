# ZooKeeper RC 两轮 RTT 通信机制

## 概述

这个修改在 ZooKeeper RC 中实现了两轮 RTT（Round-Trip Time）通信机制，在每次事务日志落盘前，Leader 会与所有 Follower 和 Observer 进行两轮通信，确保收到足够的响应后才允许日志提交到磁盘。

## 功能特性

### 🔄 两轮通信协议
- **第一轮**：`PRE_COMMIT_ROUND1` → `ACK_ROUND1`
- **第二轮**：`PRE_COMMIT_ROUND2` → `ACK_ROUND2`

### 🛡️ 容错机制
- 支持收到 `f` 个回复即可继续（默认简单多数）
- 2 秒超时保护机制
- 自动跳过非 Leader 节点

### 📊 监控和日志
- 所有操作都有 `RC:` 前缀的详细日志
- 包含性能指标和调试信息

## 实现架构

### 核心组件

1. **SyncRequestProcessor**
   - 入口点：在 `flush()` 方法中调用两轮 RTT
   - 位置：`performTwoRoundRTTBeforeCommit()`

2. **Leader**
   - 协调者：管理两轮通信的发送和 ACK 收集
   - 核心方法：`performRoundRTT()`, `handleRoundRTTAck()`

3. **LearnerHandler**
   - 消息路由：处理来自 Follower/Observer 的 ACK 消息
   - 调用 Leader 的 ACK 处理方法

4. **Follower/Observer**
   - 响应者：接收 PRE_COMMIT 消息并立即回复 ACK

### 消息流程

```
1. SyncRequestProcessor.flush()
   ↓
2. Leader.performRoundRTT(PRE_COMMIT_ROUND1, ACK_ROUND1)
   ↓
3. 发送 PRE_COMMIT_ROUND1 到所有节点
   ↓
4. 节点收到后发送 ACK_ROUND1
   ↓
5. Leader.handleRoundRTTAck() 收集 ACK
   ↓
6. 收到足够 ACK 后，开始第二轮
   ↓
7. Leader.performRoundRTT(PRE_COMMIT_ROUND2, ACK_ROUND2)
   ↓
8. 重复步骤 3-5
   ↓
9. 两轮成功后，执行 zks.getZKDatabase().commit()
```

## 修改的文件

### 1. Leader.java
- 新增消息类型常量：`PRE_COMMIT_ROUND1`, `ACK_ROUND1`, `PRE_COMMIT_ROUND2`, `ACK_ROUND2`
- 新增方法：`performRoundRTT()`, `handleRoundRTTAck()`
- 新增 ACK 跟踪机制：`round1AckTracker`, `round2AckTracker`

### 2. SyncRequestProcessor.java
- 修改 `flush()` 方法：在日志提交前调用两轮 RTT
- 新增方法：`performTwoRoundRTTBeforeCommit()`
- 新增导入：Leader, QuorumZooKeeperServer

### 3. LearnerHandler.java
- 更新 `packetToString()` 方法：添加新消息类型的字符串表示
- 更新消息处理 switch：添加 `ACK_ROUND1`, `ACK_ROUND2` 的处理逻辑

### 4. Follower.java
- 更新 `processPacket()` 方法：添加 `PRE_COMMIT_ROUND1`, `PRE_COMMIT_ROUND2` 的处理
- 自动回复对应的 ACK 消息

### 5. Observer.java
- 与 Follower 相同的修改
- 确保 Observer 也参与两轮通信

## 配置参数

### 容错阈值
```java
// 在 Leader.performRoundRTT() 中
int minRequired = Math.max(1, (totalLearners / 2) + 1); // 简单多数
```

### 超时时间
```java
// 在 Leader.performRoundRTT() 中
boolean success = responseLatch.await(2000, TimeUnit.MILLISECONDS); // 2 秒
```

## 性能影响

### 延迟增加
- 每次事务日志落盘前增加两轮网络 RTT
- 预期延迟：`2 × 网络延迟 + 处理时间`

### 吞吐量影响
- 日志批处理仍然有效
- 只在实际 flush 时进行两轮通信
- 读操作不受影响

### 资源使用
- 每轮通信使用一个 CountDownLatch
- 内存占用微量增加（ACK 跟踪 Map）

## 日志示例

### Leader 端日志
```
DEBUG RC: Starting two-round RTT communication
DEBUG RC: Starting round RTT - send: 20, expect: 21
DEBUG RC: Total learners: 2, minimum required responses: 2
DEBUG RC: Sent 20 to 2 learners
DEBUG RC: Received ACK 21 from learner 1
DEBUG RC: ACK 21 processed, remaining: 1
DEBUG RC: Round RTT completed - success: true, received: 2/2
DEBUG RC: Starting round RTT - send: 22, expect: 23
DEBUG RC: Two-round RTT completed successfully, proceeding with commit
```

### Follower 端日志
```
DEBUG RC: Received PRE_COMMIT_ROUND1, sending ACK_ROUND1
DEBUG RC: Received PRE_COMMIT_ROUND2, sending ACK_ROUND2
```

## 测试和验证

### 运行测试脚本
```bash
cd /root/code_dev/zookeeper/zookeeper-rc
./test_two_round_rtt.sh
```

### 手动验证
1. 启动 ZooKeeper 集群
2. 执行写操作
3. 检查日志中的 "RC:" 前缀消息
4. 验证两轮通信是否按预期工作

## 故障处理

### 超时处理
- 如果第一轮或第二轮超时，整个操作失败
- 抛出 `RequestProcessorException`
- 事务不会提交到磁盘

### 节点故障
- 只要收到简单多数的 ACK 即可继续
- 自动容忍少数节点故障

### 网络分区
- 如果 Leader 与多数节点失联，两轮 RTT 会失败
- 保证强一致性

## 适用场景

### 测试场景
- 网络延迟对 ZAB 协议性能的影响研究
- 分布式一致性算法的延迟分析
- 故障注入测试

### 生产考虑
- **不建议在生产环境使用**，因为会显著增加延迟
- 适用于对一致性要求极高、对性能要求相对较低的场景

## 扩展和自定义

### 修改容错阈值
修改 `Leader.performRoundRTT()` 中的 `minRequired` 计算逻辑

### 调整超时时间
修改 `responseLatch.await()` 的超时参数

### 添加更多轮次
可以仿照现有逻辑添加第三轮、第四轮通信

### 自定义消息内容
可以在 QuorumPacket 中携带额外的数据

## 注意事项

1. **性能影响**：每次写操作的延迟会显著增加
2. **兼容性**：需要所有节点都升级到支持新消息类型的版本
3. **调试**：大量日志输出，生产环境需要调整日志级别
4. **测试**：充分测试各种网络故障场景

## 版本信息

- **基于版本**：ZooKeeper RC
- **修改时间**：2025-09-17
- **修改标识**：所有修改都有 "RC MODIFICATION" 注释标记
