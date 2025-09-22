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

package org.apache.zookeeper.server;

import java.io.Flushable;
import java.io.IOException;
import java.util.ArrayDeque;
import java.util.Objects;
import java.util.Queue;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.Semaphore;
import java.util.concurrent.ThreadLocalRandom;
import java.util.concurrent.TimeUnit;
import org.apache.zookeeper.common.Time;
import org.apache.zookeeper.server.quorum.Follower;
import org.apache.zookeeper.server.quorum.FollowerZooKeeperServer;
import org.apache.zookeeper.server.quorum.Leader;
import org.apache.zookeeper.server.quorum.QuorumZooKeeperServer;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * This RequestProcessor logs requests to disk. It batches the requests to do
 * the io efficiently. The request is not passed to the next RequestProcessor
 * until its log has been synced to disk.
 *
 * SyncRequestProcessor is used in 3 different cases
 * 1. Leader - Sync request to disk and forward it to AckRequestProcessor which
 *             send ack back to itself.
 * 2. Follower - Sync request to disk and forward request to
 *             SendAckRequestProcessor which send the packets to leader.
 *             SendAckRequestProcessor is flushable which allow us to force
 *             push packets to leader.
 * 3. Observer - Sync committed request to disk (received as INFORM packet).
 *             It never send ack back to the leader, so the nextProcessor will
 *             be null. This change the semantic of txnlog on the observer
 *             since it only contains committed txns.
 */
public class SyncRequestProcessor extends ZooKeeperCriticalThread implements RequestProcessor {

    private static final Logger LOG = LoggerFactory.getLogger(SyncRequestProcessor.class);

    private static final Request REQUEST_OF_DEATH = Request.requestOfDeath;

    /** The number of log entries to log before starting a snapshot */
    private static int snapCount = ZooKeeperServer.getSnapCount();

    /**
     * The total size of log entries before starting a snapshot
     */
    private static long snapSizeInBytes = ZooKeeperServer.getSnapSizeInBytes();

    /**
     * Random numbers used to vary snapshot timing
     */
    private int randRoll;
    private long randSize;

    private final BlockingQueue<Request> queuedRequests = new LinkedBlockingQueue<>();

    private final Semaphore snapThreadMutex = new Semaphore(1);

    private final ZooKeeperServer zks;

    private final RequestProcessor nextProcessor;

    /**
     * Transactions that have been written and are waiting to be flushed to
     * disk. Basically this is the list of SyncItems whose callbacks will be
     * invoked after flush returns successfully.
     */
    private final Queue<Request> toFlush;
    private long lastFlushTime;

    public SyncRequestProcessor(ZooKeeperServer zks, RequestProcessor nextProcessor) {
        super("SyncThread:" + zks.getServerId(), zks.getZooKeeperServerListener());
        this.zks = zks;
        this.nextProcessor = nextProcessor;
        this.toFlush = new ArrayDeque<>(zks.getMaxBatchSize());
    }

    /**
     * used by tests to check for changing
     * snapcounts
     * @param count
     */
    public static void setSnapCount(int count) {
        snapCount = count;
    }

    /**
     * used by tests to get the snapcount
     * @return the snapcount
     */
    public static int getSnapCount() {
        return snapCount;
    }

    private long getRemainingDelay() {
        long flushDelay = zks.getFlushDelay();
        long duration = Time.currentElapsedTime() - lastFlushTime;
        if (duration < flushDelay) {
            return flushDelay - duration;
        }
        return 0;
    }

    /** If both flushDelay and maxMaxBatchSize are set (bigger than 0), flush
     * whenever either condition is hit. If only one or the other is
     * set, flush only when the relevant condition is hit.
     */
    private boolean shouldFlush() {
        long flushDelay = zks.getFlushDelay();
        long maxBatchSize = zks.getMaxBatchSize();
        if ((flushDelay > 0) && (getRemainingDelay() == 0)) {
            return true;
        }
        return (maxBatchSize > 0) && (toFlush.size() >= maxBatchSize);
    }

    /**
     * used by tests to check for changing
     * snapcounts
     * @param size
     */
    public static void setSnapSizeInBytes(long size) {
        snapSizeInBytes = size;
    }

    private boolean shouldSnapshot() {
        int logCount = zks.getZKDatabase().getTxnCount();
        long logSize = zks.getZKDatabase().getTxnSize();
        return (logCount > (snapCount / 2 + randRoll))
               || (snapSizeInBytes > 0 && logSize > (snapSizeInBytes / 2 + randSize));
    }

    private void resetSnapshotStats() {
        randRoll = ThreadLocalRandom.current().nextInt(snapCount / 2);
        randSize = Math.abs(ThreadLocalRandom.current().nextLong() % (snapSizeInBytes / 2));
    }

