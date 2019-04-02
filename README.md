# In-Memory-File-System-Simulator
Using FUSE to allow users to access your simulated file system made with hash tables and file mounting

STEP 1

The file system volume consists of three parts:

the superblock,
a bit vector for maintaining the free storage on the volume, and
the storage comprised of blocks.
The seed code defines sizes of all the components of the volume; you may change them if your Ubuntu Linux system does not have sufficiently large memory.

The superblock holds information about the file system such as the number of blocks, the size of a block, and the reference to the block holding the root folder of the file system.

The bitvector that spans a number of blocks holds bits that indicate whether the corresponding blocks in the volume are free or not; hence, there are as many bits in the bitvector as there are blocks in the storage. You are required to use fast bit-wise operations to search and modify bits in the bitvector.

The storage consists of blocks that are used by folders and files. Each block can hold either a folder or a file descriptor, an index to other blocks, or raw data (file content as bytes).

A file-descriptor node holds meta-data information about a file or a directory such as its size, access rights, creation time, access time, and modification time, along with a reference to the block with actual content of the file or the directory. The superblock contains a reference to the block holding a file descriptor for the root folder of the volume.

In a block with the type of a folder, the size field indicates the number of files in the folder, and the block reference field points to the index block that holds references to all files or folders in this folder. Each reference is an index to the block holding the file descriptor of the corresponding file or a folder. Of course, each of the subfolders may in turn hold other files or folders, and so on. There is an upper limit on the number of files or folders in a single folder.

In case of a block with a type of a file, the size field in the file descriptor indicates the actual size of the file. The block reference either points to a single data block if the size of the file is less than the size of a block (less the space needed for the type), or to an index block holding references to data blocks for larger files.

In this step, implement functions to:

create the file system,
create a file or a folder,
delete a file or a folder, and
obtain file information.
The files are predefined for you in the code with extensive descriptions.


