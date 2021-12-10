/****************************************************************************
*                   KCW: mount root file system                             *
*****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <ext2fs/ext2_fs.h>
#include <string.h>
#include <libgen.h>
#include <sys/stat.h>
#include <time.h>

#include "util.h"
#include "type.h"
#include "func.h"

extern MINODE *iget();

MINODE minode[NMINODE];
MINODE *root;
PROC proc[NPROC], *running;

char gpath[128]; // global for tokenized components
char *name[64];  // assume at most 64 components in pathname
int n;         // number of component strings

//Mount Information
int fd, dev;
int nblocks, ninodes, bmap, imap, iblk;
char line[128], cmd[32], pathname[128], pathname2[128];

//Mounts
MOUNT mounts[NMOUNT];


int init() {
    int i, j;
    MINODE *mip;
    PROC *p;
    MOUNT *mountP;

    printf("init()\n");

    for (i = 0; i < NMINODE; i++) {
        mip = &minode[i];
        mip->dev = mip->ino = 0;
        mip->refCount = 0;
        mip->mounted = 0;
        mip->mptr = 0;
    }
    for (i = 0; i < NPROC; i++) {
        p = &proc[i];
        p->pid = i;
        p->uid = p->gid = 0;
        p->cwd = 0;

        for (int k = 0; k < NFD; ++k) {
            p->fd[k] = 0;
        }

    }
    for (int i = 0; i < NMOUNT; ++i) {
        mountP = &mounts[i];
        mountP->dev = 0;
    }

    root = 0;
}

// load root INODE and set root pointer to it
int mount_root() {
    printf("mount_root()\n");
    root = iget(dev, 2);
    mounts[0].mountPoint = root;
}


int quit() {
    int i;
    MINODE *mip;
    for (i = 0; i < NMINODE; i++) {
        mip = &minode[i];
        if (mip->refCount > 0)
            iput(mip);
    }
    exit(0);
}

int main(int argc, char *argv[]) {
    //Create a buffer to read in the information for the starting disk
    char buf[BLKSIZE];

    //Exit out if we did not get any input
    if(argc == 0)return 1;

    //Get the disk
    char *disk = argv[1];

    //Confirm that we can open this disk
    if ((fd = open(disk, O_RDWR)) < 0) {
        printf("open %s failed\n", disk);
        exit(1);
    }

    //Set up the global device number
    dev = fd;

    //Set up the device number to equal the file descriptor for the disk we opened
    mounts[0].dev = fd;

    //Get the super block information
    get_block(dev, 1, buf);
    sp = (SUPER *) buf;

    //Confirm we are using ext2
    if (sp->s_magic != 0xEF53) {
        printf("magic = %x is not an ext2 filesystem\n", sp->s_magic);
        exit(1);
    }

    //Set up the num inodes
    mounts[0].numINodes = sp->s_inodes_count;
    mounts[0].numBlocks = sp->s_blocks_count;

    //Get the group descriptor
    get_block(dev, 2, buf);
    gp = (GD *) buf;

    //Setup the starting iblock and bitmaps for inodes and blocks
    mounts[0].bmap = gp->bg_block_bitmap;
    mounts[0].imap = gp->bg_inode_bitmap;
    mounts[0].iblock = gp->bg_inode_table;

    //Copy the name to the mount object
    strcpy(mounts[0].devName, disk);

    //Initialize the globals we need
    init();

    //Mount the rood and make sure once again that its device number is valid
    mounts[0].dev = fd;
    mount_root();

    //Set up the running process
    running = &proc[0];
    running->status = READY;
    running->cwd = iget(dev, 2);

    //Set up the circular process loop
    proc[1].uid = 1;
    proc[0].next = &proc[1];
    proc[1].next = &proc[0];


    //Command processing loop
    while (1) {
        printf("input command : [ls|cd|pwd|mkdir|rmdir|creat|link|unlink|symlink|readlink|utime|open|write|pfd|cat|close|cp|switch|mount|mounts|quit] ");
        fgets(line, 128, stdin);
        line[strlen(line) - 1] = 0;


        if (line[0] == 0)
            continue;
        pathname[0] = 0;

        sscanf(line, "%s %s", cmd, pathname);
        printf("cmd=%s pathname=%s\n", cmd, pathname);

        //List files
        if (strcmp(cmd, "ls") == 0)ls(pathname);

        //Change directory
        else if (strcmp(cmd, "cd") == 0)cd(pathname);

        //Print working directory
        else if (strcmp(cmd, "pwd") == 0)pwd(running->cwd);

        //Make directory
        else if (strcmp(cmd, "mkdir") == 0)func_mkdir(pathname);

        //Remove directory
        else if (strcmp(cmd, "rmdir") == 0)func_rmdir(pathname);

        //Create file
        else if (strcmp(cmd, "creat") == 0)func_creat(pathname);

        //Create a hard link
        else if (strcmp(cmd, "link") == 0) {
            sscanf(line, "%s %s %s", cmd, pathname, pathname2);
            func_link(pathname, pathname2);
        }

        //Unlink a file
        else if (strcmp(cmd, "unlink") == 0) {
            func_unlink(pathname);
        }

        //Create a symbolic link
        else if (strcmp(cmd, "symlink") == 0) {
            sscanf(line, "%s %s %s", cmd, pathname, pathname2);
            func_symlink(pathname, pathname2);
        }

        //Read a link
        else if (strcmp(cmd, "readlink") == 0) {
            func_readlink(pathname);
        }

        //Adjust access time for a file to now
        else if (strcmp(cmd, "utime") == 0)func_utime(pathname);

        //Open a file
        else if (strcmp(cmd, "open") == 0) {
            int mode = -1;
            sscanf(line, "%s %s %d", cmd, pathname, &mode);
            func_open(pathname, mode);
        }

        //Write to an open file
        else if (strcmp(cmd, "write") == 0) {
            func_write_cmd();
        }

        //Print file descriptors
        else if (strcmp(cmd, "pfd") == 0)func_pfd();

        //Cat (view file)
        else if (strcmp(cmd, "cat") == 0)func_cat(pathname);

        //Close an open file
        else if (strcmp(cmd, "close") == 0) {
            int fd = -1;
            sscanf(line, "%s %d", cmd, &fd);
            func_close(fd);
        }

        //Copy files
        else if (strcmp(cmd, "cp") == 0) {
            sscanf(line, "%s %s %s", cmd, pathname, pathname2);
            func_cp(pathname, pathname2);
        }

        //Quit the shell
        else if (strcmp(cmd, "quit") == 0) {
            quit();
            break;
        }

        //Switch users
        else if (strcmp(cmd, "switch") == 0)func_switch();

        //Display mounts
        else if (strcmp(cmd, "mounts") == 0)func_mounts();

        //Attempt to mount a disk
        else if (strcmp(cmd, "mount") == 0) {
            sscanf(line, "%s %s %s", cmd, pathname, pathname2);
            func_mount(pathname, pathname2);
        }

        //Unmount a device
        else if (strcmp(cmd,"umount") == 0)func_umount(pathname);

    }
}


