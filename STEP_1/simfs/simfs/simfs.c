// Joe Harkins
// 04/02/19
// Project 1
#include "simfs.h"

//////////////////////////////////////////////////////////////////////////
//
// allocation of the in-memory data structures
//
//////////////////////////////////////////////////////////////////////////

SIMFS_CONTEXT_TYPE *simfsContext; // all in-memory information about the system
SIMFS_VOLUME *simfsVolume;


//////////////////////////////////////////////////////////////////////////
//
// simfs function implementations
//
//////////////////////////////////////////////////////////////////////////

/*****
 * Retuns a hash value within the limits of the directory.
 */

inline unsigned long hash(unsigned char *str)
{
    register unsigned long hash = 5381;
    register unsigned char c;

    while ((c = *str++) != '\0')
        hash = ((hash << 5) + hash) ^ c; /* hash * 33 + c */

    return hash % SIMFS_DIRECTORY_SIZE;
}

/*****
 * Find a free block in a bit vector.
 */
inline unsigned short simfsFindFreeBlock(unsigned char *bitvector)
{
    unsigned short i = 0;
    while (bitvector[i] == 0xFF)
        i += 1;

    register unsigned char mask = 0x80;
    unsigned short j = 0;
    while (bitvector[i] & mask)
    {
        mask >>= 1;
        ++j;
    }

    return (i * 8) + j; // i bytes and j bits are all "1", so this formula points to the first "0"
}

/***
 * Three functions for bit manipulation.
 */
inline void simfsFlipBit(unsigned char *bitvector, unsigned short bitIndex)
{
    unsigned short blockIndex = bitIndex / 8;
    unsigned short bitShift = bitIndex % 8;

    register unsigned char mask = 0x80;
    bitvector[blockIndex] ^= (mask >> bitShift);
}

inline void simfsSetBit(unsigned char *bitvector, unsigned short bitIndex)
{
    unsigned short blockIndex = bitIndex / 8;
    unsigned short bitShift = bitIndex % 8;

    register unsigned char mask = 0x80;
    bitvector[blockIndex] |= (mask >> bitShift);
}

inline void simfsClearBit(unsigned char *bitvector, unsigned short bitIndex)
{
    unsigned short blockIndex = bitIndex / 8;
    unsigned short bitShift = bitIndex % 8;

    register unsigned char mask = 0x80;
    bitvector[blockIndex] &= ~(mask >> bitShift);
}

/***
 * Allocates space for the file system and saves it to disk.
 */
