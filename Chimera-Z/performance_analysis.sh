#!/bin/bash

# 性能分析脚本：分析两轮 RTT 对 ZooKeeper 性能的影响
echo "=== ZooKeeper RC 两轮 RTT 性能分析 ==="
echo

echo "📊 理论性能影响分析："
echo

# 网络延迟分析
echo "🌐 网络延迟影响："
echo "假设单次网络 RTT 延迟为 X ms，则："
echo "  • 原始 ZAB 每次提交延迟: ~X ms (Leader-Follower 通信)"
echo "  • 添加两轮 RTT 后延迟: ~3X ms (原始 + 2轮额外通信)"
echo "  • 延迟增加倍数: ~3倍"
echo

echo "📈 不同网络环境下的预期影响："

# LAN 环境
echo "  🏠 局域网 (LAN, RTT ~1ms):"
echo "    - 原始延迟: ~1ms"
echo "    - 修改后延迟: ~3ms"
echo "    - 延迟增加: +2ms"
echo

# WAN 环境  
echo "  🌍 广域网 (WAN, RTT ~50ms):"
echo "    - 原始延迟: ~50ms"
echo "    - 修改后延迟: ~150ms"
echo "    - 延迟增加: +100ms"
echo

# 跨大陆
echo "  🛰️  跨大陆 (RTT ~200ms):"
echo "    - 原始延迟: ~200ms"  
echo "    - 修改后延迟: ~600ms"
echo "    - 延迟增加: +400ms"
echo

echo "🔢 吞吐量影响分析："
echo
echo "假设原始每秒可处理 N 个事务，则："

# 计算吞吐量影响
for rtt in 1 10 50 100 200; do
    original_tps=$((1000 / rtt))
    modified_tps=$((1000 / (rtt * 3)))
    reduction=$((original_tps - modified_tps))
    reduction_percent=$(((reduction * 100) / original_tps))
    
    echo "  • RTT ${rtt}ms: 原始 ~${original_tps} TPS → 修改后 ~${modified_tps} TPS (降低 ${reduction_percent}%)"
done

echo
echo "⚠️  关键影响因素："
echo "  1. 网络延迟：影响最大，呈线性增长"
echo "  2. 节点数量：影响中等，更多节点需要更多等待时间" 
echo "  3. 网络稳定性：影响中等，不稳定网络会增加超时概率"
echo "  4. CPU/内存：影响较小，主要是网络 I/O 瓶颈"
echo

echo "🎯 适用场景建议："
echo "  ✅ 推荐使用："
echo "    - 研究和测试环境"
echo "    - 网络延迟分析"
echo "    - 一致性算法研究"
echo "    - 对延迟不敏感但对一致性要求极高的场景"
echo
echo "  ❌ 不推荐使用："
echo "    - 高频写入的生产环境"
echo "    - 对延迟敏感的实时系统"
echo "    - 跨地域部署的集群"
echo "    - 网络不稳定的环境"
echo

echo "🔧 优化建议："
echo "  1. 调整超时时间：根据网络环境调整 2 秒超时"
echo "  2. 减少容错要求：如果网络稳定，可以降低所需 ACK 数量"
echo "  3. 批量优化：确保事务批处理机制仍然有效"
echo "  4. 监控告警：密切监控两轮 RTT 的成功率和延迟"
echo

echo "📝 测试建议："
echo "  1. 基准测试：先在原始版本上测试性能基线"
echo "  2. 对比测试：在相同环境下对比修改前后的性能"
echo "  3. 压力测试：测试高并发情况下的表现"
echo "  4. 故障测试：模拟网络分区、节点故障等场景"
echo "  5. 长期测试：观察长时间运行的稳定性"
echo

echo "🔍 监控指标："
echo "  • 两轮 RTT 成功率"
echo "  • 平均 RTT 完成时间"  
echo "  • 超时发生频率"
echo "  • 事务提交延迟"
echo "  • 整体系统吞吐量"
echo

echo "=== 分析完成 ==="
