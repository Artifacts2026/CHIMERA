/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "zookeeper_tdx_crypto_jni.h"
#include "tdx_crypto_util.h"
#include <cstring>
#include <vector>
#include <stdexcept>

// Magic bytes to identify encrypted data
static const char ENCRYPTION_MAGIC[] = "ZKENC";
static const size_t MAGIC_SIZE = 5;

/**
 * Helper function to convert Java byte array to std::vector<uint8_t>
 */
static std::vector<uint8_t> jbyteArrayToVector(JNIEnv *env, jbyteArray jarray) {
    jsize len = env->GetArrayLength(jarray);
    jbyte* data = env->GetByteArrayElements(jarray, nullptr);
    
    std::vector<uint8_t> vec(data, data + len);
    
    env->ReleaseByteArrayElements(jarray, data, JNI_ABORT);
    return vec;
}

/**
 * Helper function to convert std::vector<uint8_t> to Java byte array
 */
static jbyteArray vectorToJbyteArray(JNIEnv *env, const std::vector<uint8_t>& vec) {
    size_t size = vec.size();
    jbyteArray result = env->NewByteArray(size);
    
    if (result == nullptr) {
        return nullptr; // OutOfMemoryError thrown
    }
    
    env->SetByteArrayRegion(result, 0, size, reinterpret_cast<const jbyte*>(vec.data()));
    return result;
}

/**
 * Helper function to add encryption magic header
 */
static std::vector<uint8_t> addEncryptionHeader(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> result;
    result.reserve(MAGIC_SIZE + data.size());
    
    // Add magic header
    result.insert(result.end(), ENCRYPTION_MAGIC, ENCRYPTION_MAGIC + MAGIC_SIZE);
    // Add data
    result.insert(result.end(), data.begin(), data.end());
    
    return result;
}

/**
 * Helper function to remove encryption header and validate
 */
static std::vector<uint8_t> removeEncryptionHeader(const std::vector<uint8_t>& data) {
    if (data.size() < MAGIC_SIZE) {
        throw std::runtime_error("Data too small to contain encryption header");
    }
    
    // Check magic header
    if (memcmp(data.data(), ENCRYPTION_MAGIC, MAGIC_SIZE) != 0) {
        throw std::runtime_error("Invalid encryption header");
    }
    
    // Return data without header
    return std::vector<uint8_t>(data.begin() + MAGIC_SIZE, data.end());
}

/*
 * Class:     org_apache_zookeeper_server_crypto_TdxCryptoUtil
 * Method:    initLogEncryption
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL Java_org_apache_zookeeper_server_crypto_TdxCryptoUtil_initLogEncryption
  (JNIEnv *env, jclass clazz) {
    try {
        return zookeeper_tdx::init_encryption() ? JNI_TRUE : JNI_FALSE;
    } catch (const std::exception& e) {
        // Convert C++ exception to Java exception
        jclass exceptionClass = env->FindClass("java/lang/RuntimeException");
        env->ThrowNew(exceptionClass, e.what());
        return JNI_FALSE;
    }
}

/*
 * Class:     org_apache_zookeeper_server_crypto_TdxCryptoUtil
 * Method:    encryptDataNative
 * Signature: ([B)[B
 */
JNIEXPORT jbyteArray JNICALL Java_org_apache_zookeeper_server_crypto_TdxCryptoUtil_encryptDataNative
  (JNIEnv *env, jclass clazz, jbyteArray plaintext) {
    try {
        if (plaintext == nullptr) {
            return nullptr;
        }
        
        // Convert Java array to vector
        std::vector<uint8_t> plaintextVec = jbyteArrayToVector(env, plaintext);
        
        // Encrypt the data
        std::vector<uint8_t> ciphertext;
        bool success = zookeeper_tdx::encrypt_data(plaintextVec, ciphertext);
        
        if (!success) {
            jclass exceptionClass = env->FindClass("java/lang/RuntimeException");
            env->ThrowNew(exceptionClass, "Encryption failed");
            return nullptr;
        }
        
        // Add encryption header
        std::vector<uint8_t> encryptedWithHeader = addEncryptionHeader(ciphertext);
        
        // Convert back to Java array
        return vectorToJbyteArray(env, encryptedWithHeader);
        
    } catch (const std::exception& e) {
        jclass exceptionClass = env->FindClass("java/lang/RuntimeException");
        env->ThrowNew(exceptionClass, e.what());
        return nullptr;
    }
}

/*
 * Class:     org_apache_zookeeper_server_crypto_TdxCryptoUtil
 * Method:    decryptDataNative
 * Signature: ([B)[B
 */
JNIEXPORT jbyteArray JNICALL Java_org_apache_zookeeper_server_crypto_TdxCryptoUtil_decryptDataNative
  (JNIEnv *env, jclass clazz, jbyteArray ciphertext) {
    try {
        if (ciphertext == nullptr) {
            return nullptr;
        }
        
        // Convert Java array to vector
        std::vector<uint8_t> ciphertextVec = jbyteArrayToVector(env, ciphertext);
        
        // Remove and validate encryption header
        std::vector<uint8_t> actualCiphertext = removeEncryptionHeader(ciphertextVec);
        
        // Decrypt the data
        std::vector<uint8_t> plaintext;
        bool success = zookeeper_tdx::decrypt_data(actualCiphertext, plaintext);
        
        if (!success) {
            jclass exceptionClass = env->FindClass("java/lang/RuntimeException");
            env->ThrowNew(exceptionClass, "Decryption failed");
            return nullptr;
        }
        
        // Convert back to Java array
        return vectorToJbyteArray(env, plaintext);
        
    } catch (const std::exception& e) {
        jclass exceptionClass = env->FindClass("java/lang/RuntimeException");
        env->ThrowNew(exceptionClass, e.what());
        return nullptr;
    }
}

/*
 * Class:     org_apache_zookeeper_server_crypto_TdxCryptoUtil
 * Method:    isDataEncrypted
 * Signature: ([B)Z
 */
JNIEXPORT jboolean JNICALL Java_org_apache_zookeeper_server_crypto_TdxCryptoUtil_isDataEncrypted
  (JNIEnv *env, jclass clazz, jbyteArray data) {
    try {
        if (data == nullptr) {
            return JNI_FALSE;
        }
        
        jsize len = env->GetArrayLength(data);
        if (len < MAGIC_SIZE) {
            return JNI_FALSE;
        }
        
        // Get first few bytes to check magic header
        jbyte* bytes = env->GetByteArrayElements(data, nullptr);
        bool isEncrypted = (memcmp(bytes, ENCRYPTION_MAGIC, MAGIC_SIZE) == 0);
        env->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
        
        return isEncrypted ? JNI_TRUE : JNI_FALSE;
        
    } catch (const std::exception& e) {
        return JNI_FALSE;
    }
}

/*
 * Class:     org_apache_zookeeper_server_crypto_TdxCryptoUtil
 * Method:    getTdxReport
 * Signature: ()[B
 */
JNIEXPORT jbyteArray JNICALL Java_org_apache_zookeeper_server_crypto_TdxCryptoUtil_getTdxReport
  (JNIEnv *env, jclass clazz) {
    try {
        // This would call a function to get TDX report
        // For now, return empty array as placeholder
        jbyteArray result = env->NewByteArray(0);
        return result;
        
    } catch (const std::exception& e) {
        jclass exceptionClass = env->FindClass("java/lang/RuntimeException");
        env->ThrowNew(exceptionClass, e.what());
        return nullptr;
    }
}