SIMFS_ERROR simfsCreateFileSystem(char *simfsFileName)
{
    FILE *file = fopen(simfsFileName, "wb");
    if (file == NULL)
        return SIMFS_ALLOC_ERROR;

    // --- create the OS context ---

    printf("Size of SIMFS_CONTEXT_TYPE: %ld\n", sizeof(SIMFS_CONTEXT_TYPE));
    simfsContext = malloc(sizeof(SIMFS_CONTEXT_TYPE));
    if (simfsContext == NULL)
        return SIMFS_ALLOC_ERROR;

    for (int i = 0; i < SIMFS_MAX_NUMBER_OF_OPEN_FILES; i++)
        simfsContext->globalOpenFileTable[i].type = SIMFS_INVALID_CONTENT_TYPE;  // indicates  empty slot

    for (int i = 0; i < SIMFS_DIRECTORY_SIZE; i++)
        simfsContext->directory[i] = NULL;

    memset(simfsContext->bitvector, 0, SIMFS_NUMBER_OF_BLOCKS / 8);

    simfsContext->processControlBlocks = NULL;

    // --- create the volume ---

    printf("Size of SIMFS_VOLUME: %ld\n", sizeof(SIMFS_VOLUME));
    simfsVolume = malloc(sizeof(SIMFS_VOLUME));
    if (simfsVolume == NULL)
        return SIMFS_ALLOC_ERROR;

    // initialize the superblock

    simfsVolume->superblock.attr.nextUniqueIdentifier = SIMFS_INITIAL_VALUE_OF_THE_UNIQUE_FILE_IDENTIFIER;
    simfsVolume->superblock.attr.rootNodeIndex = SIMFS_ROOT_NODE_INDEX;
    simfsVolume->superblock.attr.blockSize = SIMFS_BLOCK_SIZE;
    simfsVolume->superblock.attr.numberOfBlocks = SIMFS_NUMBER_OF_BLOCKS;

    // initialize the bitvector

    memset(simfsVolume->bitvector, 0, SIMFS_NUMBER_OF_BLOCKS / 8);

    // initialize the blocks holding the root folder

    // initialize the root folder

    simfsVolume->block[0].type = SIMFS_FOLDER_CONTENT_TYPE;
    // root folder always has "0" as the identifier
    simfsVolume->block[0].content.fileDescriptor.identifier = simfsVolume->superblock.attr.nextUniqueIdentifier++;
    simfsVolume->block[0].content.fileDescriptor.type = SIMFS_FOLDER_CONTENT_TYPE;
    strcpy(simfsVolume->block[0].content.fileDescriptor.name, "/");
    simfsVolume->block[0].content.fileDescriptor.accessRights = umask(00000);
    simfsVolume->block[0].content.fileDescriptor.owner = 0; // arbitrarily simulated
    simfsVolume->block[0].content.fileDescriptor.size = 0;

    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    simfsVolume->block[0].content.fileDescriptor.creationTime = time.tv_sec;
    simfsVolume->block[0].content.fileDescriptor.lastAccessTime = time.tv_sec;
    simfsVolume->block[0].content.fileDescriptor.lastModificationTime = time.tv_sec;

    // initialize the index block of the root folder

    // first, point from the root file descriptor to the index block
    simfsVolume->block[0].content.fileDescriptor.block_ref = 1;

    simfsVolume->block[1].type = SIMFS_INDEX_CONTENT_TYPE;

    // indicate that the blocks #0 and #1 are allocated

    // using the function to find a free block for testing purposes
    simfsFlipBit(simfsVolume->bitvector, simfsFindFreeBlock(simfsVolume->bitvector)); // should be 0
    simfsFlipBit(simfsVolume->bitvector, simfsFindFreeBlock(simfsVolume->bitvector)); // should be 1

    // sample alternative #1 - illustration of bit-wise operations
//    simfsVolume->bitvector[0] = 0;
//    simfsVolume->bitvector[0] |= 0x01 << 7; // set the first bit of the bit vector
//    simfsVolume->bitvector[0] += 0x80 >> 1; // flip the first bit of the bit vector

    // sample alternative #2 - less educational, but fastest
//     simfsVolume->bitvector[0] = 0xC0;
    // 0xC0 is 11000000 in binary (showing the root block and root's index block taken)

    fwrite(simfsVolume, 1, sizeof(SIMFS_VOLUME), file);

    fclose(file);

    return SIMFS_NO_ERROR;
}

/***
 * Loads the file system from a disk and constructs in-memory directory of all files is the system.
 *
 * Starting with the file system root (pointed to from the superblock) traverses the hierarchy of directories
 * and adds an entry for each folder or file to the directory by hashing the name and adding a directory
 * entry node to the conflict resolution list for that entry. If the entry is NULL, the new node will be
 * the only element of that list. If the list contains more than one element, then multiple files hashed to
 * the same value, so the unique file identifier can be used to determine which entry is applicable. The
 * identifier must be the same as the identifier in the file descriptor pointed to by the node reference.
 *
 * The function sets the current working directory to refer to the block holding the root of the volume. This will
 * be changed as the user navigates the file system hierarchy.
 *
 */
SIMFS_ERROR simfsMountFileSystem(char *simfsFileName)
{

    simfsContext = malloc(sizeof(SIMFS_CONTEXT_TYPE));
    if (simfsContext == NULL)
        return SIMFS_ALLOC_ERROR;

    simfsVolume = malloc(sizeof(SIMFS_VOLUME));
    if (simfsVolume == NULL)
        return SIMFS_ALLOC_ERROR;

    FILE *file = fopen(simfsFileName, "rb");
    if (file == NULL)
        return SIMFS_ALLOC_ERROR;

    fread(simfsVolume, 1, sizeof(SIMFS_VOLUME), file);
    fclose(file);

    // TODO: complete

    return SIMFS_NO_ERROR;
}

/***
 * Saves the file system to a disk and de-allocates the memory.
 *
 * Assumes that all synchronization has been done.
 *
 */
SIMFS_ERROR simfsUmountFileSystem(char *simfsFileName)
{
    FILE *file = fopen(simfsFileName, "wb");
    if (file == NULL)
        return SIMFS_ALLOC_ERROR;

    fwrite(simfsVolume, 1, sizeof(SIMFS_VOLUME), file);
    fclose(file);

    free(simfsVolume);
    free(simfsContext);

    return SIMFS_NO_ERROR;
}

