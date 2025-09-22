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

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Factory for creating encrypted/decrypted file streams
 * Automatically handles encryption based on configuration and file types
 */
public class CryptoFileFactory {
    private static final Logger LOG = LoggerFactory.getLogger(CryptoFileFactory.class);
    
    /**
     * Create an OutputStream for the given file
     * Will be encrypted if encryption is enabled and file type requires it
     */
    public static OutputStream createOutputStream(File file) throws FileNotFoundException {
        FileOutputStream fos = new FileOutputStream(file);
        
        if (shouldEncryptFile(file)) {
            LOG.debug("Creating encrypted output stream for file: {}", file.getName());
            return new EncryptedFileOutputStream(fos);
        } else {
            return fos;
        }
    }
    
    /**
     * Create an InputStream for the given file
     * Will automatically detect and decrypt if file is encrypted
     */
    public static InputStream createInputStream(File file) throws FileNotFoundException {
        FileInputStream fis = new FileInputStream(file);
        
        if (TdxCryptoUtil.isEncryptionEnabled()) {
            LOG.debug("Creating potentially decrypted input stream for file: {}", file.getName());
            return new DecryptedFileInputStream(fis);
        } else {
            return fis;
        }
    }
    
    /**
     * Determine if a file should be encrypted based on its name/type
     */
    private static boolean shouldEncryptFile(File file) {
        if (!TdxCryptoUtil.isEncryptionEnabled()) {
            return false;
        }
        
        String fileName = file.getName().toLowerCase();
        
        // Encrypt transaction logs
        if (fileName.startsWith("log.") || fileName.contains("txnlog")) {
            return true;
        }
        
        // Encrypt epoch files
        if (fileName.equals("currentepoch") || fileName.equals("acceptedepoch")) {
            return true;
        }
        
        // Don't encrypt snapshots for now (user requested)
        if (fileName.startsWith("snapshot.")) {
            return false;
        }
        
        // Don't encrypt temporary files
        if (fileName.endsWith(".tmp") || fileName.endsWith(".temp")) {
            return false;
        }
        
        // Default: don't encrypt unless specifically needed
        return false;
    }
    
    /**
     * Check if a file is likely encrypted based on its content
     */
    public static boolean isFileEncrypted(File file) {
        if (!TdxCryptoUtil.isEncryptionEnabled() || !file.exists()) {
            return false;
        }
        
        try (FileInputStream fis = new FileInputStream(file)) {
            byte[] header = new byte[256];
            int bytesRead = fis.read(header);
            
            if (bytesRead > 0) {
                return TdxCryptoUtil.isDataEncrypted(header);
            }
        } catch (Exception e) {
            LOG.debug("Failed to check if file is encrypted: {}", file.getName(), e);
        }
        
        return false;
    }
}
