/* mbed Microcontroller Library
 * Copyright (c) 2017 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef LFSFILESYSTEM_H
#define LFSFILESYSTEM_H
#include "BlockDevice.h"
#include "FreeRTOS.h"
#include "freertos/semphr.h"
#include "lfs.h"

#define LFS_READ_SIZE CONFIG_LFS_READ_SIZE
#define LFS_PROG_SIZE CONFIG_LFS_PROG_SIZE
#define LFS_BLOCK_SIZE CONFIG_LFS_BLOCK_SIZE
#define LFS_LOOKAHEAD CONFIG_LFS_LOOKAHEAD
#define LFS_NO_INTRINSICS CONFIG_LFS_NO_INTRINSICS
#define LFS_NO_INFO CONFIG_LFS_NO_INFO
#define LFS_NO_DEBUG CONFIG_LFS_NO_DEBUG
#define LFS_NO_WARN CONFIG_LFS_NO_WARN
#define LFS_NO_ERROR CONFIG_LFS_NO_ERROR
#define LFS_NO_ASSERT CONFIG_LFS_NO_ASSERT

#define LFS_IFMT 0170000    //!< type of file
#define LFS_IFSOCK 0140000  //!< socket
#define LFS_IFLNK 0120000   //!< symbolic link
#define LFS_IFREG 0100000   //!< regular
#define LFS_IFBLK 0060000   //!< block special
#define LFS_IFDIR 0040000   //!< directory
#define LFS_IFCHR 0020000   //!< character special
#define LFS_IFIFO 0010000   //!< fifo special

#define LFS_S_IFMT LFS_IFMT      //!< type of file
#define LFS_S_IFSOCK LFS_IFSOCK  //!< socket
#define LFS_S_IFLNK LFS_IFLNK    //!< symbolic link
#define LFS_S_IFREG LFS_IFREG    //!< regular
#define LFS_S_IFBLK LFS_IFBLK    //!< block special
#define LFS_S_IFDIR LFS_IFDIR    //!< directory
#define LFS_S_IFCHR LFS_IFCHR    //!< character special
#define LFS_S_IFIFO LFS_IFIFO    //!< fifo special

#define LFS_S_IRWXU (LFS_S_IRUSR | LFS_S_IWUSR | LFS_S_IXUSR)
#define LFS_S_IRUSR 0000400  //!< read permission, owner
#define LFS_S_IWUSR 0000200  //!< write permission, owner
#define LFS_S_IXUSR 0000100  //!< execute/search permission, owner
#define LFS_S_IRWXG (LFS_S_IRGRP | LFS_S_IWGRP | LFS_S_IXGRP)
#define LFS_S_IRGRP 0000040  //!< read permission, group
#define LFS_S_IWGRP 0000020  //!< write permission, group
#define LFS_S_IXGRP 0000010  //!< execute/search permission, group
#define LFS_S_IRWXO (LFS_S_IROTH | LFS_S_IWOTH | LFS_S_IXOTH)
#define LFS_S_IROTH 0000004  //!< read permission, other
#define LFS_S_IWOTH 0000002  //!< write permission, other
#define LFS_S_IXOTH 0000001  //!< execute/search permission, other

#define LFS_NAME_MAX 255  //!< Maximum size of a name in a file path

#define LFS_O_RDONLY 0         //!< Open for reading
#define LFS_O_WRONLY 1         //!< Open for writing
#define LFS_O_RDWR 2           //!< Open for reading and writing
#define LFS_O_NONBLOCK 0x0004  //!< Non-blocking mode
#define LFS_O_APPEND 0x0008    //!< Set file offset to end of file prior to each write
#define LFS_O_CREAT 0x0200     //!< Create file if it does not exist
#define LFS_O_TRUNC 0x0400     //!< Truncate file to zero length
#define LFS_O_EXCL 0x0800      //!< Fail if file exists
#define LFS_O_BINARY 0x8000    //!< Open file in binary mode
#define LFS_O_ACCMODE (LFS_O_RDONLY | LFS_O_WRONLY | LFS_O_RDWR)

// Type definitions
typedef void *fs_file_t;
typedef void *fs_dir_t;
typedef unsigned long long fsblkcnt_t;  //!< Count of file system blocks
typedef unsigned int mode_t;            //!< Mode for opening files
typedef signed int ssize_t;             //!< Signed size type, usually encodes negative errors
typedef signed long off_t;              //!< Offset in a data stream

/* Refer to sys/stat standard
 * Note: Not all fields may be supported by the underlying filesystem
 */
struct lfs_stat {
    dev_t st_dev;      //!< Device ID containing file
    ino_t st_ino;      //!< File serial number
    mode_t st_mode;    //!< Mode of file
    nlink_t st_nlink;  //!< Number of links to file

    uid_t st_uid;  //!< User ID
    gid_t st_gid;  //!< Group ID

    off_t st_size;  //!< Size of file in bytes

