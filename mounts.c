#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <ext2fs/ext2_fs.h>
#include <string.h>
#include <libgen.h>
#include <sys/stat.h>
#include "unistd.h"
#include <time.h>

#include "func.h"
#include "util.h"

extern MINODE minode[NMINODE];
extern MINODE *root;
extern PROC   proc[NPROC], *running;

extern char gpath[128];
extern char *name[64];
extern int n;

extern int fd, dev;
extern int nblocks, ninodes, bmap, imap, iblk;
extern MOUNT mounts[NMOUNT];

extern char line[128], cmd[32], pathname[128];

/**
 * Switch the process from P0 to P1 or from P1 to P0
 * @return 0 for success
 */
int func_switch(){
    // switch processes from P0 to P1
    // 2 processed system, circular linked list, so simply go to next node in list
    running = running->next;
    running->cwd = iget(dev, 2);
    printf("switched to PID %d\n", running->pid);
    return 0;
}

/**
 * Lets us know if a specific fs is mounted
 * @param fileSystem path to filesystem we want to mount
 * @return 0 if mounted, -1 if not mounted
 */
int mount_exists(char *fileSystem){
    //Loop through all potential mounts and check if any of their device names is equal to our filesystem name
    for (int i = 0; i < NMOUNT; ++i) {
        //Skip over mounts that aren't there
        if(mounts[i].dev == 0)continue;

        //if name matches we have found that the fs is already mounted
        if(strcmp(fileSystem,mounts[i].devName) == 0)return 0;
    }

    //The file system is not mounted
    return -1;
}

/**
 * Searches through the mounts & attempts to find an empty slot to use for mounting
 * @return index if a spot was found, -1 if the mounts table is full
 */
int findFreeMountSlot(){
    // Search through mounts & return first free slot we find
    for (int i = 0; i < NMOUNT; i++) {
        if (mounts[i].dev == 0)return i;
    }
    //No slot was found
    return -1;
}

int func_mount(char *fileSystem, char *mount_path){
    int ino;
    MINODE *mip;
    MOUNT *mount;

    //Exit if this filesystem is already mounted
    if(mount_exists(fileSystem) == 0){
        printf("Error: %s is already mounted \n",fileSystem);
        return -1;
    }

    //Find a slot to mount into & if no spot is found error out & exit function
    int mountIndex = findFreeMountSlot();
    if (mountIndex == -1) {
        printf("Error: mounts table is full\n");
        return -1;
    }

    // Grab the slot we are mounting into
    mount = &mounts[mountIndex];

    //Write to the mount
    strcpy(mount->devName, fileSystem);
    strcpy(mount->mntPath, mount_path);

    // open filesystem for RW, use FD # as new dev
    // check whether ext2 filesystem or not; if not reject
    //  |
    //  |-> read superblock, check if s_magic is 0xEF53
    printf("opening file %s for mount\n", fileSystem);
    int fd = open(fileSystem, O_RDWR);
    if (fd < 0) {
        printf("invalid fd for filesys: error opening file %s\n", fileSystem);
        return -1;
    }

    char buf[BLKSIZE];
    get_block(fd, 1, buf);
    SUPER *sp = (SUPER *)buf;

    if (sp->s_magic != EXT2_MAGIC) {
        printf("error, magic = %d is not an ext2 filesystem\n", sp->s_magic);
        return -1;
    }

    // mount_point, get ino and then minode
    ino = getino(mount_path);
    mip = iget(running->cwd->dev, ino);

    // check m_p is a dir and is not busy (not someone else's CWD)
    if (!S_ISDIR(mip->INODE.i_mode)) {
        printf("error, %s is not a directory, cannot be mounted\n", mount_path);
        return -1;
    }

    if (mip->refCount > 2) {
        printf("cannot mount: directory is busy (refcount > 2)\n");
        return -1;
    }

    // record new dev in mount table entry (fd is new DEV)
    mount->dev = fd;
    // for convenience, mark other information as well (TODO)
    mount->numINodes = sp->s_inodes_count;
    mount->numBlocks = sp->s_blocks_count;
    get_block(dev, 2, buf);
    gp = (GD *)buf;

    mount->bmap = gp->bg_block_bitmap;
    mount->imap = gp->bg_inode_bitmap;
    mount->iblock = gp->bg_inode_table;

    // mark mount_point's minode as being mounted on and let it point at the MOUNT table entry, which points back to the
    // m_p minode
    mip->mounted = 1;
    mip->mptr = mount;
    mount->mountPoint = mip;

    printf("my_mount: mounted disk %s onto directory %s successfully\n", fileSystem, mount_path);
    return 0;
}

int func_umount(char *fileSystem){
    // 1. Search the MOUNT table to check filesys is indeed mounted.
    int is_mounted = 0;
    int mounted_dev;
    MOUNT *mtptr = NULL;
    for (int i = 0; i < NMOUNT; i++) {
        if (!strcmp(mounts[i].devName, fileSystem) && mounts[i].dev != 0) {
            is_mounted = 1;
            mounted_dev = mounts[i].dev;
            mtptr = &mounts[i];
            break;
        }
    }

    if (!is_mounted) {
        printf("filesystem %s is not mounted, cannot umount\n", fileSystem);
        return -1;
    }

    // 2. Check whether any file is still active in the mounted filesys;
    // HOW to check?      ANS: by checking all minode[].dev

    for (int i = 0; i < NMINODE; i++) {
        if (minode[i].dev == mounted_dev) {
            printf("cannot umount filesystem, active file detected\n");
            return -1;
        }
    }

    // 3. Find the mount_point's inode (which should be in memory while it's mounted on).  Reset it to "not mounted"; then
    // iput()   the minode.  (because it was iget()ed during mounting)

    MINODE *mip = mtptr->mountPoint;

    //int ino = getino(mtptr->mntName);
    mip->mounted = 0;
    iput(mip);

    return 0;
}