//////////////////////////////////////////////////////////////////////////

/***
 * Depending on the type parameter the function creates a file or a folder in the current directory
 * of the process. If the process does not have an entry in the processControlBlock, then the root directory
 * is assumed to be its current working directory.
 *
 * If a file with the same name already exists in the current directory, it returns SIMFS_DUPLICATE_ERROR.
 *
 * Otherwise:
 *    - set the folder/file's identifier to the current value of the next unique identifier from the superblock;
 *      then it increments the next available value in the superblock (to prepare it for the next created file)
 *    - finds an available block in the storage using the in-memory bitvector and flips the bit to indicate
 *      that the block is taken
 *    - initializes a local buffer for the file descriptor block with the block type depending on the parameter type
 *      (i.e., folder or file)
 *    - creates an entry in the conflict resolution list for the corresponding in-memory directory entry
 *    - copies the local buffer to the disk block that was found to be free
 *    - copies the in-memory bitvector to the bitevector blocks on the simulated disk
 *
 *  The access rights and the the owner are taken from the context (umask and uid correspondingly).
 *
 */
SIMFS_ERROR simfsCreateFile(SIMFS_NAME_TYPE fileName, SIMFS_CONTENT_TYPE type)
{
    // TODO: Done
   *simfsContext->bitvector = *simfsVolume->bitvector; 
    SIMFS_NAME_TYPE  fileDescriptorPath= "";


    if(simfsContext->processControlBlocks == NULL) //Root Directory
    {
        sprintf(fileDescriptorPath,"/%s/",fileName);
    }
    else // Need to go into the process control block and get current working directory
    {
        sprintf(fileDescriptorPath,"%s%s/",simfsVolume->block[simfsContext->processControlBlocks->currentWorkingDirectory].content.fileDescriptor.name,fileName);
    }


    SIMFS_DIR_ENT *nodeSearch;
    unsigned long hashIndex = hash(fileDescriptor.name);

    nodeSearch = &simfsContext->directory[hashIndex];

    // Traverse the directory to determine if the fileName is already created.
    while(nodeSearch!= NULL)
    {
        if(strcmp(fileDescriptorPath, simfsVolume->block[nodeSearch->nodeReference].content.fileDescriptor.name) == 0)
        {
            // Duplicate Found
            return SIMFS_DUPLICATE_ERROR;
        }
        else
        {
            nodeSearch = nodeSearch->next;
        }
    }
   // End of duplicate search
    unsigned short freeBlock= simfsFindFreeBlock(simfsContext->bitvector); // Find the free block
    unsigned short freeFolderBlock;
    simfsFlipBit(simfsContext->bitvector, freeBlock); // We need to flip the block to state this block is no longer free (File is going to be placed here)
    simfsVolume->block[freeBlock].type = type;


    //File Descriptor Node contains all the information of the file//
    SIMFS_FILE_DESCRIPTOR_TYPE *temp = (SIMFS_FILE_DESCRIPTOR_TYPE *)malloc(sizeof(struct simfs_file_descriptor_type));
    memcpy(temp->name, fileDescriptorPath,strlen(fileDescriptorPath)+1);
    temp->type = type;
    temp->creationTime = time(0);
    temp->lastAccessTime = temp->creationTime;
    temp->lastModificationTime = temp->lastAccessTime;
    temp->accessRights = 0777; // Simulated Value to give this file an access right.
    temp->size = 0;

        if (type == SIMFS_FOLDER_CONTENT_TYPE )
        {

        freeFolderBlock = simfsFindFreeBlock(simfsContext->bitvector);
        simfsFlipBit(simfsContext->bitvector, freeFolderBlock);
        simfsVolume->block[freeFolderBlock].type = type;
        temp->block_ref = freeFolderBlock;

        }
        else // If it is a FILE_CONTENT_TYPE
        {
            temp->block_ref = SIMFS_INVALID_INDEX;
        }


    simfsVolume->block[freeBlock].content.fileDescriptor = *temp;

    nodeSearch = &simfsContext->directory[hashIndex];
    while(nodeSearch->next != NULL)
    {
        nodeSearch = nodeSearch->next;
    }
    nodeSearch->next = (SIMFS_DIR_ENT*)malloc(sizeof(SIMFS_DIR_ENT));
    nodeSearch = nodeSearch->next;
    //node reference describes the location in the volume
    nodeSearch->nodeReference = freeBlock;
    nodeSearch->next = NULL;

    *simfsVolume->bitvector = *simfsContext->bitvector; // Copy the In-Memory bitvector to the simulated disk

    return SIMFS_NO_ERROR;
}

