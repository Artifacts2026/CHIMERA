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

import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Decrypted FileInputStream that automatically detects and decrypts encrypted files
 */
public class DecryptedFileInputStream extends InputStream {
    private static final Logger LOG = LoggerFactory.getLogger(DecryptedFileInputStream.class);
    
    private final FileInputStream delegate;
    private final boolean encryptionEnabled;
    private byte[] decryptedBuffer;
    private int bufferPosition = 0;
    private boolean isEncryptedFile = false;
    private boolean headerChecked = false;
    
    public DecryptedFileInputStream(FileInputStream delegate) {
        this.delegate = delegate;
        this.encryptionEnabled = TdxCryptoUtil.isEncryptionEnabled();
    }
    
    @Override
    public int read() throws IOException {
        if (!headerChecked) {
            checkIfEncrypted();
        }
        
        if (!encryptionEnabled || !isEncryptedFile) {
            return delegate.read();
        }
        
        if (decryptedBuffer == null || bufferPosition >= decryptedBuffer.length) {
            fillDecryptedBuffer();
        }
        
        if (decryptedBuffer == null || bufferPosition >= decryptedBuffer.length) {
            return -1; // EOF
        }
        
        return decryptedBuffer[bufferPosition++] & 0xFF;
    }
    
    @Override
    public int read(byte[] b) throws IOException {
        return read(b, 0, b.length);
    }
    
    @Override
    public int read(byte[] b, int off, int len) throws IOException {
        if (!headerChecked) {
            checkIfEncrypted();
        }
        
        if (!encryptionEnabled || !isEncryptedFile) {
            return delegate.read(b, off, len);
        }
        
        int totalRead = 0;
        while (totalRead < len) {
            if (decryptedBuffer == null || bufferPosition >= decryptedBuffer.length) {
                fillDecryptedBuffer();
                if (decryptedBuffer == null || bufferPosition >= decryptedBuffer.length) {
                    break; // EOF
                }
            }
            
            int available = decryptedBuffer.length - bufferPosition;
            int toRead = Math.min(len - totalRead, available);
            System.arraycopy(decryptedBuffer, bufferPosition, b, off + totalRead, toRead);
            bufferPosition += toRead;
            totalRead += toRead;
        }
        
        return totalRead > 0 ? totalRead : -1;
    }
    
    private void checkIfEncrypted() throws IOException {
        headerChecked = true;
        
        if (!encryptionEnabled) {
            return;
        }
        
        // Check if the delegate supports mark/reset
        if (!delegate.markSupported()) {
            LOG.debug("Delegate InputStream does not support mark/reset, skipping encryption check");
            isEncryptedFile = false;
            return;
        }
        
        // Mark current position
        delegate.mark(1024);
        
        try {
            // Read a small amount to check if encrypted
            byte[] header = new byte[256];
            int bytesRead = delegate.read(header);
            
            if (bytesRead > 0) {
                isEncryptedFile = TdxCryptoUtil.isDataEncrypted(header);
                LOG.debug("File encryption detected: {}", isEncryptedFile);
            }
        } catch (Exception e) {
            LOG.warn("Failed to check file encryption status", e);
        } finally {
            // Reset to beginning
            try {
                delegate.reset();
            } catch (IOException e) {
                LOG.warn("Failed to reset InputStream position", e);
                // If reset fails, we can't continue reading from this stream
                throw new IOException("Cannot reset InputStream position after encryption check", e);
            }
        }
    }
    
    private void fillDecryptedBuffer() throws IOException {
        try {
            // Read encrypted chunk
            byte[] encryptedChunk = new byte[4096]; // Read in chunks
            int bytesRead = delegate.read(encryptedChunk);
            
            if (bytesRead <= 0) {
                decryptedBuffer = null;
                return;
            }
            
            // Trim to actual size
            if (bytesRead < encryptedChunk.length) {
                byte[] trimmed = new byte[bytesRead];
                System.arraycopy(encryptedChunk, 0, trimmed, 0, bytesRead);
                encryptedChunk = trimmed;
            }
            
            // Decrypt
            decryptedBuffer = TdxCryptoUtil.decryptData(encryptedChunk);
            bufferPosition = 0;
            
        } catch (Exception e) {
            LOG.error("Failed to decrypt data", e);
            decryptedBuffer = null;
        }
    }
    
    @Override
    public void close() throws IOException {
        delegate.close();
    }
    
    @Override
    public int available() throws IOException {
        if (!encryptionEnabled || !isEncryptedFile) {
            return delegate.available();
        }
        
        int delegateAvailable = delegate.available();
        int bufferAvailable = (decryptedBuffer != null) ? 
            (decryptedBuffer.length - bufferPosition) : 0;
        
        return bufferAvailable + (delegateAvailable > 0 ? 1 : 0); // Approximate
    }
}
