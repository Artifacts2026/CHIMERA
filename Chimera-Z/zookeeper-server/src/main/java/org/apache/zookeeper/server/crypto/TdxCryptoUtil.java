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
 * TDX Crypto Utility for ZooKeeper
 * Provides hardware-level encryption using Intel TDX
 */
public class TdxCryptoUtil {
    private static final Logger LOG = LoggerFactory.getLogger(TdxCryptoUtil.class);
    
    // Configuration property to enable/disable encryption
    public static final String ENCRYPTION_ENABLED_PROPERTY = "zookeeper.encryption.enabled";
    
    private static boolean initialized = false;
    private static boolean encryptionEnabled = false;
    
    static {
        try {
            // Load native library
            System.loadLibrary("zookeeper_tdx_crypto");
            initialized = true;
            
            // Check if encryption is enabled via system property
            String enabledProp = System.getProperty(ENCRYPTION_ENABLED_PROPERTY, "false");
            encryptionEnabled = Boolean.parseBoolean(enabledProp);
            
            if (encryptionEnabled) {
                LOG.info("TDX encryption is enabled for ZooKeeper");
                if (!initLogEncryption()) {
                    LOG.error("Failed to initialize TDX encryption");
                    encryptionEnabled = false;
                }
            } else {
                LOG.info("TDX encryption is disabled for ZooKeeper");
            }
        } catch (UnsatisfiedLinkError e) {
            LOG.warn("TDX crypto native library not found, encryption disabled: {}", e.getMessage());
            initialized = false;
            encryptionEnabled = false;
        }
    }
    
    /**
     * Check if encryption is available and enabled
     */
    public static boolean isEncryptionEnabled() {
        return initialized && encryptionEnabled;
    }
    
    /**
     * Initialize log encryption (native method)
     */
    public static native boolean initLogEncryption();
    
    /**
     * Encrypt data (native method)
     */
    public static native byte[] encryptDataNative(byte[] plaintext);
    
    /**
     * Decrypt data (native method)
     */
    public static native byte[] decryptDataNative(byte[] ciphertext);
    
    /**
     * Encrypt data (wrapper method)
     */
    public static byte[] encryptData(byte[] plaintext) {
        return encryptDataNative(plaintext);
    }
    
    /**
     * Decrypt data (wrapper method)
     */
    public static byte[] decryptData(byte[] ciphertext) {
        return decryptDataNative(ciphertext);
    }
    
    /**
     * Check if data is encrypted (native method)
     */
    public static native boolean isDataEncrypted(byte[] data);
    
    /**
     * Get TDX attestation report (native method)
     */
    public static native byte[] getTdxReport();
}
