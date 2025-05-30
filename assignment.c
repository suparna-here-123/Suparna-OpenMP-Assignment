#include <assert.h>
#include <stdio.h>
#include <omp.h>
#include <stdlib.h>

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
    int secondReceiver;  // used when you need to send a message to 2 nodes, where 1 node id is in the sender field
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
byte num2Byte(int sender);
void sendToSharers(byte bitVector, message msg);
void printMessage(message msg, int sentTo);


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
    
    // IMPLEMENT (DONE??) - Create the omp parallel region with an appropriate data environment
    #pragma omp parallel num_threads(numThreads) shared(messageBuffers)
    {
        processorNode node;
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
            while ( messageBuffers[ threadId ].count > 0 && messageBuffers[ threadId ].head != messageBuffers[ threadId ].tail) 
            {
                int head = messageBuffers[ threadId ].head;
                msg = messageBuffers[ threadId ].queue[ head ];
                messageBuffers[ threadId ].head = ( head + 1 ) % MSG_BUFFER_SIZE;

                #ifdef DEBUG
                printf( "Processor %d got msg from: %d, type: %d, address: 0x%02X\n",
                        threadId, msg.sender, msg.type, msg.address );
                #endif /* ifdef DEBUG */

                // IMPLEMENT (DONE?) extract procNodeAddr and memBlockAddr from message address
                byte procNodeAddr = (msg.address & 0xF0) >> 4;
                byte memBlockAddr = msg.address & 0x0F;
                byte cacheIndex = memBlockAddr % CACHE_SIZE;

                switch ( msg.type ) {
                    case READ_REQUEST:
                        // IMPLEMENT - DONE?
                        // This is in the home node
                        // If directory state is,
                        // Step 1 : Accessing directory state of block referred to in the message
                        directoryEntryState memState = (node.directory[memBlockAddr]).state;
                        switch (memState)
                        {
                            // U: update directory and send value using REPLY_RD
                            case U :
                                // Updating state in directory to EM
                                node.directory[memBlockAddr].state = EM;
                                node.directory[memBlockAddr].bitVector = (1 << msg.sender);
                                
                                message uReadReqMsg;
                                uReadReqMsg.sender = threadId;
                                uReadReqMsg.address = msg.address;  // address that was requested only
                                uReadReqMsg.type = REPLY_RD;
                                uReadReqMsg.value = node.memory[memBlockAddr];
                                uReadReqMsg.bitVector = (byte) 0;           // no sharers
                                sendMessage(msg.sender, uReadReqMsg);
                                //printMessage(uReadReqMsg, threadId);
                                break;

                            // S: update directory and send value using REPLY_RD
                            case S :
                                message sReadReqMsg;
                                sReadReqMsg.sender = threadId;
                                sReadReqMsg.address = msg.address;
                                sReadReqMsg.type = REPLY_RD;
                                sReadReqMsg.value = node.memory[memBlockAddr];
                                // Sending sharers list coz it's needed to set appropriate cacheline status to EXCLUSIVE or SHARED in receiver
                                sReadReqMsg.bitVector = node.directory[memBlockAddr].bitVector;
                                sendMessage(msg.sender, sReadReqMsg);
                                
                                // Add sharer to bitVector
                                (node.directory[memBlockAddr]).bitVector |= (1 << msg.sender);
                                //printMessage(sReadReqMsg, threadId);

                                break;
                            
                            // EM: forward request to the current owner node for
                            // writeback intervention using WRITEBACK_INT
                            case EM :
                                message emReadReqMsg;
                                // FORWARDING - so sender is the requesting node not home node (could be if that is also the req node)
                                emReadReqMsg.sender = msg.sender;
                                emReadReqMsg.address = msg.address;
                                emReadReqMsg.type = WRITEBACK_INT;
                                byte owner = node.directory[memBlockAddr].bitVector;
                                sendMessage(owner, emReadReqMsg);
                                //printMessage(emReadReqMsg, threadId);
                                break;
                        }
                        
                        break;

                    case REPLY_RD:
                        //printMessage(msg, threadId);
                        // IMPLEMENT - DONE ?
                        // This is in the requesting node
                        // If some other memory block was in cacheline, you need to handle cache replacement
                        
                        // Step 1 : Check if cache is full and some line has to be evicted
                        if (node.cache[cacheIndex].address != 0xFF) { handleCacheReplacement(threadId, node.cache[cacheIndex]); }
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
                        message wbIntflushMsg;
                        wbIntflushMsg.type = FLUSH;
                        wbIntflushMsg.sender = threadId;
                        wbIntflushMsg.address = msg.address;
                        wbIntflushMsg.value = (node.cache)[cacheIndex].value;
                        wbIntflushMsg.secondReceiver = msg.sender;

                        // Step 2 : Changing cacheline state to shared
                        (node.cache)[cacheIndex].state = SHARED;

                        // Step 3 : Flushing to home node
                        sendMessage(procNodeAddr, wbIntflushMsg);

                        // Step 4 : Sending the message to the sender
                        if (msg.sender != procNodeAddr) { sendMessage(msg.sender, wbIntflushMsg); }
                        break;
                    
                    // (????????????)
                    case FLUSH:
                        // IMPLEMENT
                        // If in home node
                        // update directory state and bitvector appropriately
                        // update memory value

                        // Step 1 : Checking if I'm the home node for the message received
                        if (threadId == procNodeAddr)
                        {
                            node.memory[memBlockAddr] = msg.value;
                            node.directory[memBlockAddr].bitVector |= 1 << msg.sender;
                            node.directory[memBlockAddr].bitVector |= 1 << msg.secondReceiver;
                            (node.directory)[memBlockAddr].state = S;

                            if (threadId == msg.secondReceiver)
                            {
                                handleCacheReplacement(threadId, node.cache[cacheIndex]);
                                node.cache[cacheIndex].address = msg.address;
                                node.cache[cacheIndex].value = msg.value;
                                node.cache[cacheIndex].state = SHARED;
                            }
                            
                        }
                        // If in requesting node, load block into cache
                        // If some other memory block was in cacheline, you need to
                        // handle cache replacement
                        // IMPORTANT: there can be cases where home node is same as
                        // requesting node, which need to be handled appropriately
                        else{
                            if (node.cache[cacheIndex].address != 0xFF) { handleCacheReplacement(threadId, node.cache[cacheIndex]); }
                            node.cache[cacheIndex].address = msg.address;
                            node.cache[cacheIndex].value = msg.value;
                            node.cache[cacheIndex].state = SHARED;


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
                        // message upgradeReplyMsg;
                        // upgradeReplyMsg.type = REPLY_ID;
                        // upgradeReplyMsg.sender = threadId;
                        // upgradeReplyMsg.address = msg.address;
                        
                        byte flusherByte = (1 << msg.sender);
                        // upgradeReplyMsg.bitVector = (node.directory)[memBlockAddr].bitVector & ~flusherByte;    // excludes the requesting node here
                        // sendMessage(msg.sender, upgradeReplyMsg);

                        // // Step 2 : updating directory to have only the requesting node as sole owner (invalid owners)
                        // (node.directory)[memBlockAddr].state = EM;
                        // (node.directory)[memBlockAddr].bitVector = flusherByte;

                        // case UPGRADE (in home node)
                        message upgradeReplyMsg;
                        upgradeReplyMsg.type = REPLY_ID;
                        upgradeReplyMsg.sender = threadId;
                        upgradeReplyMsg.address = msg.address;
                        upgradeReplyMsg.bitVector = (node.directory)[memBlockAddr].bitVector & ~flusherByte;
                        upgradeReplyMsg.value = node.memory[memBlockAddr]; // <--- ADD THIS!
                        sendMessage(msg.sender, upgradeReplyMsg);

                        break;

                    case REPLY_ID:
                        // IMPLEMENT
                        // This is in the requesting ( new owner ) node
                        // The owner node recevied the sharers list from home node
                        // Invalidate all sharers' entries using INV

                        // Step 1 : Draft msg template
                        message invalidMsg;
                        invalidMsg.type = INV;
                        invalidMsg.sender = threadId;
                        invalidMsg.address = msg.address;
                        
                        // Step 2 : Getting processor IDs from sharers list and sending (byte)
                        sendToSharers(msg.bitVector, invalidMsg);
                        // Handle cache replacement if needed, and load the block into the cacheline
                        // Checking if the cacheline is occupied by some other address (?)
                        if (node.cache[cacheIndex].address != msg.address) {
                            handleCacheReplacement(threadId, node.cache[cacheIndex]);
                        }
                        // Step 3 : Setting cache line state to MODIFIED
                        node.cache[cacheIndex].address = msg.address;
                        node.cache[cacheIndex].value = msg.value;
                        (node.cache)[cacheIndex].state = MODIFIED;
                        // NOTE: Ideally, we should update the owner node cache line
                        // after we receive INV_ACK from every sharer, but for that
                        // we will have to keep track of all the INV_ACKs.
                        // Instead, we will assume that INV does not fail.
                        break;

                    case INV:
                        // IMPLEMENT
                        // This is in the sharer node
                        // Invalidate the cache entry for memory block
                        // If the cache no longer has the memory block ( replaced by
                        // a different block ), then do nothing

                        // Check to see if cacheline exists
                        if (node.cache[cacheIndex].address == msg.address){
                            node.cache[cacheIndex].state = INV;
                        }
                        break;

                    case WRITE_REQUEST:
                        // IMPLEMENT
                        // This is in the home node
                        // Write miss occured in requesting node
                        // If the directory state is
                        directoryEntryState wrMemState = (node.directory)[memBlockAddr].state;
                        switch (wrMemState){
                        // U:   no cache contains this memory block, and requesting
                        //      node directly becomes the owner node, use REPLY_WR
                        case U :
                            // Step 1 : Changing the directory state to EM
                            (node.directory)[memBlockAddr].state = EM;

                            // Step 2 : Sending message
                            message uWriteReplyMsg;
                            uWriteReplyMsg.sender = threadId;
                            uWriteReplyMsg.address = msg.address;
                            uWriteReplyMsg.type = REPLY_WR;
                            uWriteReplyMsg.value = (node.memory)[memBlockAddr];
                            uWriteReplyMsg.bitVector = (byte) 0;
                            sendMessage(msg.sender, uWriteReplyMsg);
                            break;
                            
                        // S:   other caches contain this memory block in clean state
                        //      which have to be invalidated
                        //      send sharers list to new owner node using REPLY_ID
                        //      update directory
                        case S :
                            // Step 1 : Update directory entry state to EM
                            (node.directory)[memBlockAddr].state = EM;

                            // Step 2 : Get list of sharers (excluding requesting block coz otherwise it'll also get set to invalid)
                            byte senderByte = 1 << msg.sender;
                            byte sendBitVector = (node.directory)[memBlockAddr].bitVector & (~senderByte);

                            // Step 3 : Send message
                            message sWriteReplyMsg;
                            sWriteReplyMsg.sender = threadId;
                            sWriteReplyMsg.address = msg.address;
                            sWriteReplyMsg.type = REPLY_ID;
                            sWriteReplyMsg.value = (node.memory)[memBlockAddr];
                            sWriteReplyMsg.bitVector = sendBitVector;
                            sendMessage(msg.sender, sWriteReplyMsg);
                            break;                        

                            
                        // EM:  one other cache contains this memory block, which
                        //      can be in EXCLUSIVE or MODIFIED
                        //      send WRITEBACK_INV to the old owner, to flush value
                        //      into memory and invalidate cacheline
                        case EM :
                            //printf("WRITE REQUEST TO memblock %d in %d\n", msg.address, omp_get_thread_num());
                            // Step 1 : Drafting message
                            message emWriteReplyMsg;
                            // FORWARDING REQUEST
                            emWriteReplyMsg.sender = msg.sender;
                            emWriteReplyMsg.address = msg.address;
                            emWriteReplyMsg.type = WRITEBACK_INV;

                            // Step 2 : Finding who the old owner is
                            byte oldOwner = node.directory[memBlockAddr].bitVector;

                            // Step 3 : Send message
                            sendMessage(oldOwner, emWriteReplyMsg);
                            break;
                            
                        }
                        break;

                    case REPLY_WR:
                        // IMPLEMENT
                        // This is in the requesting ( new owner ) node
                        // Handle cache replacement if needed, and load the memory
                        // block into cache
                        
                        // IS THIS CORRECT?? - seeing if cacheline is occupied
                        if ((node.cache)[cacheIndex].address != 0xFF){
                            handleCacheReplacement(threadId, node.cache[cacheIndex]);
                        }
                        node.cache[cacheIndex].address = msg.address;
                        node.cache[cacheIndex].value = msg.value;
                        if (msg.bitVector == 0x00) { node.cache[cacheIndex].state = EXCLUSIVE; }
                        else { node.cache[cacheIndex].state = SHARED; }
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
                        message wbInvflushMsg;
                        wbInvflushMsg.type = FLUSH_INVACK;
                        wbInvflushMsg.sender = threadId;
                        wbInvflushMsg.address = msg.address;
                        wbInvflushMsg.value = node.cache[cacheIndex].value;
                        wbInvflushMsg.secondReceiver = msg.sender;
                        
                        // Step 2 : Send same message to requesting node (if not same as home node)
                        sendMessage(procNodeAddr, wbInvflushMsg);
                        if (procNodeAddr != msg.sender) { sendMessage(msg.sender, wbInvflushMsg); }

                        // Step 3 : Invalidate cache line
                        node.cache[cacheIndex].state = INVALID;

                        break;

                    case FLUSH_INVACK:
                        // IMPLEMENT - HOW???
                        // If in home node, update directory and memory
                        // The bit vector should have only the new owner node set
                        // Flush the value from the old owner to memory
                        if (procNodeAddr == threadId)
                        {
                            node.directory[memBlockAddr].bitVector = 1 << (msg.secondReceiver);
                            node.directory[memBlockAddr].state = EM;
                            node.memory[memBlockAddr] = msg.value;

                            // Checking if this flushed cacheline exists in my cache
                            if (node.cache[cacheIndex].address == msg.address){
                                node.cache[cacheIndex].value = msg.value;
                            }
                        }
                        // If in requesting node, handle cache replacement if needed,
                        // and load block into cache
                        else{
                            if (node.cache[cacheIndex].address != 0xFF) { handleCacheReplacement(threadId, node.cache[cacheIndex]); }
                            node.cache[cacheIndex].address = msg.address;
                            node.cache[cacheIndex].value = msg.value;
                            node.cache[cacheIndex].state = EXCLUSIVE;
                        }
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

                        // Step 1 : Check if I'm the home node
                        if (procNodeAddr == threadId){

                            // Step A : Remove old node from bitvector
                            byte senderByte = (1 << msg.sender);
                            node.directory[memBlockAddr].bitVector = node.directory[memBlockAddr].bitVector & (~senderByte);

                            // Step B : Check how many sharers and update directory state
                            if ( node.directory[memBlockAddr].bitVector == 0x00 ){ node.directory[memBlockAddr].state = U; }
                            else { node.directory[memBlockAddr].state = EM; }

                            // Step C : Inform remaining sharer to change state
                            byte remOwner = (node.directory[memBlockAddr].bitVector);
                            message evictSharedMsg;
                            evictSharedMsg.type = EVICT_SHARED;
                            evictSharedMsg.sender = threadId;
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
                        (node.memory)[memBlockAddr] = msg.value;
                        node.directory[memBlockAddr].bitVector = 0x00;
                        node.directory[memBlockAddr].state = U;

                        break;
                }
                messageBuffers[threadId].count --;
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
            }
            else {
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
                    // printf("Read %u from instruction\n", node.cache[cacheIndex].value);
                }
                // Read-miss, send message
                else{
                    // printf("Read miss for mem block %d in %d, sending msg to %d\n", memBlockAddr, threadId, instrHomeNodeAddr);
                    message readMissMsg;
                    readMissMsg.type = READ_REQUEST;
                    readMissMsg.sender = threadId;
                    readMissMsg.address = instr.address;
                    sendMessage(instrHomeNodeAddr, readMissMsg);
                }
            } 
            // Write operation
            else {
                // printf("Write miss in %d\n", threadId);
                // IMPLEMENT - DONE?
                // check if memory block is present in cache
                // if cache hit and cacheline state is not invalid, then it is a write hit

                // Some non-empty cacheline present - have to check status
                if (node.cache[cacheIndex].address != 0xFF) 
                {
                    switch(node.cache[cacheIndex].state)
                    {
                        // if cache miss or cacheline state is invalid, then it is a write miss, send a WRITE_REQUEST to home node on a write miss
                        case INVALID :
                            message invWriteMissMsg;
                            invWriteMissMsg.sender = threadId;
                            invWriteMissMsg.address = instr.address;
                            invWriteMissMsg.type = WRITE_REQUEST;
                            sendMessage(instrHomeNodeAddr, invWriteMissMsg);
                            break;
                        
                        // if shared, other nodes contain this memory block, request home node to send list of sharers. 
                        // This node will have to send an UPGRADE to home node to promote directory from S to EM, 
                        // and also invalidate the entries in the sharers
                        case SHARED :
                            message shareWriteMissMsg;
                            shareWriteMissMsg.sender = threadId;
                            shareWriteMissMsg.address = instr.address;
                            shareWriteMissMsg.type = UPGRADE;
                            sendMessage(instrHomeNodeAddr, shareWriteMissMsg);
                            break;
                        
                        // if modified or exclusive, update cache directly as only this node
                        // contains the memory block, no network transactions required
                        default :
                            //printf("Write %u from instruction\n", instr.value);
                            node.cache[cacheIndex].value = instr.value;
                            break;

                    }
                }  
                // Write-miss
                else{
                    message writeMissMsg;
                    writeMissMsg.type = WRITE_REQUEST;
                    writeMissMsg.sender = omp_get_thread_num();
                    writeMissMsg.address = instr.address;
                    sendMessage(instrHomeNodeAddr, writeMissMsg);
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

    int nextTail = (messageBuffers[receiver].tail + 1) % MSG_BUFFER_SIZE;
    if (nextTail == messageBuffers[receiver].head) {
        fprintf(stderr, "Warning: Buffer full for processor %d. Dropping message.\n", receiver);
        omp_unset_lock(&msgBufferLocks[receiver]);
        return;
    }

    // Step2 : Accessing the message buffer of receiver and adding message at tail index - assuming tail is at last insertable position
    (messageBuffers[receiver].queue)[messageBuffers[receiver].tail] = msg;
    // Setting tail to the next insert-able position
    messageBuffers[receiver].tail = (messageBuffers[receiver].tail + 1) % MSG_BUFFER_SIZE;

    // Step 3 : Increase count of messages processed in receiver
    messageBuffers[receiver].count ++;

    // Step 4 : Unlock
    omp_unset_lock(&msgBufferLocks[receiver]);
}



void sendToSharers(byte bitVector, message msg)
{
    int procNum = 0;
    while (bitVector != 0x00){
        if (bitVector & 1)
        {
            printf("Sending message to sharer %d\n", procNum);
            sendMessage(procNum, msg);
        }
        bitVector >>= 1;
        procNum ++;
    }
}

// Getting byte representation (one-hot encoding here) of a given processor number
byte num2Byte(int sender){
    byte byteSender;
    byteSender = 1 << sender;
    return byteSender;
}


void handleCacheReplacement( int sender, cacheLine oldCacheLine ) {
    // IMPLEMENT - DONE
    // Notify the home node before a cacheline gets replaced
    // Extract the home node's address and memory block index from cacheline address

    // Step 1 : Use a bit mask to get first 4 bits
    byte procNodeAddr = (oldCacheLine.address & 0xF0) >> 4;
    
    switch ( oldCacheLine.state ) {
        case EXCLUSIVE:
        case SHARED:
            // IMPLEMENT - DONE?
            // If cache line was shared, inform home node about the eviction
            message sMsgToSend;
            sMsgToSend.sender = sender;
            sMsgToSend.address = oldCacheLine.address;
            sMsgToSend.type = EVICT_SHARED;
            // Step 2 : Send the message
            sendMessage(procNodeAddr, sMsgToSend);
            break;

        case MODIFIED:
            // IMPLEMENT - DONE?
            // If cache line was modified, send updated value to home node 
            // so that memory can be updated before eviction
            message mMsgToSend;
            mMsgToSend.sender = sender;
            mMsgToSend.address = oldCacheLine.address;
            mMsgToSend.type = EVICT_MODIFIED;
            mMsgToSend.value = oldCacheLine.value;
            // Step 2 : Send the message
            sendMessage(procNodeAddr, mMsgToSend);
            break;
            
        default:
            // No action required for INVALID or EXCLUSIVE states
            break;
    }
}

void printMessage(message msg, int sentTo)
{
    char* arr[] = {"READ_REQUEST", "WRITE_REQUEST", "REPLY_RD", "REPLY_WR", "REPLY_ID", "INV", "UPGRADE", "WRITEBACK_INV", "WRITEBACK_INT", "FLUSH", "FLUSH_INVACK", "EVICT_SHARED", "EVICT_MODIFIEFD"};
    printf("Sent to : %d\n", sentTo);
    printf("Transaction type : %s\n", arr[msg.type]);
    printf("Sender : %d\n", msg.sender);
    printf("Address : %d\n", msg.address);
    printf("Value : %d\n", msg.value);
    
    int bitVec[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    if (msg.bitVector == 0x00) { bitVec[8] = 1; }
    int proc = 0;
    while (msg.bitVector != 0x00){
        if (msg.bitVector & 1){
            bitVec[8 - proc] = 1;
        }
        msg.bitVector >>= 1;
        proc ++;
    }
    printf("Bit vector : ");
    for (int i = 0; i < 8; i ++) { printf("%d", bitVec[i]); }
    printf("\nSecond Receiver : %d\n", msg.secondReceiver);
    

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

    char filename[32];
    snprintf(filename, sizeof(filename), "core_%d_output.txt", processorId);

    FILE *file = fopen(filename, "w");
    if (!file) {
        printf("Error: Could not open file %s\n", filename);
        return;
    }

    fprintf(file, "=======================================\n");
    fprintf(file, " Processor Node: %d\n", processorId);
    fprintf(file, "=======================================\n\n");

    // Print memory state
    fprintf(file, "-------- Memory State --------\n");
    fprintf(file, "| Index | Address |   Value  |\n");
    fprintf(file, "|----------------------------|\n");
    for (int i = 0; i < MEM_SIZE; i++) {
        fprintf(file, "|  %3d  |  0x%02X   |  %5d   |\n", i, (processorId << 4) + i,
                node.memory[i]);
    }
    fprintf(file, "------------------------------\n\n");

    // Print directory state
    fprintf(file, "------------ Directory State ---------------\n");
    fprintf(file, "| Index | Address | State |    BitVector   |\n");
    fprintf(file, "|------------------------------------------|\n");
    for (int i = 0; i < MEM_SIZE; i++) {
        fprintf(file, "|  %3d  |  0x%02X   |  %2s   |   0x%08B   |\n",
                i, (processorId << 4) + i, dirStateStr[node.directory[i].state],
                node.directory[i].bitVector);
    }
    fprintf(file, "--------------------------------------------\n\n");
    
    // Print cache state
    fprintf(file, "------------ Cache State ----------------\n");
    fprintf(file, "| Index | Address | Value |    State    |\n");
    fprintf(file, "|---------------------------------------|\n");
    for (int i = 0; i < CACHE_SIZE; i++) {
        fprintf(file, "|  %3d  |  0x%02X   |  %3d  |  %8s \t|\n",
               i, node.cache[i].address, node.cache[i].value,
               cacheStateStr[node.cache[i].state]);
    }
    fprintf(file, "----------------------------------------\n\n");

    fclose(file);
}