    time_t st_atime;  //!< Time of last access
    time_t st_mtime;  //!< Time of last data modification
    time_t st_ctime;  //!< Time of last status change
};

struct lfs_statvfs {
    unsigned long f_bsize;   //!< Filesystem block size
    unsigned long f_frsize;  //!< Fragment size (block size)

    fsblkcnt_t f_blocks;  //!< Number of blocks
    fsblkcnt_t f_bfree;   //!< Number of free blocks
    fsblkcnt_t f_bavail;  //!< Number of free blocks for unprivileged users

    unsigned long f_fsid;  //!< Filesystem ID

    unsigned long f_namemax;  //!< Maximum filename length
};
/* The following are dirent.h definitions are declared here to guarantee
 * consistency where structure may be different with different toolchains
 */
struct lfs_dirent {
    char d_name[LFS_NAME_MAX + 1];  //!< Name of file
    uint8_t d_type;             //!< Type of file
};

enum {
    LFS_DT_UNKNOWN,  //!< The file type could not be determined.
    LFS_DT_FIFO,     //!< This is a named pipe (FIFO).
    LFS_DT_CHR,      //!< This is a character device.
    LFS_DT_DIR,      //!< This is a directory.
    LFS_DT_BLK,      //!< This is a block device.
    LFS_DT_REG,      //!< This is a regular file.
    LFS_DT_LNK,      //!< This is a symbolic link.
    LFS_DT_SOCK,     //!< This is a UNIX domain socket.
};

/**
 * LittleFileSystem, a little filesystem
 */
class LittleFileSystem {
   public:
    /** Lifetime of the LittleFileSystem
     *
     *  @param name         Name to add filesystem to tree as
     *  @param bd           BlockDevice to mount, may be passed instead to mount call
     *  @param read_size
     *      Minimum size of a block read. This determines the size of read buffers.
     *      This may be larger than the physical read size to improve performance
     *      by caching more of the block device.
     *  @param prog_size
     *      Minimum size of a block program. This determines the size of program
     *      buffers. This may be larger than the physical program size to improve
     *      performance by caching more of the block device.
     *  @param block_size
     *      Size of an erasable block. This does not impact ram consumption and
     *      may be larger than the physical erase size. However, this should be
     *      kept small as each file currently takes up an entire block.
     *  @param lookahead
     *      Number of blocks to lookahead during block allocation. A larger
     *      lookahead reduces the number of passes required to allocate a block.
     *      The lookahead buffer requires only 1 bit per block so it can be quite
     *      large with little ram impact. Should be a multiple of 32.
     */
    LittleFileSystem(const char *name = NULL, BlockDevice *bd = NULL,
                     lfs_size_t read_size = LFS_READ_SIZE, lfs_size_t prog_size = LFS_PROG_SIZE,
                     lfs_size_t block_size = LFS_BLOCK_SIZE, lfs_size_t lookahead = LFS_LOOKAHEAD);
    ~LittleFileSystem();

    /** Formats a block device with the LittleFileSystem
     *
     *  The block device to format should be mounted when this function is called.
     *
     *  @param bd       This is the block device that will be formated.
     *  @param read_size
     *      Minimum size of a block read. This determines the size of read buffers.
     *      This may be larger than the physical read size to improve performance
     *      by caching more of the block device.
     *  @param prog_size
     *      Minimum size of a block program. This determines the size of program
     *      buffers. This may be larger than the physical program size to improve
     *      performance by caching more of the block device.
     *  @param block_size
     *      Size of an erasable block. This does not impact ram consumption and
     *      may be larger than the physical erase size. However, this should be
     *      kept small as each file currently takes up an entire block.
     *  @param lookahead
     *      Number of blocks to lookahead during block allocation. A larger
     *      lookahead reduces the number of passes required to allocate a block.
     *      The lookahead buffer requires only 1 bit per block so it can be quite
     *      large with little ram impact. Should be a multiple of 32.
     */
    static int format(BlockDevice *bd, lfs_size_t read_size = LFS_READ_SIZE,
                      lfs_size_t prog_size = LFS_PROG_SIZE, lfs_size_t block_size = LFS_BLOCK_SIZE,
                      lfs_size_t lookahead = LFS_LOOKAHEAD);

    /** Mounts a filesystem to a block device
     *
     *  @param bd       BlockDevice to mount to
     *  @return         0 on success, negative error code on failure
     */
    int mount(BlockDevice *bd);

    /** Unmounts a filesystem from the underlying block device
     *
     *  @return         0 on success, negative error code on failure
     */
    int unmount();

    /** Reformats a filesystem, results in an empty and mounted filesystem
     *
     *  @param bd
     *      BlockDevice to reformat and mount. If NULL, the mounted
     *      block device will be used.
     *      Note: if mount fails, bd must be provided.
     *      Default: NULL
     *
     *  @return         0 on success, negative error code on failure
     */
    int reformat(BlockDevice *bd);

    /** Remove a file from the filesystem.
     *
     *  @param path     The name of the file to remove.
     *  @return         0 on success, negative error code on failure
     */
    int remove(const char *path);

