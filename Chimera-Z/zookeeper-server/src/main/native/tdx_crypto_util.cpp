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

#include "tdx_crypto_util.h"
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <iostream>
#include "tdx_attest.h"

// TDX密钥策略常量定义
#define TDX_KEYPOLICY_SEALING     0x1
#define TDX_KEYPOLICY_NOMIGRATE   0x2
#define TDX_KEYPOLICY_NOISOLATION 0x4

#include <stdint.h>
#define PACKED                  __attribute__((__packed__))

#define SIZE_OF_SHA256_HASH_IN_QWORDS 4
#define SIZE_OF_SHA384_HASH_IN_QWORDS 6
#define SIZE_OF_SHA384_HASH_IN_BYTES (SIZE_OF_SHA384_HASH_IN_QWORDS << 3)
typedef union measurement_u
{
    uint64_t qwords[SIZE_OF_SHA384_HASH_IN_QWORDS];
    uint8_t  bytes[SIZE_OF_SHA384_HASH_IN_BYTES];
} measurement_t;

// REPORTTYPE indicates the reported Trusted Execution Environment (TEE) type, sub-type and version.
typedef union PACKED td_report_type_s
{
	struct
	{
		//
		// Trusted Execution Environment (TEE) Type:
		//      0x00:   SGX
		//      0x7F-0x01:  Reserved (TEE implemented by CPU)
		//      0x80:   Reserved (TEE implemented by SEAM module)
		//      0x81:   TDX
		//      0xFF-0x82:  Reserved (TEE implemented by SEAM module)
		//
		uint8_t type;
		uint8_t subtype; 	// TYPE-specific subtype
		uint8_t version; 	// TYPE-specific version.
		uint8_t reserved; 	// Must be zero
	};
	uint32_t raw;
} td_report_type_t;

#define CPUSVN_SIZE                       16 // < CPUSVN is a 16B Security Version Number of the CPU.
#define SIZE_OF_REPORTDATA_IN_BYTES       64
#define SIZE_OF_REPORTMAC_STRUCT_IN_BYTES 256

// REPORTMACSTRUCT is common to all TEEs (SGX and TDX).
typedef struct PACKED report_mac_struct_s
{
	td_report_type_t  report_type; 			// Type Header Structure
	uint8_t           reserved_0[12]; 		// < Must be 0
	uint8_t           cpusvn[CPUSVN_SIZE]; 	// < CPU SVN
	// SHA384 of TEETCBINFO for TEEs implemented using a SEAM
	uint8_t          tee_tcb_info_hash[SIZE_OF_SHA384_HASH_IN_QWORDS * 8];
	//SHA384 of TEEINFO, which is a TEE-specific info structure (TDINFO or SGXINFO), or 0 if no TEE is represented
	uint8_t          tee_info_hash[SIZE_OF_SHA384_HASH_IN_QWORDS * 8];
	// A set of data used for communication between the caller and the target.
	uint8_t           report_data[SIZE_OF_REPORTDATA_IN_BYTES];
	uint8_t           reserved_1[32];
	uint8_t          mac[SIZE_OF_SHA256_HASH_IN_QWORDS * 8]; // < The MAC over the REPORTMACSTRUCT with model-specific MAC
} report_mac_struct_t;

#define SIZE_OF_TEE_TCB_SVN_IN_BYTES         16
typedef struct PACKED tee_tcb_info_s
{
	//
	// Indicates TEE_TCB_INFO fields which are valid.
	// - 1 in the i-th significant bit reflects that the field starting at offset (8 * i)
	// - 0 in the i-th significant bit reflects that either no field starts at offset (8 * i)
	//   or that field is not populated and is set to zero.
	//
	uint64_t       valid;
	uint8_t        tee_tcb_svn[SIZE_OF_TEE_TCB_SVN_IN_BYTES];  // < TEE_TCB_SVN Array
	measurement_t  mr_seam;  // < Measurement of the SEAM module
	//
	// Measurement of SEAM module signer if non-intel SEAM module was loaded
	//
	measurement_t  mr_signer_seam;
	uint64_t       attributes;  // < Additional configuration ATTRIBUTES if non-intel SEAM module was loaded
	uint8_t        reserved[128];  // Must be 0
} tee_tcb_info_t;

#define NUM_OF_RTMRS                    4
#define SIZE_OF_TD_INFO_STRUCT_IN_BYTES 512

//
// @struct td_info_s
//
// @brief TDINFO_STRUCT is the TDX-specific TEEINFO part of TDGMRREPORT.
//
// It contains the measurements and initial configuration of the TD that was locked at initialization,
// and a set of measurement registers that are run-time extendible.
// These values are copied from the TDCS by the TDGMRREPORT function.
//
typedef struct PACKED td_info_s
{
    uint64_t       attributes; 	// < TD's ATTRIBUTES
    uint64_t       xfam; 		// < TD's XFAM
    measurement_t  mr_td; 		// < Measurement of the initial contents of the TD
    //
    // 48 Software defined ID for additional configuration for the software in the TD
    //
    measurement_t  mr_config_id;
    measurement_t  mr_owner; 	// < Software defined ID for TD's owner
    //
    // Software defined ID for owner-defined configuration of the guest TD,
    // e.g., specific to the workload rather than the runtime or OS.
    //
    measurement_t  mr_owner_config;
    // measurement_t  rtmr[NUM_OF_RTMRS]; // <  Array of NUM_RTMRS runtime extendable measurement registers
	measurement_t  rtmr0;
	measurement_t  rtmr1;
	measurement_t  rtmr2;
	measurement_t  rtmr3;
    uint8_t        reserved[112];
} td_info_t;

#define SIZE_OF_TD_REPORT_STRUCT_IN_BYTES 1024