//////////////////////////////////////////////////////////////////////////

/***
 * Deletes a file from the file system.
 *
 * Hashes the file name and check if the file is in the directory. If not, then it returns SIMFS_NOT_FOUND_ERROR.
 * Otherwise:
 *    - finds the reference to the file descriptor block
 *    - if the referenced block is a folder that is not empty, then returns SIMFS_NOT_EMPTY_ERROR.
 *    - Otherwise:
 *       - checks if the process owner can delete this file or folder; if not, it returns SIMFS_ACCESS_ERROR.
 *       - Otherwise:
 *          - frees all blocks belonging to the file by flipping the corresponding bits in the in-memory bitvector
 *          - frees the reference block by flipping the corresponding bit in the in-memory bitvector
 *          - clears the entry in the folder by removing the corresponding node in the list associated with
 *            the slot for this file
 *          - copies the in-memory bitvector to the bitvector blocks on the simulated disk
 */
SIMFS_ERROR simfsDeleteFile(SIMFS_NAME_TYPE fileName)
{
    // TODO: implement
    

     SIMFS_NAME_TYPE  fileDescriptorPath= "";

    if(simfsContext->processControlBlocks == NULL) //Root Directory
    {
        sprintf(fileDescriptorPath,"%s%s/","/",fileName);
    }
    else // Need to go into the process control block and get current working directory
    {
    
        sprintf(fileDescriptorPath,"%s%s/",simfsVolume->block[simfsContext->processControlBlocks->currentWorkingDirectory].content.fileDescriptor.name,fileName);
    }


    SIMFS_DIR_ENT *nodeCurrent, *nodePrevious = nodeCurrent;
    unsigned long hashIndex = hash(fileDescriptorPath);

    nodeCurrent = &simfsContext->directory[hashIndex];

    while(nodeCurrent != NULL && strcmp(fileDescriptorPath, simfsVolume->block[nodeCurrent->nodeReference].content.fileDescriptor.name) != 0)
    {
       nodePrevious = nodeCurrent;
       nodeCurrent = nodeCurrent->next;
    }
    
    if (nodeCurrent == NULL)
    {
        return SIMFS_NOT_FOUND_ERROR;
    }

        SIMFS_FILE_DESCRIPTOR_TYPE *temp = &simfsVolume->block[nodeCurrent->nodeReference].content.fileDescriptor;
    
    if (temp->type == SIMFS_FOLDER_CONTENT_TYPE)  // We have a Folder -------------------
    {
        //Check if the Folder is Empty
        if (temp->size != 0)
        {
            return SIMFS_NOT_EMPTY_ERROR;
        }
        else
        {
            temp->accessRights = (0777) << 1;
            //check if I have the access rights to accomplish such a task.
            if(temp->accessRights >= 256)
            {
                // free the folder
                if (nodeCurrent->next == NULL)// Last ONE!
                {
                    simfsFlipBit(simfsContext->bitvector, temp->block_ref);
                    simfsFlipBit(simfsContext->bitvector, nodeCurrent->nodeReference);
                    nodePrevious->next = NULL;
                }
                else if (nodeCurrent->next != NULL && nodePrevious != nodeCurrent) // If it is the first thing.
                {
                    simfsFlipBit(simfsContext->bitvector, temp->block_ref);
                    simfsFlipBit(simfsContext->bitvector, nodeCurrent->nodeReference);
                    nodePrevious->next = nodeCurrent->next;
                    nodeCurrent = NULL;
                }
                else // Our Previous Node is the same as our current node
                {
                    simfsFlipBit(simfsContext->bitvector, temp->block_ref);
                    simfsFlipBit(simfsContext->bitvector, nodeCurrent->nodeReference);
                    nodePrevious = nodeCurrent->next;
                    nodeCurrent = NULL;
                    simfsContext->directory[hashIndex] = nodePrevious;
                }
            }
            else
            {
                return SIMFS_ACCESS_ERROR;
            }

                

        }
    }
    else // We have a File -------------------
    {

        temp->accessRights = (0777) << 1;
            
            if(temp->accessRights >= 256)
            {
                unsigned short numOfDataBlocksForFiles = temp->size/SIMFS_DATA_SIZE;
                if ((temp->size % SIMFS_DATA_SIZE) > 0)
                {
                    numOfDataBlocksForFiles += 1;
                }

                    if (numOfDataBlocksForFiles == 1)  // There is only one file to handle.
                    {
                        simfsFlipBit(simfsContext->bitvector, temp->block_ref);
                        numOfDataBlocksForFiles--;
                    }
                    else // We need to account for multiple files. 
                    {

                        SIMFS_INDEX_TYPE indexLocation = temp->block_ref;
                        SIMFS_INDEX_TYPE *searchIndex = simfsVolume->block[indexLocation].content.index;


                                int counter = 0;
                                while (numOfDataBlocksForFiles != 0)
                                {
                                    counter = counter % SIMFS_INDEX_SIZE; // 

                                    if (simfsVolume->block[searchIndex[counter]].type == SIMFS_DATA_CONTENT_TYPE)
                                    {
                                        simfsFlipBit(simfsContext->bitvector, searchIndex[counter]);
                                        numOfDataBlocksForFiles--;
                                    }
                                    else  // We are dealing with an INDEX_CONTENT_TYPE
                                    {
                                        simfsFlipBit(simfsContext->bitvector, indexLocation);
                                        indexLocation = searchIndex[counter];
                                        searchIndex[counter] = simfsVolume->block[searchIndex[counter]].content.index;
                                    }
                                     counter++;
                                }

                                simfsFlipBit(simfsContext->bitvector, indexLocation);

                    }
                    simfsFlipBit(simfsContext->bitvector, nodeCurrent->nodeReference);


                // free the folder
                if (nodeCurrent->next == NULL)// Last ONE!
                {
                    simfsFlipBit(simfsContext->bitvector, temp->block_ref);
                    simfsFlipBit(simfsContext->bitvector, nodeCurrent->nodeReference);
                    nodePrevious->next = NULL;
                }
                else if (nodeCurrent->next != NULL && nodePrevious != nodeCurrent) // If it is the first thing.
                {
                    simfsFlipBit(simfsContext->bitvector, temp->block_ref);
                    simfsFlipBit(simfsContext->bitvector, nodeCurrent->nodeReference);
                    nodePrevious->next = nodeCurrent->next;
                    nodeCurrent = NULL;
                }
                else // Our Previous Node is the same as our current node
                {
                    simfsFlipBit(simfsContext->bitvector, temp->block_ref);
                    simfsFlipBit(simfsContext->bitvector, nodeCurrent->nodeReference);
                    nodePrevious = nodeCurrent->next;
                    nodeCurrent = NULL;
                    simfsContext->directory[hashIndex] = nodePrevious;
                }



            }
            else
            {
                return SIMFS_ACCESS_ERROR;
            }


    }
    // Set File type when deleting to SIMFS_INVALID_Content aTYPE

    *simfsVolume->bitvector = *simfsContext->bitvector; // Copy the In-Memory bit vector to the simulated disk
    return SIMFS_NO_ERROR;
}