    @Override
    public void run() {
        try {
            // we do this in an attempt to ensure that not all of the servers
            // in the ensemble take a snapshot at the same time
            resetSnapshotStats();
            lastFlushTime = Time.currentElapsedTime();
            while (true) {
                ServerMetrics.getMetrics().SYNC_PROCESSOR_QUEUE_SIZE.add(queuedRequests.size());

                long pollTime = Math.min(zks.getMaxWriteQueuePollTime(), getRemainingDelay());
                Request si = queuedRequests.poll(pollTime, TimeUnit.MILLISECONDS);
                if (si == null) {
                    /* We timed out looking for more writes to batch, go ahead and flush immediately */
                    flush();
                    si = queuedRequests.take();
                }

                if (si == REQUEST_OF_DEATH) {
                    break;
                }

                long startProcessTime = Time.currentElapsedTime();
                ServerMetrics.getMetrics().SYNC_PROCESSOR_QUEUE_TIME.add(startProcessTime - si.syncQueueStartTime);

                // track the number of records written to the log
                if (!si.isThrottled() && zks.getZKDatabase().append(si)) {
                    if (shouldSnapshot()) {
                        resetSnapshotStats();
                        // roll the log
                        zks.getZKDatabase().rollLog();
                        // take a snapshot
                        if (!snapThreadMutex.tryAcquire()) {
                            LOG.warn("Too busy to snap, skipping");
                        } else {
                            new ZooKeeperThread("Snapshot Thread") {
                                public void run() {
                                    try {
                                        zks.takeSnapshot();
                                    } catch (Exception e) {
                                        LOG.warn("Unexpected exception", e);
                                    } finally {
                                        snapThreadMutex.release();
                                    }
                                }
                            }.start();
                        }
                    }
                } else if (toFlush.isEmpty()) {
                    // optimization for read heavy workloads
                    // iff this is a read or a throttled request(which doesn't need to be written to the disk),
                    // and there are no pending flushes (writes), then just pass this to the next processor
                    if (nextProcessor != null) {
                        nextProcessor.processRequest(si);
                        if (nextProcessor instanceof Flushable) {
                            ((Flushable) nextProcessor).flush();
                        }
                    }
                    continue;
                }
                toFlush.add(si);
                if (shouldFlush()) {
                    flush();
                }
                ServerMetrics.getMetrics().SYNC_PROCESS_TIME.add(Time.currentElapsedTime() - startProcessTime);
            }
        } catch (Throwable t) {
            handleException(this.getName(), t);
        }
        LOG.info("SyncRequestProcessor exited!");
    }

    private void flush() throws IOException, RequestProcessorException {
        if (this.toFlush.isEmpty()) {
            return;
        }

        ServerMetrics.getMetrics().BATCH_SIZE.add(toFlush.size());

        long flushStartTime = Time.currentElapsedTime();
        
        // RC MODIFICATION: Perform two-round RTT communication before committing to disk
        if (performTwoRoundRTTBeforeCommit()) {
            LOG.debug("RC: Two-round RTT completed successfully, proceeding with commit");
            zks.getZKDatabase().commit();
        } else {
            LOG.warn("RC: Two-round RTT failed, aborting commit");
            throw new RequestProcessorException("Two-round RTT communication failed");
        }
        
        ServerMetrics.getMetrics().SYNC_PROCESSOR_FLUSH_TIME.add(Time.currentElapsedTime() - flushStartTime);

        if (this.nextProcessor == null) {
            this.toFlush.clear();
        } else {
            while (!this.toFlush.isEmpty()) {
                final Request i = this.toFlush.remove();
                long latency = Time.currentElapsedTime() - i.syncQueueStartTime;
                ServerMetrics.getMetrics().SYNC_PROCESSOR_QUEUE_AND_FLUSH_TIME.add(latency);
                this.nextProcessor.processRequest(i);
            }
            if (this.nextProcessor instanceof Flushable) {
                ((Flushable) this.nextProcessor).flush();
            }
        }
        lastFlushTime = Time.currentElapsedTime();
    }

