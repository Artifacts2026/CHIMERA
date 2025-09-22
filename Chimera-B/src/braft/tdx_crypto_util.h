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

#ifndef BRAFT_TDX_CRYPTO_UTIL_H
#define BRAFT_TDX_CRYPTO_UTIL_H

#include <string>
#include <stdint.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/aes.h>
#include <butil/iobuf.h>

#ifdef TDX_ENABLED
#include <tdx_attest.h>
#endif

namespace braft {

// Initialize log encryption
bool init_log_encryption();

// Initialize snapshot encryption
// bool init_snapshot_encryption();

// Encrypt IOBuf data
bool encrypt_data(const butil::IOBuf& plaintext, butil::IOBuf* ciphertext);

// Decrypt IOBuf data
bool decrypt_data(const butil::IOBuf& ciphertext, butil::IOBuf* plaintext);

} // namespace braft

#endif // BRAFT_TDX_CRYPTO_UTIL_H 