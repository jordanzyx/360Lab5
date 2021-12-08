//
// Created by Jordan Muehlbauer on 11/3/21.
//
#include <sys/stat.h>
#include <libgen.h>
#include <sys/time.h>
#include <ext2fs/ext2_fs.h>
#include <stdlib.h>
#include <time.h>
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
    int ino = getino(path);

    //Create a new file if we do not have one
    if (ino == -1) {
        // Make sure that the parent exists
        char parent[256], buf[256];

        //Copy path into buffer since dirname destroys strings
        strcpy(buf, path);
        strcpy(parent, dirname(buf));

        //Make sure the parent exists now
        int pino = getino(parent);
        if (pino == -1) {
            printf("Error: Could not find parent for %s\n",path);
            return -1;
        }

        //Create the file
        func_creat(path);

        //Confirm the file was created
        ino = getino(path);
        if (ino == -1) {
            // if ino still failed, we probably have bigger problems
            printf("\n");
            return -1;
        }
    }

    //Get the memory INODE so we can validate other information about this file
    printf("Opening file %s with ino %d\n",path,ino);
    func_pfd();
    MINODE *mip = iget(dev, ino);
    printf("Confirming %d %d\n",ino,mip->ino);

    //Confirm that the file is a regular file
    if (!S_ISREG(mip->INODE.i_mode)) {
        printf("Error Opening: File is not regular\n");
        return -1;
    }

    // Check the other open files and make sure that there is not another instance of this file opened that is not strictly READ
    for (int i = 0; i < NFD; i++) {
        //Skip over OFT we are viewing if it is null
        if (running->fd[i] == NULL)continue;
        //Skip if its Memory INODE is not identical
        if (running->fd[i]->mptr != mip)continue;
        //Skip if it is in read mode
        if (running->fd[i]->mode == 0)continue;

        //Cancel this operation because we have conflicting modes
        printf("Error opening %s because there is already and instance of this file open with mode %d\n",path,running->fd[i]->mode);
    }

    //Create a new OFT to store in our table
    OFT *oftp = (OFT *)malloc(sizeof(OFT));

    //Prepare the mode & basic information for this OFT
    oftp->mode = mode;
    oftp->refCount = 1;
    oftp->mptr = mip;

    //Free inodes if we are using WRITE because we are completely writing over this file
    if(mode == WRITE)freeINodes(mip);

    //Adjust offset based on mode
    if(mode == READ || mode == WRITE || mode == READ_WRITE)oftp->offset = 0;
    if(mode == APPEND)oftp->offset = mip->INODE.i_size;

    int fdGiven = -1;

    //Find the first open FD slot
    for (int i = 0; i < NFD; i++) {
        //Skip fd slots that are taken
        if(running->fd[i] != 0)continue;
        printf("==== ON %d ====\n\n",i);
        func_pfd();

        //Store on the process
        running->fd[i] = oftp;

        //Store the fd
        fdGiven = i;

        //Exit out of loop
        break;
    }

    //Adjust access time
    mip->INODE.i_atime = time(0L);

    //Adjust modify time if we are not looking at READ
    if (mode != READ)mip->INODE.i_mtime = time(0L);

    //Mark as dirty and write to disk
    mip->dirty = 1;
    iput(mip);

    printf("Opened %s with fd: %d\n",path,fdGiven);

    //Look at me!
    func_pfd();

    return fdGiven;
}

int func_close(int fd){
    //Make sure that the file descriptor given is within range
    if( !(fd >= 0 && fd < NFD)){
        printf("Error closing: Invalid file descriptor \n");
        return -1;
    }

    // Confirm there is an OFT & the fd supplied
    if (running->fd[fd] == NULL) {
        printf("Error closing: No OFT found @ fd %d\n",fd);
        return -1;
    }

    //Grab OFT & index
    OFT *oftp = running->fd[fd];

    //Clear it from the table
    running->fd[fd] = 0;

    //Decrease references
    oftp->refCount--;

    //If the references are over 0 exit
    if (oftp->refCount > 0)return 0;

    //Push the changes back to disk
    MINODE *mip = oftp->mptr;
    mip->dirty = 1;
    iput(mip);

    //Destroy the pointer we allocated for this information
    free(oftp);

    return 0;
}

int func_pfd(){
    //Loop through the open files and print out their information
    printf("-- Open File Descriptors --\n");
    for (int i = 0; i < NFD; ++i) {
        //Skip if the slot is empty
        if(running->fd[i] == NULL)continue;

        //Get the oft
        OFT* oft = running->fd[i];

        //Create a string for mode
        char* mode = "";
        if(oft->mode == READ)mode = "Read";
        if(oft->mode == WRITE)mode = "Write";
        if(oft->mode == READ_WRITE)mode = "Read_Write";
        if(oft->mode == APPEND)mode = "Append";


        //Print info for the file descriptor
        printf("fd: %d, mode: %s  ino: %d\n",i, mode,running->fd[i]->mptr->ino);
    }

    return -1;
}

int func_lseek(int fd, int pos){

}