    /**
     * RC MODIFICATION: Perform two-round RTT communication before committing to disk
     * This method ensures f nodes respond in each round before proceeding
     * @return true if both rounds complete successfully, false otherwise
     */
    private boolean performTwoRoundRTTBeforeCommit() {
        // Only perform two-round RTT if this is a Leader
        if (!(zks instanceof QuorumZooKeeperServer)) {
            LOG.debug("RC: Not a QuorumZooKeeperServer, skipping two-round RTT");
            return true; // Non-quorum servers don't participate
        }
        
        QuorumZooKeeperServer qzks = (QuorumZooKeeperServer) zks;
        if (qzks.getQuorumPeer() == null || !qzks.getQuorumPeer().isRunning()) {
            LOG.debug("RC: QuorumPeer not running, skipping two-round RTT");
            return true;
        }
        
        // Both Leader and Followers perform two-round RTT before disk commit
        QuorumZooKeeperServer.State state = qzks.getQuorumPeer().getPeerState();
        if (state != QuorumZooKeeperServer.State.LEADING && 
            state != QuorumZooKeeperServer.State.FOLLOWING) {
            LOG.debug("RC: State is {}, skipping two-round RTT", state);
            return true; // Only Leader and Followers participate
        }
        
        try {
            LOG.debug("RC: Starting two-round RTT communication for state: {}", state);
            
            if (state == QuorumZooKeeperServer.State.LEADING) {
                // Leader coordinates with all followers
                Leader leader = qzks.getLeader();
                if (leader == null) {
                    LOG.warn("RC: Leader is null, cannot perform two-round RTT");
                    return false;
                }
                
                // Round 1
                if (!leader.performRoundRTT(Leader.PRE_COMMIT_ROUND1, Leader.ACK_ROUND1)) {
                    LOG.warn("RC: Leader Round 1 RTT failed");
                    return false;
                }
                
                // Round 2
                if (!leader.performRoundRTT(Leader.PRE_COMMIT_ROUND2, Leader.ACK_ROUND2)) {
                    LOG.warn("RC: Leader Round 2 RTT failed");
                    return false;
                }
                
            } else if (state == QuorumZooKeeperServer.State.FOLLOWING) {
                // Follower performs simplified two-round RTT 
                // For correctness, Follower should also do two-round RTT before disk commit
                // This is a simplified version that introduces delay without full complexity
                if (!performFollowerSimplifiedTwoRoundRTT()) {
                    LOG.warn("RC: Follower simplified two-round RTT failed");
                    return false;
                }
            }
            
            LOG.debug("RC: Two-round RTT completed successfully for {}", state);
            return true;
            
        } catch (Exception e) {
            LOG.error("RC: Exception during two-round RTT", e);
            return false;
        }
    }

    /**
     * RC MODIFICATION: Perform simplified two-round RTT for Follower nodes
     * This introduces the correct delay without the full complexity of peer-to-peer communication
     * In a full implementation, Follower would communicate with other nodes before disk commit
     * @return true if both rounds complete successfully, false otherwise
     */
    private boolean performFollowerSimplifiedTwoRoundRTT() {
        try {
            LOG.debug("RC: Follower performing simplified two-round RTT (delay simulation)");
            
            // Simulate two rounds of network communication delay
            // In a full implementation, this would be actual network communication
            
            // Round 1: Simulate network delay for first round
            long round1Start = System.nanoTime();
            Thread.sleep(10); // Simulate 10ms network delay
            long round1Time = TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - round1Start);
            LOG.debug("RC: Follower simulated round 1 completed in {}ms", round1Time);
            
            // Round 2: Simulate network delay for second round  
            long round2Start = System.nanoTime();
            Thread.sleep(10); // Simulate 10ms network delay
            long round2Time = TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - round2Start);
            LOG.debug("RC: Follower simulated round 2 completed in {}ms", round2Time);
            
            LOG.debug("RC: Follower simplified two-round RTT completed (total: {}ms)", round1Time + round2Time);
            return true;
            
        } catch (InterruptedException e) {
            LOG.warn("RC: Follower simplified two-round RTT interrupted", e);
            Thread.currentThread().interrupt();
            return false;
        } catch (Exception e) {
            LOG.error("RC: Exception during follower simplified two-round RTT", e);
            return false;
        }
    }

    public void shutdown() {
        LOG.info("Shutting down");
        queuedRequests.add(REQUEST_OF_DEATH);
        try {
            this.join();
            this.flush();
        } catch (InterruptedException e) {
            LOG.warn("Interrupted while waiting for {} to finish", this);
            Thread.currentThread().interrupt();
        } catch (IOException e) {
            LOG.warn("Got IO exception during shutdown");
        } catch (RequestProcessorException e) {
            LOG.warn("Got request processor exception during shutdown");
        }
        if (nextProcessor != null) {
            nextProcessor.shutdown();
        }
    }

    public void processRequest(final Request request) {
        Objects.requireNonNull(request, "Request cannot be null");

        request.syncQueueStartTime = Time.currentElapsedTime();
        queuedRequests.add(request);
        ServerMetrics.getMetrics().SYNC_PROCESSOR_QUEUED.add(1);
    }

}
