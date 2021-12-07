#ifndef TYPE_H
#define TYPE_H

/*************** type.h file for LEVEL-1 ****************/
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

typedef struct ext2_super_block SUPER;
typedef struct ext2_group_desc  GD;
typedef struct ext2_inode       INODE;
typedef struct ext2_dir_entry_2 DIR;

SUPER *sp;
GD    *gp;
INODE *ip;
DIR   *dp;   

#define FREE        0
#define READY       1

#define BLKSIZE  1024
#define NMINODE   128
#define NPROC       2
#define NFD        16
#define NMOUNT     16

#define DIR_MODE 0x41ED
#define FILE_MODE 0x81A4
#define EXT2_MAGIC 0xEF53

//MODES FOR FILES WHEN WE OPEN
#define READ 0
#define WRITE 1
#define READ_WRITE 2
#define APPEND 3

#define SUPER_USER 0

typedef struct minode{
  INODE INODE;           // INODE structure on disk
  int dev, ino;          // (dev, ino) of INODE
  int refCount;          // in use count
  int dirty;             // 0 for clean, 1 for modified

  int mounted;           // for level-3
  struct mntable *mptr;  // for level-3
}MINODE;

// Open file table
typedef struct oft
{
    int mode;     // mode of opened file
    int refCount; // number of PROCs sharing this instance
    MINODE *mptr; // pointer to minode of file
    int offset;   // byte offset for R|W
} OFT;

typedef struct proc{
  struct proc *next;
  int          pid;      // process ID  
  int          uid;      // user ID
  int          gid;
  int          status;
  MINODE      *cwd;      // CWD directory pointer
  OFT *fd[NFD];          // list of open files
}PROC;

typedef struct mount{
    //Device number, note: 0 if free
    int dev;

    //Super block & group descriptor info
    int numINodes;
    int numBlocks;
    int freeINodes;
    int freeBlocks;
    int bmap;
    int imap;

    //Starting block for INodes
    int iblock;

    //Name of device & name of mounted directory
    char mntPath[256];
    char devName[256];

    //MINODE to mount point
    MINODE *mountPoint;
}MOUNT;

#endif