    /** Rename a file in the filesystem.
     *
     *  @param path     The name of the file to rename.
     *  @param newpath  The name to rename it to
     *  @return         0 on success, negative error code on failure
     */
    int rename(const char *path, const char *newpath);

    /** Store information about the file in a stat structure
     *
     *  @param path     The name of the file to find information about
     *  @param st       The stat buffer to write to
     *  @return         0 on success, negative error code on failure
     */
    int stat(const char *path, struct lfs_stat *st);

    /** Create a directory in the filesystem.
     *
     *  @param path     The name of the directory to create.
     *  @param mode     The permissions with which to create the directory
     *  @return         0 on success, negative error code on failure
     */
    int mkdir(const char *path, mode_t mode);

    /** Store information about the mounted filesystem in a statvfs structure
     *
     *  @param path     The name of the file to find information about
     *  @param buf      The stat buffer to write to
     *  @return         0 on success, negative error code on failure
     */
    int statvfs(const char *path, struct lfs_statvfs *buf);

   protected:
    /** Open a file on the filesystem
     *
     *  @param file     Destination for the handle to a newly created file
     *  @param path     The name of the file to open
     *  @param flags    The flags to open the file in, one of LFS_O_RDONLY, LFS_O_WRONLY, LFS_O_RDWR,
     *                  bitwise or'd with one of LFS_O_CREAT, LFS_O_TRUNC, LFS_O_APPEND
     *  @return         0 on success, negative error code on failure
     */
    int file_open(fs_file_t *file, const char *path, int flags);

    /** Close a file
     *
     *  @param file     File handle
     *  return          0 on success, negative error code on failure
     */
    int file_close(fs_file_t file);

    /** Read the contents of a file into a buffer
     *
     *  @param file     File handle
     *  @param buffer   The buffer to read in to
     *  @param size     The number of bytes to read
     *  @return         The number of bytes read, 0 at end of file, negative error on failure
     */
    ssize_t file_read(fs_file_t file, void *buffer, size_t size);

    /** Write the contents of a buffer to a file
     *
     *  @param file     File handle
     *  @param buffer   The buffer to write from
     *  @param size     The number of bytes to write
     *  @return         The number of bytes written, negative error on failure
     */
    ssize_t file_write(fs_file_t file, const void *buffer, size_t size);

    /** Flush any buffers associated with the file
     *
     *  @param file     File handle
     *  @return         0 on success, negative error code on failure
     */
    int file_sync(fs_file_t file);

    /** Move the file position to a given offset from from a given location
     *
     *  @param file     File handle
     *  @param offset   The offset from whence to move to
     *  @param whence   The start of where to seek
     *      SEEK_SET to start from beginning of file,
     *      SEEK_CUR to start from current position in file,
     *      SEEK_END to start from end of file
     *  @return         The new offset of the file
     */
    off_t file_seek(fs_file_t file, off_t offset, int whence);

    /** Get the file position of the file
     *
     *  @param file     File handle
     *  @return         The current offset in the file
     */
    off_t file_tell(fs_file_t file);

    /** Get the size of the file
     *
     *  @param file     File handle
     *  @return         Size of the file in bytes
     */
    off_t file_size(fs_file_t file);

    /** Open a directory on the filesystem
     *
     *  @param dir      Destination for the handle to the directory
     *  @param path     Name of the directory to open
     *  @return         0 on success, negative error code on failure
     */
    int dir_open(fs_dir_t *dir, const char *path);

    /** Close a directory
     *
     *  @param dir      Dir handle
     *  return          0 on success, negative error code on failure
     */
    int dir_close(fs_dir_t dir);

    /** Read the next directory entry
     *
     *  @param dir      Dir handle
     *  @param ent      The directory entry to fill out
     *  @return         1 on reading a filename, 0 at end of directory, negative error on failure
     */
    ssize_t dir_read(fs_dir_t dir, struct lfs_dirent *ent);

    /** Set the current position of the directory
     *
     *  @param dir      Dir handle
     *  @param offset   Offset of the location to seek to,
     *                  must be a value returned from dir_tell
     */
    void dir_seek(fs_dir_t dir, off_t offset);

    /** Get the current position of the directory
     *
     *  @param dir      Dir handle
     *  @return         Position of the directory that can be passed to dir_rewind
     */
    off_t dir_tell(fs_dir_t dir);

    /** Rewind the current position to the beginning of the directory
     *
     *  @param dir      Dir handle
     */
    void dir_rewind(fs_dir_t dir);

   private:
    const char *const _name;
    lfs_t _lfs;  // _the actual filesystem
    struct lfs_config _config;
    BlockDevice *_bd;  // the block device

    // default parameters
    const lfs_size_t _read_size;
    const lfs_size_t _prog_size;
    const lfs_size_t _block_size;
    const lfs_size_t _lookahead;

    // thread-safe locking
    SemaphoreHandle_t _mutex;
};

#endif
