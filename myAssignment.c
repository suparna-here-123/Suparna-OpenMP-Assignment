#include <assert.h>
#include <stdio.h>
#include <omp.h>
#include <stdlib.h>
hi

#define NUM_PROCS 4
#define CACHE_SIZE 4
#define MEM_SIZE 16
#define MSG_BUFFER_SIZE 256
#define MAX_INSTR_NUM 32

typedef unsigned char byte;

typedef enum { MODIFIED, EXCLUSIVE, SHARED, INVALID } cacheLineState;

// In addition to cache line states, each directory entry also has a state
// EM ( exclusive or modified ) : memory block is in only one cache.
//                  When a memory block in a cache is written to, it would have
//                  resulted in cache hit and transition from EXCLUSIVE to MODIFIED.
//                  Main memory / directory has no way of knowing of this transition.
//                  Hence, this state only cares that the memory block is in a single
//                  cache, and not whether its in EXCLUSIVE or MODIFIED.
// S ( shared )     : memory block is in multiple caches
// U ( unowned )    : memory block is not in any cache
typedef enum { EM, S, U } directoryEntryState;

typedef enum { 
    READ_REQUEST,       // requesting node sends to home node on a read miss 
    WRITE_REQUEST,      // requesting node sends to home node on a write miss 
    REPLY_RD,           // home node replies with data to requestor for read request
    REPLY_WR,           // home node replies to requestor for write request
    REPLY_ID,           // home node replies with IDs of sharers to requestor
    INV,                // owner node asks sharers to invalidate
    UPGRADE,            // owner node asks home node to change state to EM
    WRITEBACK_INV,      // home node asks owner node to flush and change to INVALID
    WRITEBACK_INT,      // home node asks owner node to flush and change to SHARED
    FLUSH,              // owner flushes data to home + requestor
    FLUSH_INVACK,       // flush, piggybacking an InvAck message
    EVICT_SHARED,       // handle cache replacement of a shared cache line
    EVICT_MODIFIED      // handle cache replacement of a modified cache line
} transactionType;

// We will create our own address space which will be of size 1 byte
// LSB 4 bits indicate the location in memory
// MSB 4 bits indicate the processor it is present in.
// For example, 0x36 means the memory block at index 6 in the 3rd processor
typedef struct instruction {
    byte type;      // 'R' for read, 'W' for write
    byte address;
    byte value;     // used only for write operations
} instruction;

typedef struct cacheLine {
    byte address;           // this is the address in memory
    byte value;             // this is the value stored in cached memory
    cacheLineState state;   // state for you to do MESI protocol
} cacheLine;

typedef struct directoryEntry {
    byte bitVector;         // each bit indicates whether that processor has this
                            // memory block in its cache
    directoryEntryState state;
} directoryEntry;

// Note that each message will contain values only in the fields which are relevant 
// to the transactionType
typedef struct message {
    transactionType type;
    int sender;          // thread id that sent the message
    byte address;        // memory block address
    byte value;          // value in memory / cache
    byte bitVector;      // ids of sharer nodes
    int secondReceiver;  // used when you need to send a message to 2 nodes, where
                         // 1 node id is in the sender field
    directoryEntryState dirState;   // directory entry state of the memory block
} message;

typedef struct messageBuffer {
    message queue[ MSG_BUFFER_SIZE ];
    // a circular queue message buffer
    int head;
    int tail;
    int count;          // store total number of messages processed by the node
} messageBuffer;

typedef struct processorNode {
    cacheLine cache[ CACHE_SIZE ];
    byte memory[ MEM_SIZE ];
    directoryEntry directory[ MEM_SIZE ];
    instruction instructions[ MAX_INSTR_NUM ];
    int instructionCount;
} processorNode;

void initializeProcessor( int threadId, processorNode *node, char *dirName );
void sendMessage( int receiver, message msg );
void handleCacheReplacement( int sender, cacheLine oldCacheLine );
void printProcessorState( int processorId, processorNode node );

