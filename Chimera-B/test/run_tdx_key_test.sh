#!/bin/bash

# 编译测试程序
cd ..
mkdir -p build
cd build
cmake .. -DENABLE_TDX=ON
make test_tdx_key_derivation

# 清除之前的输出
echo > tdx_key_test_results.txt

# 运行测试程序
echo "开始运行TDX密钥派生测试..."
./test/test_tdx_key_derivation | tee tdx_key_test_results.txt

# 检查结果
echo -e "\n结果分析:"
if grep -q "生成的密钥" tdx_key_test_results.txt; then
    echo "测试成功生成了密钥！"
    
    # 提取并比较不同purpose_id生成的密钥
    echo -e "\n密钥比较:"
    grep -A 1 "测试 purpose_id" tdx_key_test_results.txt | grep -v "\-\-" > keys.txt
    
    # 检查密钥是否不同
    PREV_KEY=""
    KEYS_DIFFER=true
    while read -r PURPOSE && read -r KEY; do
        echo "Purpose: $PURPOSE"
        echo "Key: $KEY"
        
        if [ -n "$PREV_KEY" ] && [ "$KEY" = "$PREV_KEY" ]; then
            KEYS_DIFFER=false
            echo "警告: 该密钥与前一个相同!"
        fi
        
        PREV_KEY="$KEY"
        echo ""
    done < keys.txt
    
    if $KEYS_DIFFER; then
        echo "所有密钥都不同 - 测试通过！"
    else
        echo "有些密钥相同 - 测试失败!"
    fi
else
    echo "测试未能生成密钥，可能平台不支持TDX或缺少TDX SDK"
fi

echo -e "\n测试完成!" 