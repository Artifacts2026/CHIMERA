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

import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Encrypted FileOutputStream that encrypts data before writing to disk
 */
public class EncryptedFileOutputStream extends OutputStream {
    private static final Logger LOG = LoggerFactory.getLogger(EncryptedFileOutputStream.class);
    
    private final FileOutputStream delegate;
    private final boolean encryptionEnabled;
    
    public EncryptedFileOutputStream(FileOutputStream delegate) {
        this.delegate = delegate;
        this.encryptionEnabled = TdxCryptoUtil.isEncryptionEnabled();
        
        if (encryptionEnabled) {
            LOG.debug("Created encrypted output stream for file");
        }
    }
    
    @Override
    public void write(int b) throws IOException {
        write(new byte[]{(byte) b});
    }
    
    @Override
    public void write(byte[] b) throws IOException {
        write(b, 0, b.length);
    }
    
    @Override
    public void write(byte[] b, int off, int len) throws IOException {
        if (!encryptionEnabled) {
            delegate.write(b, off, len);
            return;
        }
        
        try {
            // Extract the portion to encrypt
            byte[] dataToEncrypt = new byte[len];
            System.arraycopy(b, off, dataToEncrypt, 0, len);
            
            // Encrypt the data
            byte[] encryptedData = TdxCryptoUtil.encryptData(dataToEncrypt);
            
            // Write encrypted data
            delegate.write(encryptedData);
            
        } catch (Exception e) {
            LOG.error("Failed to encrypt data, falling back to unencrypted write", e);
            delegate.write(b, off, len);
        }
    }
    
    @Override
    public void flush() throws IOException {
        delegate.flush();
    }
    
    @Override
    public void close() throws IOException {
        delegate.close();
    }
    
    // Delegate methods for FileOutputStream compatibility
    public void write(byte[] b, boolean immediateFlush) throws IOException {
        write(b);
        if (immediateFlush) {
            flush();
        }
    }
}
