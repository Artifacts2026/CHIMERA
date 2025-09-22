// Copyright (c) 2024 New Engraft Authors. All Rights Reserved
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

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>

#ifdef TDX_ENABLED
#include <tdx_attest.h>
#endif

// 打印十六进制数据
void print_hex(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        printf("%02x", data[i]);
    }
    printf("\n");
}

// 从TDX派生密钥（如果支持TDX）
int derive_key_from_tdx(uint8_t* key, uint8_t* iv, const char* purpose_id) {
#ifdef TDX_ENABLED
    printf("测试 purpose_id: %s\n", purpose_id);
    
    // 从TDX TD Report中获取mr_td度量值并派生密钥
    tdx_report_t* report = NULL;
    tdx_report_data_t report_data = {0};
    uint32_t report_size = 0;
    
    // 将purpose_id作为report_data的一部分
    strncpy((char*)report_data.d, purpose_id, sizeof(report_data.d) - 1);
    
    // 获取TDX报告
    if (tdx_att_get_report(&report_data, &report, &report_size) != TDX_ATTEST_SUCCESS) {
        printf("无法获取TDX报告，使用随机密钥\n");
        return -1;
    }
    
    // 使用mr_td作为密钥材料
    SHA256((const unsigned char*)&report->mr_td, sizeof(report->mr_td), key);
    // 使用mr_td的前16字节作为IV
    memcpy(iv, &report->mr_td, 16);
    
    // 打印密钥信息
    printf("生成的密钥: ");
    print_hex(key, 32);
    printf("生成的IV: ");
    print_hex(iv, 16);
    
    // 释放报告
    tdx_att_free_report(report);
    return 0;
#else
    printf("TDX未启用，此平台不支持TDX\n");
    return -1;
#endif
}

int main() {
    printf("TDX密钥派生测试程序\n");
    printf("===================\n\n");
    
    // 测试不同的purpose_id
    const char* test_purposes[] = {
        "braft_log_encryption",
        "braft_snapshot_encryption",
        "test_purpose_1",
        "test_purpose_2",
        "this_is_a_very_long_purpose_id_to_test_truncation_behavior"
    };
    
    uint8_t key[32];
    uint8_t iv[16];
    
    for (int i = 0; i < sizeof(test_purposes) / sizeof(test_purposes[0]); i++) {
        printf("\n测试 #%d:\n", i + 1);
        memset(key, 0, sizeof(key));
        memset(iv, 0, sizeof(iv));
        
        int ret = derive_key_from_tdx(key, iv, test_purposes[i]);
        if (ret != 0) {
#ifdef TDX_ENABLED
            printf("从TDX派生密钥失败，生成随机密钥代替\n");
            
            // 随机生成密钥和IV
            if (RAND_bytes(key, 32) != 1) {
                printf("生成随机密钥失败\n");
                continue;
            }
            if (RAND_bytes(iv, 16) != 1) {
                printf("生成随机IV失败\n");
                continue;
            }
            
            printf("随机生成的密钥: ");
            print_hex(key, 32);
            printf("随机生成的IV: ");
            print_hex(iv, 16);
#else
            printf("此平台不支持TDX，跳过测试\n");
#endif
        }
        
        printf("-----------------\n");
    }
    
    printf("\n测试完成\n");
    return 0;
} 