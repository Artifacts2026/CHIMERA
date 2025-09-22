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

#ifndef ZOOKEEPER_TDX_CRYPTO_JNI_H
#define ZOOKEEPER_TDX_CRYPTO_JNI_H

#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Class:     org_apache_zookeeper_server_crypto_TdxCryptoUtil
 * Method:    initLogEncryption
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL Java_org_apache_zookeeper_server_crypto_TdxCryptoUtil_initLogEncryption
  (JNIEnv *, jclass);

/*
 * Class:     org_apache_zookeeper_server_crypto_TdxCryptoUtil
 * Method:    encryptDataNative
 * Signature: ([B)[B
 */
JNIEXPORT jbyteArray JNICALL Java_org_apache_zookeeper_server_crypto_TdxCryptoUtil_encryptDataNative
  (JNIEnv *, jclass, jbyteArray);

/*
 * Class:     org_apache_zookeeper_server_crypto_TdxCryptoUtil
 * Method:    decryptDataNative
 * Signature: ([B)[B
 */
JNIEXPORT jbyteArray JNICALL Java_org_apache_zookeeper_server_crypto_TdxCryptoUtil_decryptDataNative
  (JNIEnv *, jclass, jbyteArray);

/*
 * Class:     org_apache_zookeeper_server_crypto_TdxCryptoUtil
 * Method:    isDataEncrypted
 * Signature: ([B)Z
 */
JNIEXPORT jboolean JNICALL Java_org_apache_zookeeper_server_crypto_TdxCryptoUtil_isDataEncrypted
  (JNIEnv *, jclass, jbyteArray);

/*
 * Class:     org_apache_zookeeper_server_crypto_TdxCryptoUtil
 * Method:    getTdxReport
 * Signature: ()[B
 */
JNIEXPORT jbyteArray JNICALL Java_org_apache_zookeeper_server_crypto_TdxCryptoUtil_getTdxReport
  (JNIEnv *, jclass);

#ifdef __cplusplus
}
#endif

#endif // ZOOKEEPER_TDX_CRYPTO_JNI_H