//////////////////////////////////////////////////////////////////////////

/***
 * Finds the file in the in-memory directory and obtains the information about the file from the file descriptor
 * block referenced from the directory.
 *
 * If the file is not found, then it returns SIMFS_NOT_FOUND_ERROR
 */
SIMFS_ERROR simfsGetFileInfo(SIMFS_NAME_TYPE fileName, SIMFS_FILE_DESCRIPTOR_TYPE *infoBuffer)
{
    // TODO: implement

     SIMFS_NAME_TYPE  fileDescriptorPath= "";
    if(simfsContext->processControlBlocks == NULL) //Root Directory
    {
        sprintf(fileDescriptorPath,"%s%s/","/",fileName);
    }
    else // Need to go into the process control block and get current working directory
    {

        sprintf(fileDescriptorPath,"%s%s/",simfsVolume->block[simfsContext->processControlBlocks->currentWorkingDirectory].content.fileDescriptor.name,fileName);
    }

    SIMFS_DIR_ENT *nodeSearch;
    unsigned long hashIndex = hash(fileDescriptorPath);

    nodeSearch = &simfsContext->directory[hashIndex];

    // Traverse the directory to determine if the fileName is already created.
    while(nodeSearch!= NULL && strcmp(fileDescriptorPath, simfsVolume->block[nodeSearch->nodeReference].content.fileDescriptor.name)!= 0)
    {

            nodeSearch = nodeSearch->next;

    }
    *infoBuffer =  simfsVolume->block[nodeSearch->nodeReference].content.fileDescriptor;
    return SIMFS_NO_ERROR;
}