//
// @struct td_report_t
//
// @brief TDREPORT_STRUCT is the output of the TDGMRREPORT function.
//
// If is composed of a generic MAC structure, a SEAMINFO structure and
// a TDX-specific TEE info structure.
//
typedef struct PACKED td_report_s
{
    report_mac_struct_t  report_mac_struct; // < REPORTMACSTRUCT for the TDGMRREPORT
    //
    // Additional attestable elements in the TD's TCB not reflected in the REPORTMACSTRUCT.CPUSVN.
    // Includes the SEAM measurements.
    //
    tee_tcb_info_t       tee_tcb_info;
    td_info_t            td_info; 			// < TD's attestable properties
} td_report_t;

#define devname		"/dev/tdx_guest"

#define HEX_DUMP_SIZE	16
#define MAX_ROW_SIZE	70

namespace zookeeper_tdx {

// Global AES key and initialization vector
static uint8_t g_encryption_key[32]; // Key for encryption
static uint8_t g_encryption_iv[16];  // IV for encryption
static bool g_encryption_initialized = false;

// Error handling
static void handle_openssl_error(const char* operation) {
    char err_buf[256];
    unsigned long err = ERR_get_error();
    ERR_error_string_n(err, err_buf, sizeof(err_buf));
    std::cerr << "OpenSSL error during " << operation << ": " << err_buf << std::endl;
}

void gen_report_data(uint8_t *reportdata) {
    srand(time(NULL));
    for (int i = 0; i < TDX_REPORT_DATA_SIZE; i++) {
        reportdata[i] = rand();
    }
}

int derive_mr_td_with_id_sealing_key(const td_report_t* tdx_report, 
                                    const char* custom_id,
                                    uint8_t* key_buffer, 
                                    uint8_t* iv_buffer) {
    if (!tdx_report || !custom_id || !key_buffer || !iv_buffer) {
        return -1;
    }
    
    uint8_t binding_material[SIZE_OF_SHA384_HASH_IN_BYTES + 128] = {0};
    size_t offset = 0;
    
    memcpy(binding_material, tdx_report->td_info.mr_td.bytes, SIZE_OF_SHA384_HASH_IN_BYTES);
    offset += SIZE_OF_SHA384_HASH_IN_BYTES;
    
    memcpy(binding_material + offset, custom_id, strlen(custom_id));
    offset += strlen(custom_id);
    
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, binding_material, offset);
    
    uint8_t temp_key[32];
    SHA256_Final(temp_key, &sha256);
    memcpy(key_buffer, temp_key, 16);
    memcpy(iv_buffer, temp_key + 16, 16);
    
    memset(binding_material, 0, sizeof(binding_material));
    
    return 0;
}

bool init_encryption() {
    if (g_encryption_initialized) {
        return true;
    }
    
    tdx_report_data_t report_data = {{0}};
    td_report_t tdx_report = {{0}};

    gen_report_data(report_data.d);

    if (TDX_ATTEST_SUCCESS != tdx_att_get_report(&report_data, (tdx_report_t *)&tdx_report)) {
        std::cerr << "Failed to get TDX report" << std::endl;
        return false;
    }

    int result = derive_mr_td_with_id_sealing_key(&tdx_report, "zookeeper_encryption", g_encryption_key, g_encryption_iv);
    
    if (result != 0) {
        std::cerr << "Failed to initialize encryption" << std::endl;
        return false;
    } else {
        g_encryption_initialized = true;
    }
    
    return true;
}

bool encrypt_data(const std::vector<uint8_t>& plaintext, std::vector<uint8_t>& ciphertext) {
    if (!g_encryption_initialized) {
        ciphertext = plaintext;
        return true;
    }
    
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        handle_openssl_error("create context");
        return false;
    }
    
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, g_encryption_key, g_encryption_iv) != 1) {
        handle_openssl_error("encrypt init");
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    
    size_t plaintext_len = plaintext.size();
    int block_size = EVP_CIPHER_CTX_block_size(ctx);
    size_t max_output_len = plaintext_len + block_size;
    
    ciphertext.resize(max_output_len);
    int out_len = 0;
    int total_out_len = 0;
    
    if (EVP_EncryptUpdate(ctx, ciphertext.data(), &out_len, 
                         plaintext.data(), plaintext_len) != 1) {
        handle_openssl_error("encrypt update");
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    
    total_out_len = out_len;
    
    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + total_out_len, &out_len) != 1) {
        handle_openssl_error("encrypt final");
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    
    total_out_len += out_len;
    ciphertext.resize(total_out_len);
    
    EVP_CIPHER_CTX_free(ctx);
    
    return true;
}

bool decrypt_data(const std::vector<uint8_t>& ciphertext, std::vector<uint8_t>& plaintext) {
    if (!g_encryption_initialized) {
        plaintext = ciphertext;
        return true;
    }
    
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        handle_openssl_error("create context");
        return false;
    }
    
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, g_encryption_key, g_encryption_iv) != 1) {
        handle_openssl_error("decrypt init");
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    
    size_t ciphertext_len = ciphertext.size();
    int block_size = EVP_CIPHER_CTX_block_size(ctx);
    size_t max_output_len = ciphertext_len;
    
    plaintext.resize(max_output_len);
    int out_len = 0;
    int total_out_len = 0;
    
    if (EVP_DecryptUpdate(ctx, plaintext.data(), &out_len, 
                         ciphertext.data(), ciphertext_len) != 1) {
        handle_openssl_error("decrypt update");
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    
    total_out_len = out_len;
    
    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + total_out_len, &out_len) != 1) {
        handle_openssl_error("decrypt final");
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    
    total_out_len += out_len;
    plaintext.resize(total_out_len);
    
    EVP_CIPHER_CTX_free(ctx);
    
    return true;
}

} // namespace zookeeper_tdx 