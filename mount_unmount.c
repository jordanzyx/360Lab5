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
        printf("Mount %d name %s\n",i,mounts[i].devName);
        //Skip over mounts that aren't there
        if(mounts[i].dev == 0)continue;

        //if name matches we have found that the fs is already mounted
        printf("We found that %s was mounted with device # %d on iteration %d\n",fileSystem,mounts[i].dev,i);
        if(strcmp(fileSystem,mounts[i].devName) == 0)return 0;
    }

    //The file system is not mounted
    return -1;
}

int getDevice(char *fileSystem){
    //Loop through all potential mounts and check if any of their device names is equal to our filesystem name
    for (int i = 0; i < NMOUNT; ++i) {
        //Skip over mounts that aren't there
        if(mounts[i].dev == 0)continue;

        //Return the device number
        if(strcmp(fileSystem,mounts[i].devName) == 0)return mounts[i].dev;
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

int func_mounts(){
    printf("===== MOUNTS =====\n");
    for (int i = 0; i < NMOUNT; ++i) {
        //Get the mount we are currently looking at in our iteration
        printf("%s mounted at %s, device num: %d iblock: %d imap %d bmap %d\n",mounts[i].devName,mounts[i].mntPath,mounts[i].dev,mounts[i].iblock,mounts[i].imap,mounts[i].bmap);
    }
}


int func_mount(char *fileSystem, char *mount_path){
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

    //Grab the slot we are mounting into
    MOUNT *mount = &mounts[mountIndex];

    //Write to the mount the names
    mount->dev = 0;
    strcpy(mount->devName, fileSystem);
    strcpy(mount->mntPath, mount_path);

    //Open using linux open so that we can get files from outside our shell. The FD will be the device number
    int fd = open(fileSystem, O_RDWR);

    //Validate that we were able to open the file
    if (fd < 0) {
        printf("Error opening %s for mounting\n", fileSystem);
        return -1;
    }

    //Create a buffer to read in the super block from this file
    char buf[BLKSIZE];

    //Read in the super block and cast the buffer to the super
    get_block(fd, 1, buf);
    SUPER *sp = (SUPER *)buf;

    //Confirm that we are looking at a EXT2 system
    if (sp->s_magic != EXT2_MAGIC) {
        printf("Error: magic value: %d which is not an ext2 value\n", sp->s_magic);
        return -1;
    }

    //Get the ino for the mounting point and get the memory inode for it
    int ino = getino(mount_path);
    MINODE *mip = iget(running->cwd->dev, ino);

    //Confirm that this node is a directory
    if (!S_ISDIR(mip->INODE.i_mode)) {
        printf("Error: %s is not a directory, aborting mount...\n", mount_path);
        return -1;
    }

    //Confirm that the directory is not busy as someones cwd
    if (mip->refCount > 2) {
        printf("Error: refcount > 2 this directory is someones cwd and is busy\n");
        return -1;
    }

    //Set up the device number
    mount->dev = fd;

    //Store the inode counts
    mount->numINodes = sp->s_inodes_count;
    mount->numBlocks = sp->s_blocks_count;

    //Read in the group descriptor block
    get_block(fd, 2, buf);
    gp = (GD *)buf;

    //Set up the bit map for blocks and inodes and set up the starting point
    mount->bmap = gp->bg_block_bitmap;
    mount->imap = gp->bg_inode_bitmap;
    mount->iblock = gp->bg_inode_table;
    
    //Confirm that the memory inode is marked as mounted and set up its mount pointer to this new mount
    mip->mounted = 1;
    mip->mptr = mount;

    //Set up the mount point on our mount to point to this memory inode
    mount->mountPoint = mip;

    //Put back the memory inode
    iput(mip);

    //Inform that the mount worked
    printf("mounting %s @ dir %s is complete\n", mount->devName, mount->mntPath);
    return 0;
}

int func_umount(char *fileSystem){

    //Confirm tat the file system is mounted
    if(mount_exists(fileSystem) != 0){
        printf("filesystem %s is not mounted, cannot umount\n", fileSystem);
        return -1;
    }

    //Get the device number for the mount
    int devNum = getDevice(fileSystem);

    //Get the mount
    MOUNT *mount = getMount(devNum);


    //Look through all MINODE's and see if any of the device numbers are equatl to this device
    for (int i = 0; i < NMINODE; i++) {

        //Check if this node's device is our current device
        if (minode[i].dev == devNum) {
            printf("unmounting failed: there are still active files\n");
            return -1;
        }
    }


    //Get the mount point for this mount
    MINODE *mip = mount->mountPoint;

    //Unmount the memory inode
    mip->mounted = 0;
    mip->mptr = NULL;

    //Push back
    mip->dirty = 1;
    iput(mip);

    return 0;
}