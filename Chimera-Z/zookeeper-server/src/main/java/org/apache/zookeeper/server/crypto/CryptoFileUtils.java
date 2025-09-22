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

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.nio.charset.StandardCharsets;

/**
 * Utility class for file I/O operations with encryption support
 * Provides convenient methods for reading/writing files with automatic encryption
 */
public class CryptoFileUtils {
    
    /**
     * Create a new InputStream for the given file
     * Automatically handles decryption if needed
     */
    public static InputStream newInputStream(File file) throws FileNotFoundException {
        return CryptoFileFactory.createInputStream(file);
    }
    
    /**
     * Create a new OutputStream for the given file
     * Automatically handles encryption if needed
     */
    public static OutputStream newOutputStream(File file) throws FileNotFoundException {
        return CryptoFileFactory.createOutputStream(file);
    }
    
    /**
     * Create a new BufferedInputStream for the given file
     */
    public static BufferedInputStream newBufferedInputStream(File file) throws FileNotFoundException {
        return new BufferedInputStream(newInputStream(file));
    }
    
    /**
     * Create a new BufferedOutputStream for the given file
     */
    public static BufferedOutputStream newBufferedOutputStream(File file) throws FileNotFoundException {
        return new BufferedOutputStream(newOutputStream(file));
    }
    
    /**
     * Create a new BufferedReader for the given file using UTF-8 encoding
     */
    public static BufferedReader newBufferedReader(File file) throws FileNotFoundException {
        return new BufferedReader(
            new InputStreamReader(newInputStream(file), StandardCharsets.UTF_8)
        );
    }
    
    /**
     * Create a new BufferedWriter for the given file using UTF-8 encoding
     */
    public static BufferedWriter newBufferedWriter(File file) throws FileNotFoundException {
        return new BufferedWriter(
            new OutputStreamWriter(newOutputStream(file), StandardCharsets.UTF_8)
        );
    }
    
    /**
     * Check if encryption is enabled for the system
     */
    public static boolean isEncryptionEnabled() {
        return TdxCryptoUtil.isEncryptionEnabled();
    }
    
    /**
     * Check if a specific file is encrypted
     */
    public static boolean isFileEncrypted(File file) {
        return CryptoFileFactory.isFileEncrypted(file);
    }
}