messageBuffer messageBuffers[ NUM_PROCS ];
// IMPLEMENT - DONE
// Create locks to ensure thread-safe access to each processor's message buffer.
omp_lock_t msgBufferLocks[ NUM_PROCS ];

int main( int argc, char * argv[] ) {
    if (argc < 2) {
        fprintf( stderr, "Usage: %s <test_directory>\n", argv[0] );
        return EXIT_FAILURE;
    }
    char *dirName = argv[1];
    
    // IMPLEMENT (DONE) - set number of threads to NUM_PROCS
    int numThreads = NUM_PROCS;

    for ( int i = 0; i < NUM_PROCS; i++ ) {
        messageBuffers[ i ].count = 0;
        messageBuffers[ i ].head = 0;
        messageBuffers[ i ].tail = 0;
        // IMPLEMENT (DONE) - initialize the locks in msgBufferLocks
        omp_init_lock(&msgBufferLocks[ i ]);
    }
    processorNode node;

    // IMPLEMENT (DONE??) - Create the omp parallel region with an appropriate data environment
    #pragma omp parallel num_threads(numThreads) shared(messageBuffers)
    {
        int threadId = omp_get_thread_num();
        initializeProcessor( threadId, &node, dirName );
        // IMPLEMENT (DONE)- wait for all processors to complete initialization before proceeding
        #pragma omp barrier

        message msg;
        message msgReply;
        instruction instr;
        int instructionIdx = -1;
        int printProcState = 1;         // control how many times to dump processor
        byte waitingForReply = 0;       // if a processor is waiting for a reply
                                        // then dont proceed to next instruction
                                        // as current instruction has not finished
        while ( 1 ) {
            // Process all messages in message queue first
            while ( 
                messageBuffers[ threadId ].count > 0 &&
                messageBuffers[ threadId ].head != messageBuffers[ threadId ].tail
            ) {
                int head = messageBuffers[ threadId ].head;
                msg = messageBuffers[ threadId ].queue[ head ];
                messageBuffers[ threadId ].head = ( head + 1 ) % MSG_BUFFER_SIZE;

                #ifdef DEBUG
                printf( "Processor %d msg from: %d, type: %d, address: 0x%02X\n",
                        threadId, msg.sender, msg.type, msg.address );
                #endif /* ifdef DEBUG */

                // IMPLEMENT (DONE?) extract procNodeAddr and memBlockAddr from message address
                byte procNodeAddr = (msg.address & 0xF0) >> 4;
                int homeNodeNum = (int) procNodeAddr;        // Home node of memory block
                byte memBlockAddr = msg.address & 0x0F;
                int memIndex = (int)memBlockAddr;
                byte cacheIndex = memBlockAddr % CACHE_SIZE;
                int cacheIndex = (int) cacheIndex;

                switch ( msg.type ) {
                    case READ_REQUEST:
                        // IMPLEMENT - DONE?
                        // This is in the home node
                        // If directory state is,
                        // Step 1 : Accessing directory state of block referred to in the message
                        directoryEntryState state = (node.directory[memIndex]).state;
                        switch (state)
                        {
                            // U: update directory and send value using REPLY_RD
                            case U :
                                message readReqMsg;
                                readReqMsg.type = REPLY_RD;
                                readReqMsg.sender = omp_get_thread_num();
                                readReqMsg.address = msg.address;
                                readReqMsg.value = node.memory[memIndex];
                                // Also sending sharers list coz it's needed to set appropriate cacheline status to EXCLUSIVE or SHARED in receiver
                                readReqMsg.bitVector = node.directory[memIndex].bitVector;
                                sendMessage(homeNodeNum, readReqMsg);

                                  // Updating directory
                                node.directory[memIndex].state = EM;
                                node.directory[memIndex].bitVector = num2Byte(msg.sender);
                                break;

                            // S: update directory and send value using REPLY_RD
                            case S :
                                message readReqMsg;
                                readReqMsg.type = REPLY_RD;
                                readReqMsg.sender = omp_get_thread_num();
                                readReqMsg.address = msg.address;
                                readReqMsg.value = node.memory[memBlockAddr];
                                // sharers list should not include new sharer no?
                                readReqMsg.bitVector = node.directory[memIndex].bitVector;
                                sendMessage(homeNodeNum, readReqMsg);

                                // Updating directory
                                (node.directory[memIndex]).bitVector = (node.directory[memIndex]).bitVector & procNodeAddr;
                                break;
                            
                            // EM: forward request to the current owner node for
                            // writeback intervention using WRITEBACK_INT
                            case EM :
                                message readReqMsg;
                                readReqMsg.type = WRITEBACK_INT;
                                readReqMsg.sender = omp_get_thread_num();
                                readReqMsg.address = msg.address;
                                sendMessage(homeNodeNum, readReqMsg);
                        }
                        
                        break;

                    case REPLY_RD:
                        // IMPLEMENT - DONE ?
                        // This is in the requesting node
                        // If some other memory block was in cacheline, you need to handle cache replacement
                        
                        // Step 1 : Check if cache is full and some line has to be evicted
                        if (node.cache[cacheIndex].address != 0xFF) { handleCacheReplacement(omp_get_thread_num(), node.cache[cacheIndex]); }
                        node.cache[cacheIndex].address = msg.address;
                        node.cache[cacheIndex].value = msg.value;
                        
                        // Read in the memory block sent in the message to cache
                        // Handle state of the memory block appropriately
                        // Checking if there are already some other processors sharing this value
                        if (msg.bitVector != 0x00) { node.cache[cacheIndex].state = SHARED; }
                        else { node.cache[cacheIndex].state = EXCLUSIVE; }
                                                
                        break;

                    case WRITEBACK_INT:
                        // IMPLEMENT - DONE?
                        // This is in old owner node which contains a cache line in MODIFIED state
                        // Flush this value to the home node and the requesting node using FLUSH
                        // Change cacheline state to SHARED
                        // If home node is the requesting node, avoid sending FLUSH twice

                        // Step 1 : Drafting message
                        message flushMsg;
                        flushMsg.type = FLUSH;
                        flushMsg.sender = omp_get_thread_num();
                        flushMsg.address = msg.address;
                        flushMsg.value = (node.cache)[cacheIndex].value;
                        // sharers are now this node and the requesting node
                        flushMsg.bitVector = num2Byte(omp_get_thread_num()) & num2Byte(msg.sender);

                        // Step 2 : Changing cacheline state to shared
                        (node.cache)[cacheIndex].state = SHARED;

                        // Step 3 : Flushing to home node
                        sendMessage(homeNodeNum, flushMsg);

                        // Step 4 : Sending the message to the sender
                        if (msg.sender != homeNodeNum) { sendMessage(msg.sender, flushMsg); }
                        break;
                    
                    // (????????????)
                    case FLUSH:
                        // IMPLEMENT
                        // If in home node
                        // update directory state and bitvector appropriately
                        // update memory value

                        // Step 1 : Checking if I'm the home node for the message received
                        if (omp_get_thread_num() == homeNodeNum)
                        {
                            // Updating dir status to SHARED REGARDLESS (??)
                            (node.directory)[memIndex].state = SHARED;
                            //(node.directory)[memIndex].bitVector
                            
                        }
                        // If in requesting node, load block into cache
                        // If some other memory block was in cacheline, you need to
                        // handle cache replacement
                        // IMPORTANT: there can be cases where home node is same as
                        // requesting node, which need to be handled appropriately
                        else{


                        }
                        
                        break;

                    case UPGRADE:
                        // IMPLEMENT - DONE(?)
                        // This is in the home node
                        // The requesting node had a write hit on a SHARED cacheline
                        // Send list of sharers to requesting node using REPLY_ID
                        // Update directory state to EM, and bit vector to only have
                        // the requesting node set
                        // IMPORTANT: Do not include the requesting node in the
                        // sharers list

                        // Step 1 : Sending message
                        message upgradeReplyMsg;
                        upgradeReplyMsg.type = REPLY_ID;
                        upgradeReplyMsg.sender = omp_get_thread_num();
                        upgradeReplyMsg.address = msg.address;
                        
                        
                        byte flusherByte = num2Byte(msg.sender);
                        upgradeReplyMsg.bitVector = (node.directory)[memIndex].bitVector & ~flusherByte;    // excludes the requesting node here
                        sendMessage(msg.sender, upgradeReplyMsg);

                        // Step 2 : updating directory
                        (node.directory)[memIndex].state = EM;
                        (node.directory)[memIndex].bitVector = flusherByte;

                        break;

                    case REPLY_ID:
                        // IMPLEMENT
                        // This is in the requesting ( new owner ) node
                        // The owner node recevied the sharers list from home node
                        // Invalidate all sharers' entries using INV

                        // Step 1 : Draft msg template
                        message invalidMsg;
                        invalidMsg.type = INV;
                        invalidMsg.sender = omp_get_thread_num();
                        invalidMsg.address = msg.address;
                        
                        // Step 2 : Getting processor IDs from sharers list and sending (byte)
                        sendToSharers(msg.bitVector, invalidMsg);
                                      
                        
                        // Handle cache replacement if needed, and load the block
                        // into the cacheline
                        // NOTE: Ideally, we should update the owner node cache line
                        // after we receive INV_ACK from every sharer, but for that
                        // we will have to keep track of all the INV_ACKs.
                        // Instead, we will assume that INV does not fail.

                        // Step 3 : Setting cache line state to MODIFIED
                        (node.cache)[cacheIndex].state = MODIFIED;
                        break;

                    case INV:
                        // IMPLEMENT
                        // This is in the sharer node
                        // Invalidate the cache entry for memory block
                        // If the cache no longer has the memory block ( replaced by
                        // a different block ), then do nothing

                        // Check to see if cacheline exists
                        if (node.cache[cacheIndex].address != 0xFF){
                            node.cache[cacheIndex].state = INV;
                        }
                        break;

                    case WRITE_REQUEST:
                        // IMPLEMENT
                        // This is in the home node
                        // Write miss occured in requesting node
                        // If the directory state is
                        directoryEntryState state = (node.directory)[memIndex].state;
                        switch (state){
                        // U:   no cache contains this memory block, and requesting
                        //      node directly becomes the owner node, use REPLY_WR
                        case U :
                            // Step 1 : Changing the directory state to EM
                            (node.directory)[memIndex].state = EM;

                            // Step 2 : Sending message
                            message writeReplyMsg;
                            writeReplyMsg.type = REPLY_WR;
                            writeReplyMsg.sender = omp_get_thread_num();
                            writeReplyMsg.address = msg.address;
                            writeReplyMsg.value = (node.memory)[memIndex];
                            sendMessage(msg.sender, writeReplyMsg);
                            
                        // S:   other caches contain this memory block in clean state
                        //      which have to be invalidated
                        //      send sharers list to new owner node using REPLY_ID
                        //      update directory
                        case S :
                            // Step 1 : Update directory entry state to EM
                            (node.directory)[memIndex].state = EM;

                            // Step 2 : Get list of sharers (excluding requesting block coz otherwise it'll also get set to invalid)
                            byte senderByte = num2Byte(msg.sender);
                            byte sendBitVector = (node.directory)[memIndex].bitVector & (~senderByte);

                            // Step 3 : Send message
                            message writeReplyMsg;
                            writeReplyMsg.type = REPLY_ID;
                            writeReplyMsg.sender = omp_get_thread_num();
                            writeReplyMsg.address = msg.address;
                            writeReplyMsg.value = (node.memory)[memIndex];
                            writeReplyMsg.bitVector = sendBitVector;
                            sendMessage(msg.sender, writeReplyMsg);                            

                            
                        // EM:  one other cache contains this memory block, which
                        //      can be in EXCLUSIVE or MODIFIED
                        //      send WRITEBACK_INV to the old owner, to flush value
                        //      into memory and invalidate cacheline
                        case EM :
                            // Step 1 : Drafting message
                            message writeReplyMsg;
                            writeReplyMsg.type = WRITEBACK_INV;
                            writeReplyMsg.sender = omp_get_thread_num();
                            writeReplyMsg.address = msg.address;
                            writeReplyMsg.secondReceiver = msg.sender;

                            // Step 2 : Finding who the old owner is
                            int oldOwner = (int)((node.directory)[memIndex].bitVector);

                            // Step 3 : Send message
                            sendMessage(oldOwner, writeReplyMsg);
                            
                        }
                        break;

                    case REPLY_WR:
                        // IMPLEMENT
                        // This is in the requesting ( new owner ) node
                        // Handle cache replacement if needed, and load the memory
                        // block into cache
                        
                        // IS THIS CORRECT?? - seeing if cacheline is occupied
                        if ((node.cache)[cacheIndex].address != 0xFF){
                            handleCacheReplacement(omp_get_thread_num(), node.cache[cacheIndex]);
                        }

                        cacheLine CL = node.cache[cacheIndex];
                        CL.address = msg.address;
                        CL.state = EXCLUSIVE;  
                        break;

                    case WRITEBACK_INV:
                        // IMPLEMENT
                        // This is in the old owner node
                        // Flush the currrent value to home node using FLUSH_INVACK
                        // Send an ack to the requesting node which will become the
                        // new owner node
                        // If home node is the new owner node, dont send twice
                        // Invalidate the cacheline

                        // Step 1 : Drafting message for flush
                        message flushMsg;
                        flushMsg.type = FLUSH_INVACK;
                        flushMsg.sender = omp_get_thread_num();
                        flushMsg.address = msg.address;
                        flushMsg.value = node.cache[cacheIndex].value;
                        
                        // Step 2 : Send same message to requesting node (if not same as home node)
                        sendMessage(homeNodeNum, flushMsg);
                        if (homeNodeNum != msg.sender) { sendMessage(msg.secondReceiver, flushMsg); }

                        // Step 3 : Invalidate cache line
                        node.cache[cacheIndex].state = INVALID;

                        break;

                    case FLUSH_INVACK:
                        // IMPLEMENT - HOW???
                        // If in home node, update directory and memory
                        // The bit vector should have only the new owner node set
                        // Flush the value from the old owner to memory
                        //
                        // If in requesting node, handle cache replacement if needed,
                        // and load block into cache
                        break;
                    
                    case EVICT_SHARED:
                        // IMPLEMENT
                        // If in home node,
                        // Requesting node evicted a cacheline which was in SHARED
                        // Remove the old node from bitvector,
                        // if no more sharers exist, change directory state to U
                        // if only one sharer exist, change directory state to EM
                        // Inform the remaining sharer ( which will become owner ) to
                        // change from SHARED to EXCLUSIVE using EVICT_SHARED
                        //

                        // Step 1 : Check if I'm the home node
                        if (msg.sender == omp_get_thread_num()){

                            // Step A : Remove old node from bitvector
                            byte senderByte = num2Byte(msg.sender);
                            node.directory[memIndex].bitVector = node.directory[memIndex].bitVector & (~senderByte);

                            // Step B : Check how many sharers and update directory state
                            if (!node.directory[memIndex].bitVector){ node.directory[memIndex].state = U; }
                            else { node.directory[memIndex].state = EM; }

                            // Step C : Inform remaining sharer to change state
                            int remOwner = (int)((node.directory[memIndex].bitVector));
                            message evictSharedMsg;
                            evictSharedMsg.type = EVICT_SHARED;
                            evictSharedMsg.sender = omp_get_thread_num();
                            evictSharedMsg.address = msg.address;
                            sendMessage(remOwner, evictSharedMsg);
                        }
                        // If in remaining sharer ( new owner ), update cacheline
                        // from SHARED to EXCLUSIVE
                        else{ (node.cache)[cacheIndex].state = EXCLUSIVE; }
                        break;

                    case EVICT_MODIFIED:
                        // IMPLEMENT
                        // This is in home node,
                        // Requesting node evicted a cacheline which was in MODIFIED
                        // Flush value to memory
                        // Remove the old node from bitvector HINT: since it was in
                        // modified state, not other node should have had that
                        // memory block in a valid state its cache
                        
                        // Step 1 : Flush value to memory
                        (node.memory)[memIndex] = msg.value;

                        // Step 2 : Are sharers with invalid state stored in bitvector???

                        break;
                }
            }
            
            // Check if we are waiting for a reply message
            // if yes, then we have to complete the previous instruction before
            // moving on to the next
            if ( waitingForReply > 0 ) {
                continue;
            }

            // Process an instruction
            if ( instructionIdx < node.instructionCount - 1 ) {
                instructionIdx++;
            } else {
                if ( printProcState > 0 ) {
                    printProcessorState( threadId, node );
                    printProcState--;
                }
                // even though there are no more instructions, this processor might
                // still need to react to new transaction messages
                //
                // once all the processors are done printing and appear to have no
                // more network transactions, please terminate the program by sending
                // a SIGINT ( CTRL+C )
                continue;
            }
            instr = node.instructions[ instructionIdx ];

            #ifdef DEBUG
            printf( "Processor %d: instr type=%c, address=0x%02X, value=%hhu\n",
                    threadId, instr.type, instr.address, instr.value );
            #endif

            // IMPLEMENT
            // Extract the home node's address and memory block index from
            // instruction address
            byte procNodeAddr = (instr.address & 0xF0) >> 4;
            int instrHomeNodeAddr = (int)procNodeAddr;
            byte memBlockAddr = instr.address & 0x0F;
            byte cacheIndex = memBlockAddr % CACHE_SIZE;
            int cacheIndex = (int) cacheIndex;

          if ( instr.type == 'R' ) {
                // IMPLEMENT
                // check if memory block is present in cache
                //
                // if cache hit and the cacheline state is not invalid, then use
                // that value. no transaction/transition takes place so no work.
                //
                // if cacheline is invalid, or memory block is not present, it is
                // treated as a read miss
                // send a READ_REQUEST to home node on a read miss

                // Step 1 : If cacheline exists and state not invalid
                if (node.cache[cacheIndex].address != 0xFF && node.cache[cacheIndex].state != INVALID)
                {
                // HOW TO "PROCESS" a read instruction??
                printf("Read %u from instruction\n", node.cache[cacheIndex].value);
                }
                // Read-miss, send message
                else{
                    message readMissMsg;
                    readMissMsg.type = READ_REQUEST;
                    readMissMsg.sender = omp_get_thread_num();
                    readMissMsg.address = instr.address;
                    sendMessage(instrHomeNodeAddr, readMissMsg);
                }
            } 
            // Write operation
            else {
                // IMPLEMENT - DONE?
                // check if memory block is present in cache
                // if cache hit and cacheline state is not invalid, then it is a
                // write hit
                if (node.cache[cacheIndex].address != 0xFF) 
                {
                    switch(node.cache[cacheIndex].state)
                    {
                        // if cache miss or cacheline state is invalid, then it is a write miss, send a WRITE_REQUEST to home node on a write miss
                        case INVALID :
                            message writeMissMsg;
                            writeMissMsg.type = WRITE_REQUEST;
                            writeMissMsg.sender = omp_get_thread_num();
                            writeMissMsg.address = instr.address;
                            sendMessage(instrHomeNodeAddr, writeMissMsg);
                            break;
                        
                        // if shared, other nodes contain this memory block, request home node to send list of sharers. 
                        // This node will have to send an UPGRADE to home node to promote directory from S to EM, 
                        // and also invalidate the entries in the sharers
                        case SHARED :
                            message writeMissMsg;
                            writeMissMsg.type = UPGRADE;
                            writeMissMsg.sender = omp_get_thread_num();
                            writeMissMsg.address = instr.address;
                            sendMessage(instrHomeNodeAddr, writeMissMsg);
                            break;
                        
                        // if modified or exclusive, update cache directly as only this node
                        // contains the memory block, no network transactions required
                        default :
                            node.cache[cacheIndex].value = instr.value;
                            break;

                    }
                }

                
                
            }

        }
    }
}

