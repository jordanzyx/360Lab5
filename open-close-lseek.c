//
// Created by Jordan Muehlbauer on 11/3/21.
//
#include <sys/stat.h>
#include <libgen.h>
#include <sys/time.h>
#include <ext2fs/ext2_fs.h>
#include <stdlib.h>
#include "string.h"
#include "func.h"
#include "util.h"
#include "stdio.h"

/**** globals defined in main.c file ****/
extern MINODE minode[NMINODE];
extern MINODE *root;
extern PROC   proc[NPROC], *running;

extern char gpath[128];
extern char *name[64];
extern int n;

extern int fd, dev;
extern int nblocks, ninodes, bmap, imap, iblk;

extern char line[128], cmd[32], pathname[128];


int func_open(char *path, int mode){

    //Validate that the mode for opening is valid
    if (!(mode >= 0 && mode <= 3)) {
        printf("File Opening Error: Invalid Mode\n");
        return -1;
    }

    //Prepare device based on path
    if (path[0] == '/')dev = root->dev;
    else dev = running->cwd->dev;

    //Get the INO for this path and make sure that file is around
    int ino = getino(pathname);

    if (ino == -1) {
        // ino must be created, does not exist

        // find parent ino of file to be created
        char parent[BLKSIZE], buf[BLKSIZE];
        strcpy(buf, pathname);
        strcpy(parent, dirname(buf));
        printf("PARENT: %s\n", parent);
        int pino = getino(parent);
        if (pino == -1) {
            printf("error finding parent inode (open file)\n");
            return -1;
        }
        MINODE *pmip = iget(dev, pino);

        int r = func_creat(pathname);
        ino = getino(pathname);

        if (ino == -1) {
            // if ino still failed, we probably have bigger problems
            printf("error: new ino allocation failed for open\n");
            return -1;
        }
    }


    printf("MIDDLE OPEN: running->cwd->ino, address: %d\t%x\n", running->cwd->ino, running->cwd);

    MINODE *mip = iget(dev, ino);

    printf("debug mode: %d\n", mip->ino);

    if (!S_ISREG(mip->INODE.i_mode)) {
        printf("error: not a regular file\n");
        return -1;
    }


    // go through all open files-- check if anything is open with incompatible mode
    for (int i = 0; i < NFD; i++) {
        if (running->fd[i] == NULL)
            break;
        if (running->fd[i]->mptr == mip) {
            if (mode != 0) {
                printf("error: already open with incompatible mode\n");
                return -1;
            }
        }
    }

    OFT *oftp = (OFT *)malloc(sizeof(OFT));
    oftp->mode = mode;
    oftp->refCount = 1;
    oftp->mptr = mip;

    switch(mode) {
        case 0:                 // read, offset = 0
            oftp->offset = 0;
            break;
        case 1:                 // write, truncate file to 0 size
            inode_truncate(mip);
            oftp->offset = 0;
            break;
        case 2:                 // read/write, don't truncate file
            oftp->offset = 0;
            break;
        case 3:                 // append
            oftp->offset = mip->INODE.i_size;
            break;
        default:                // shouldn't ever get here based on first check, but just in case
            printf("error: invalid mode\n");
            return -1;
    }

    int returned_fd = -1;
    // might be redundant-- same loop earlier, could be refactored to find NULL fd[i] earlier
    for (int i = 0; i < NFD; i++) {
        if (running->fd[i] == NULL) {
            running->fd[i] = oftp;
            returned_fd = i;
            break;
        }
    }

    if (mode != 0) { // not read, mtime
        mip->INODE.i_mtime = time(NULL);
    }
    mip->INODE.i_atime = time(NULL);
    mip->dirty = 1;
    iput(mip);

    return returned_fd;
}

int func_close(int fd){

}

int func_lseek(int fd, int pos){

}