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

package org.apache.zookeeper.server.crypto;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Test utility for TDX encryption functionality
 */
public class TdxCryptoTest {
    private static final Logger LOG = LoggerFactory.getLogger(TdxCryptoTest.class);
    
    public static void main(String[] args) {
        LOG.info("=== TDX Crypto Test ===");
        
        // Check if encryption is enabled
        boolean enabled = TdxCryptoUtil.isEncryptionEnabled();
        LOG.info("TDX Encryption enabled: {}", enabled);
        
        if (!enabled) {
            LOG.warn("TDX encryption is not enabled. Please check:");
            LOG.warn("1. JNI library is loaded correctly");
            LOG.warn("2. System property 'zookeeper.encryption.enabled' is set to true");
            LOG.warn("3. TDX hardware is available");
            return;
        }
        
        // Test encryption/decryption
        testEncryptionDecryption();
        
        // Print statistics
        LOG.info("=== Final Statistics ===");
        LOG.info("Test completed successfully");
    }
    
    private static void testEncryptionDecryption() {
        LOG.info("=== Testing Encryption/Decryption ===");
        
        try {
            // Test data
            String testData = "Hello ZooKeeper TDX Encryption!";
            byte[] originalData = testData.getBytes("UTF-8");
            
            LOG.info("Original data: {}", testData);
            LOG.info("Original data length: {} bytes", originalData.length);
            
            // Encrypt
            byte[] encryptedData = TdxCryptoUtil.encryptData(originalData);
            LOG.info("Encrypted data length: {} bytes", encryptedData.length);
            LOG.info("Encrypted data (hex): {}", bytesToHex(encryptedData));
            
            // Check if data is encrypted
            boolean isEncrypted = TdxCryptoUtil.isDataEncrypted(encryptedData);
            LOG.info("Data is encrypted: {}", isEncrypted);
            
            // Decrypt
            byte[] decryptedData = TdxCryptoUtil.decryptData(encryptedData);
            String decryptedString = new String(decryptedData, "UTF-8");
            
            LOG.info("Decrypted data: {}", decryptedString);
            LOG.info("Decrypted data length: {} bytes", decryptedData.length);
            
            // Verify
            boolean success = testData.equals(decryptedString);
            LOG.info("Encryption/Decryption test: {}", success ? "PASSED" : "FAILED");
            
            if (!success) {
                LOG.error("Original: '{}'", testData);
                LOG.error("Decrypted: '{}'", decryptedString);
            }
            
        } catch (Exception e) {
            LOG.error("Encryption/Decryption test failed", e);
        }
    }
    
    private static String bytesToHex(byte[] bytes) {
        StringBuilder result = new StringBuilder();
        for (byte b : bytes) {
            result.append(String.format("%02x", b));
        }
        return result.toString();
    }
}