void sendMessage( int receiver, message msg ) {
    // IMPLEMENT - done?
    // Ensure thread safety while adding a message to the receiver's buffer
    // Manage buffer indices correctly to maintain a circular queue structure

    // Step 1 : Locking message buffer of receiver before adding
    omp_set_lock(&msgBufferLocks[receiver]);

    // Step2 : Accessing the message buffer of receiver and adding message at tail index
    messageBuffer rcvBuffer = messageBuffers[receiver];
    (rcvBuffer.queue)[rcvBuffer.tail] = msg;

    // Step 3 : Increasing tail value count, if exceeds MSG_BUFFER_SIZE, don't increment
    if (++rcvBuffer.tail > MSG_BUFFER_SIZE) { rcvBuffer.tail--; }

    // Step 3 : Increase count of messages processed in receiver
    rcvBuffer.count ++;    

    // Step 4 : Unlock
    omp_unset_lock(&msgBufferLocks[receiver]);
}

void sendToSharers(byte bitVector, message msg)
{
    int procNum = 7;
    if (bitVector == 0x00){
        sendMessage(0, msg);
        return;
    }else{
        while (bitVector != 0x00){
            if (bitVector & 1)
                sendMessage(procNum, msg);
            bitVector >>= 1;
            procNum --;
        }
    }
}