//////////////////////////////////////////////////////////////////////////

/***
 * Creates an in-memory description of the file for fast access.
 *
 * - hashes the name and searches for it in the in-memory directory resolving potential conflicts using the
 *   unique identifier.
 *
 *   If the file does not exist (i.e., the slot obtained by hashing is NULL), the SIMFS_NOT_FOUND_ERROR is returned.
 *
 * Otherwise:
 *
 *    - if there is a global entry for the file (as indicated by the index to the global open file table in the
 *      directory entry), then:
 *
 *       - it increases the reference count for this file
 *
 *       - otherwise
 *          - finds an empty slot in the global open file table and adds an entry for the file
 *          - copies the information from the file descriptor block referenced from the directory entry for
 *            this file to the new entry in the global open file table
 *          - sets the reference count of the file to 1
 *          - adds the index of the entry in the global open file table to the directory entry for this file
 *
 *   - checks if the process has its process control block in the processControlBlocks list
 *      - if not, then a file control block for the process is created and added to the list; the current
 *        working directory is initialized to the root of the volume and the number of the open files is
 *        initialized to 1; the process id should be simulated for testing; it will be possible to obtain
 *        the actual pid after integration with FUSE
 *
 *     - scans the per-process open file table of the process checking if an entry for the file already
 *      exists in the table (i.e., the file has already been opened)
 *
 *       - if the entry indeed does exists, the function returns the index of the entry through the parameter
 *         fileHandle, and then returns SIMFS_DUPLICATE_ERROR as the return value. This is not a fatal error.
 *
 *       - otherwise:
 *          - the function finds an empty slot in the table and fills it with the information including
 *            the index to the entry for this file in the global open file table
 *
 *          - returns the index to the new element of the per-process open file table through the parameter
 *            fileHandle and SIMFS_NO_ERROR as the return value.
 *
 * If there is no free slot for the file in either the global file table or in the per-process
 * file table, or if there is any other allocation problem, then the function returns SIMFS_ALLOC_ERROR.
 *
 */
SIMFS_ERROR simfsOpenFile(SIMFS_NAME_TYPE fileName, SIMFS_FILE_HANDLE_TYPE *fileHandle)
{
    // TODO: implement
    SIMFS_NAME_TYPE  fileDescriptorPath= "";
    SIMFS_FILE_DESCRIPTOR_TYPE file_descriptor_type;
    unsigned long hashIndex = hash(fileName);
    for (int j = 0; j < SIMFS_DIRECTORY_SIZE ; ++j)
    {
        if(hash(simfsContext->directory[]))
        {
            SIMFS_DIR_ENT directoryEntry;
            if(directoryEntry[fileDescriptorPath] == NULL)
            {
                return SIMFS_NOT_FOUND_ERROR;
            }
            else
            {
                if(simfsContext[fileDescriptorPath] != NULL)
                {
                    simfsContext->globalOpenFileTable->referenceCount++;
                }
                else
                {
                    for (int i = 0; i < SIMFS_MAX_NUMBER_OF_OPEN_FILES; ++i)
                    {
                        if(simfsContext->globalOpenFileTable[i].referenceCount == NULL)
                        {

                            simfsContext->globalOpenFileTable[i] =

                                    simfsContext->globalOpenFileTable[i].referenceCount++;
                            simfsContext->directory[hashIndex]
                        }
                    }
                }
            }
        }
    }
    return SIMFS_NO_ERROR;
}

//////////////////////////////////////////////////////////////////////////

