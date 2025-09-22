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
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.FilterOutputStream;
import java.io.IOException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * An atomic file output stream that supports encryption
 * Extends the functionality of AtomicFileOutputStream with encryption capabilities
 */
public class CryptoAtomicFileOutputStream extends FilterOutputStream {
    
    public static final String TMP_EXTENSION = ".tmp";
    
    private static final Logger LOG = LoggerFactory.getLogger(CryptoAtomicFileOutputStream.class);
    
    private final File origFile;
    private final File tmpFile;
    private final boolean encryptionEnabled;
    
    public CryptoAtomicFileOutputStream(File f) throws FileNotFoundException {
        // Call parent constructor with a temporary output stream
        // We'll replace it after initialization
        super(new FileOutputStream(new File(f.getParentFile(), f.getName() + TMP_EXTENSION)));
        
        // Now set instance variables
        this.tmpFile = new File(f.getParentFile(), f.getName() + TMP_EXTENSION);
        this.origFile = f;
        this.encryptionEnabled = TdxCryptoUtil.isEncryptionEnabled();
        
        // Replace the output stream if encryption is enabled
        if (this.encryptionEnabled) {
            try {
                super.out.close(); // Close the original stream
                super.out = new EncryptedFileOutputStream(new FileOutputStream(this.tmpFile));
                LOG.debug("Created encrypted atomic output stream for: {}", f.getName());
            } catch (IOException e) {
                throw new FileNotFoundException("Failed to create encrypted output stream: " + e.getMessage());
            }
        }
    }
    
    @Override
    public void close() throws IOException {
        boolean success = false;
        try {
            flush();
            success = true;
        } finally {
            try {
                out.close();
            } catch (IOException e) {
                if (success) {
                    throw e;
                }
            }
        }
        
        if (success) {
            // Move temp file to final location
            if (!tmpFile.renameTo(origFile)) {
                throw new IOException("Failed to move " + tmpFile + " to " + origFile);
            }
            LOG.debug("Successfully wrote atomic file: {}", origFile.getName());
        } else {
            // Clean up temp file on failure
            if (!tmpFile.delete()) {
                LOG.warn("Failed to delete temporary file: {}", tmpFile);
            }
        }
    }
    
    public void abort() {
        try {
            out.close();
        } catch (IOException e) {
            LOG.warn("Failed to close output stream during abort", e);
        }
        
        if (!tmpFile.delete()) {
            LOG.warn("Failed to delete temporary file during abort: {}", tmpFile);
        }
    }
}