// Getting byte representation (one-hot encoding here) of a given processor number
byte num2Byte(int sender){
    char senderByte[8];
    for ( int i = 0; i < NUM_PROCS; i++ ){
        if (sender == i){ senderByte[i] = 1; }
        else { senderByte[i] = 0; }
    }
    return (byte)senderByte;
}


void handleCacheReplacement( int sender, cacheLine oldCacheLine ) {
    // IMPLEMENT - DONE
    // Notify the home node before a cacheline gets replaced
    // Extract the home node's address and memory block index from cacheline address

    // Step 1 : Use a bit mask to get first and last 4 bits
    byte procNodeAddr = (oldCacheLine.address & 0xF0) >> 4;
    int homeNodeNum = (int)procNodeAddr;
    byte memBlockAddr = oldCacheLine.address & 0x0F;
    
    switch ( oldCacheLine.state ) {
        case SHARED:
            // IMPLEMENT - DONE?
            // If cache line was shared, inform home node about the eviction

            // Step 1 : Create the message
            message msgToSend;
            msgToSend.type = EVICT_SHARED;
            msgToSend.sender = omp_get_thread_num();
            msgToSend.address = memBlockAddr;

            // Step 2 : Send the message
            sendMessage(homeNodeNum, msgToSend);
            break;

        case MODIFIED:
            // IMPLEMENT - DONE?
            // If cache line was modified, send updated value to home node 
            // so that memory can be updated before eviction
            
            // Step 1 : Create the message
            message msgToSend;
            msgToSend.type = EVICT_MODIFIED;
            msgToSend.sender = omp_get_thread_num();
            msgToSend.address = memBlockAddr;
            msgToSend.value = oldCacheLine.value;

            // Step 2 : Send the message
            sendMessage(homeNodeNum, msgToSend);
            break;
            
        default:
            // No action required for INVALID or EXCLUSIVE states
            break;
    }
}