/***
 * The function replaces content of a file with new one pointed to by the parameter writeBuffer.
 *
 * Checks if the file handle points to a valid file descriptor of an open file. Any issues should be reported by
 * returning SIMFS_SYSTEM_ERROR. In this case, further code debugging is needed.
 *
 * Otherwise, it checks the access rights for writing. If the process owner is not allowed to write to the file,
 * then the function returns SIMFS_ACCESS_ERROR.
 *
 * Then, the function calculates the space needed for the new content and checks if the write buffer can fit into
 * the remaining free space in the file system. If not, then the SIMFS_ALLOC_ERROR is returned.
 *
 * Otherwise, the function:
 *    - acquires as many new blocks as needed to hold the new content modifying corresponding bits in
 *      the in-memory bitvector,
 *    - copies the characters pointed to by the parameter writeBuffer (until '\0' but excluding it) to the
 *      new just acquired blocks,
 *    - copies any modified block of the in-memory bitvector to the corresponding bitvector block on the disk.
 *
 * If the new content has been written successfully, the function then removes all blocks currently held by
 * this file and modifies the file descriptor to reflect the new location, the new size of the file, and the new
 * times of last modification and access.
 *
 * This order of actions prevents file corruption, since in case of any error with writing new content, the file's
 * old version is intact. This technique is called copy-on-write and is an alternative to journalling.
 *
 * The function returns SIMFS_WRITE_ERROR in response to exception not specified earlier.
 *
 */
SIMFS_ERROR simfsWriteFile(SIMFS_FILE_HANDLE_TYPE fileHandle, char *writeBuffer)
{
    // TODO: implement
	SIMFS_NAME_TYPE fileDescriptor = "";
	unsigned long hashIndex = "";

    return SIMFS_NO_ERROR;
}

//////////////////////////////////////////////////////////////////////////

/***
 * The function returns the complete content of the file to the caller through the parameter readBuffer.
 *
 * Checks if the file handle (i.e., an index to an entry in the per-process open files table) points to
 * a valid file descriptor of an open file. Any issues should be reported by returning SIMFS_SYSTEM_ERROR.
 * In this case, further code debugging is needed.
 *
 * Otherwise, it checks the user's access right to read the file. If the process owner is not allowed to read the file,
 * then the function returns SIMFS_ACCESS_ERROR.
 *
 * Otherwise, the function allocates memory sufficient to hold the read content with an appended end of string
 * character; the pointer to newly allocated memory is passed back through the readBuffer parameter. All the content
 * of the blocks is concatenated using the allocated space, and an end of string character is appended at the end of
 * the concatenated content.
 *
 * The function returns SIMFS_READ_ERROR in response to exception not specified earlier.
 *
 */
SIMFS_ERROR simfsReadFile(SIMFS_FILE_HANDLE_TYPE fileHandle, char **readBuffer)
{
    // TODO: implement

    return SIMFS_NO_ERROR;
}

//////////////////////////////////////////////////////////////////////////

/***
 * Removes the entry for the file with the file handle provided as the parameter from the open file table
 * for this process. It decreases the number of open files for the file in the process control block of
 * this process, and if it becomes zero, then the process control block for this process is removed from
 * the processControlBlocks list.
 *
 * Decreases the reference count in the global open file table, and if that number is 0, it also removes the entry
 * for this file from the global open file table. In this case, it also removes the index to the global open file
 * table from the directory entry for the file by overwriting it with SIMFS_INVALID_OPEN_FILE_TABLE_INDEX.
 *
 */

SIMFS_ERROR simfsCloseFile(SIMFS_FILE_HANDLE_TYPE fileHandle)
{
    // TODO: implement

    return SIMFS_NO_ERROR;
}

//////////////////////////////////////////////////////////////////////////
//
// The following functions are provided only for testing without FUSE.
//
// When linked to the FUSE library, both user ID and process ID can be obtained by calling fuse_get_context().
// See: http://libfuse.github.io/doxygen/structfuse__context.html
//
//////////////////////////////////////////////////////////////////////////

/***
 * Simulates FUSE context to get values for user ID, process ID, and umask through fuse_context
 */

struct fuse_context *simfs_debug_get_context()
{

    // TODO: replace its use with FUSE's fuse_get_context()

    struct fuse_context *context = malloc(sizeof(struct fuse_context));

    context->fuse = NULL;
    context->uid = (uid_t) rand() % 10 + 1;
    context->pid = (pid_t) rand() % 10 + 1;
    context->gid = (gid_t) rand() % 10 + 1;
    context->private_data = NULL;
    context->umask = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH; // can be changed as needed

    return context;
}

/***
 * Generates random readable/printable content for testing
 */

char *simfsGenerateContent(int size)
{
    size = (size <= 0 ? rand() % 1000 : size); // arbitrarily chosen as an example

    char *content = malloc(size);

    int firstPrintable = ' ';
    int len = '~' - firstPrintable;

    for (int i = 0; i < size - 1; i++)
        *(content + i) = firstPrintable + rand() % len;

    content[size - 1] = '\0';
    return content;
}
