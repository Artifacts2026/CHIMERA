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

#ifndef ZOOKEEPER_TDX_CRYPTO_UTIL_H
#define ZOOKEEPER_TDX_CRYPTO_UTIL_H

#include <string>
#include <stdint.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/aes.h>
#include <vector>

#ifdef TDX_ENABLED
#include <tdx_attest.h>
#endif

namespace zookeeper_tdx {

// Initialize encryption
bool init_encryption();

// Encrypt data
bool encrypt_data(const std::vector<uint8_t>& plaintext, std::vector<uint8_t>& ciphertext);

// Decrypt data
bool decrypt_data(const std::vector<uint8_t>& ciphertext, std::vector<uint8_t>& plaintext);

} // namespace zookeeper_tdx

#endif // ZOOKEEPER_TDX_CRYPTO_UTIL_H 