void initializeProcessor( int threadId, processorNode *node, char *dirName ) {
    // IMPORTANT: DO NOT MODIFY
    for ( int i = 0; i < MEM_SIZE; i++ ) {
        node->memory[ i ] = 20 * threadId + i;  // some initial value to mem block
        node->directory[ i ].bitVector = 0;     // no cache has this block at start
        node->directory[ i ].state = U;         // this block is in Unowned state
    }

    for ( int i = 0; i < CACHE_SIZE; i++ ) {
        node->cache[ i ].address = 0xFF;        // this address is invalid as we can
                                                // have a maximum of 8 nodes in the 
                                                // current implementation
        node->cache[ i ].value = 0;
        node->cache[ i ].state = INVALID;       // all cache lines are invalid
    }

    // read and parse instructions from core_<threadId>.txt
    char filename[ 128 ];
    snprintf(filename, sizeof(filename), "tests/%s/core_%d.txt", dirName, threadId);
    FILE *file = fopen( filename, "r" );
    if ( !file ) {
        fprintf( stderr, "Error: count not open file %s\n", filename );
        exit( EXIT_FAILURE );
    }

    char line[ 20 ];
    node->instructionCount = 0;
    while ( fgets( line, sizeof( line ), file ) &&
            node->instructionCount < MAX_INSTR_NUM ) {
        if ( line[ 0 ] == 'R' && line[ 1 ] == 'D' ) {
            sscanf( line, "RD %hhx",
                    &node->instructions[ node->instructionCount ].address );
            node->instructions[ node->instructionCount ].type = 'R';
            node->instructions[ node->instructionCount ].value = 0;
        } else if ( line[ 0 ] == 'W' && line[ 1 ] == 'R' ) {
            sscanf( line, "WR %hhx %hhu",
                    &node->instructions[ node->instructionCount ].address,
                    &node->instructions[ node->instructionCount ].value );
            node->instructions[ node->instructionCount ].type = 'W';
        }
        node->instructionCount++;
    }

    fclose( file );
    #ifdef DEBUG
    printf( "Processor %d initialized\n", threadId );
    #endif /* ifdef DEBUG */
}

void printProcessorState(int processorId, processorNode node) {
    // IMPORTANT: DO NOT MODIFY
    static const char *cacheStateStr[] = { "MODIFIED", "EXCLUSIVE", "SHARED",
                                           "INVALID" };
    static const char *dirStateStr[] = { "EM", "S", "U" };

    #pragma omp critical
    {
    printf("=======================================\n");
    printf(" Processor Node: %d\n", processorId);
    printf("=======================================\n\n");

    // Print memory state
    printf("----------- Memory State -----------\n");
    printf("| Index |  Value  |\n");
    printf("|-----------------|\n");
    for (int i = 0; i < MEM_SIZE; i++) {
        printf("|  %3d  |  %5d  |\n", i, node.memory[i]);
    }
    printf("------------------------------------\n\n");

    // Print directory state
    printf("---------- Directory State ---------\n");
    printf("| Index | State | BitVector      |\n");
    printf("|--------------------------------|\n");
    for (int i = 0; i < MEM_SIZE; i++) {
        printf("|  %3d  |   %2s  |   0x%08B   |\n", 
               i, dirStateStr[node.directory[i].state], node.directory[i].bitVector);
    }
    printf("------------------------------------\n\n");

    // Print cache state
    printf("------------ Cache State ----------------\n");
    printf("| Index | Address | Value |  State      |\n");
    printf("|---------------------------------------|\n");
    for (int i = 0; i < CACHE_SIZE; i++) {
        printf("|  %3d  |  0x%02X  |  %3d  |  %8s \t|\n", 
               i, node.cache[i].address, node.cache[i].value,
               cacheStateStr[node.cache[i].state]);
    }
    printf("----------------------------------------\n\n");
    }
}
