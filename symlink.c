//
// Created by Jordan Muehlbauer on 11/3/21.
//
#include <sys/stat.h>
#include <libgen.h>
#include <sys/time.h>
#include <ext2fs/ext2_fs.h>
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

/**
 *
 * Creates a symbolic link to a entry in our filesystem (NOTE: can cross devices)
 *
 * @param old path of file we are linking too
 * @param new path of the file that is the symbolic link
 * @return nothing
 */
int func_symlink(char *old, char *new){

    // Prepare device for finding old
    if (old[0] == '/')dev = root->dev;
    else dev = running->cwd->dev;

    //Get the ino and confirm its valid
    int oldIno = getino(old);
    if (oldIno == -1) {
        printf("Error: %s doesn't exist\n", old);
        return -1;
    }

    // Prepare device for finding new
    if (new[0] == '/')dev = root->dev;
    else dev = running->cwd->dev;

    // Create the new file
    func_creat(new);

    // Confirm that new was created
    int new_ino = getino(new);
    if (new_ino == -1) {
        printf("Error creating %s\n", new);
        return -1;
    }

    //Get the memory INODE for the created file and set it to be a LNK file type
    MINODE *mip = iget(dev, new_ino);

    //This sets it as LNK file type
    mip->INODE.i_mode = 0xA1FF;

    // i_block[] can hold 60 characters
    // i_block[] + 24 = 84
    //Store the name of old into the new linker file
    char buf[BLKSIZE];

    //Allocate a block to store the information
    int block = balloc(dev);
    mip->INODE.i_block[0] = block;

    //Copy name into buffer
    strcpy(buf,old);

    //WRITE TO DISK
    put_block(dev,block,buf);

    // set /x/y/z file size = number of chars in oldName
    mip->INODE.i_size = strlen(old) + 1; // +1 for '\0'

    // Mark as dirty and write to disk
    mip->dirty = 1;
    iput(mip);
}

/**
 *
 * Prints out to the stdin the link from a LNK file
 *  Algorithm
 * (1). get file’s INODE in memory; verify it’s a LNK file
 * (2). copy target filename from INODE.i_block[ ] into buffer;
 * (3). return file size;
 *
 * @param file path of the file we are reading the link from
 */
int func_readlink(char *file){
    //Properly set up the device
    dev = file[0] == '/' ? root->dev : running->cwd->dev;

    //Get the ino so we can confirm if the file exists & is a LNK file
    int ino = getino(file);
    if(ino == -1){
        printf("Error: No file found %s", file);
        return -1;
    }

    //Get the memory inode for the file in question
    MINODE *mip = iget(dev,ino);

    //Confirm it is a LNK file
    if(!S_ISLNK(mip->INODE.i_mode)){
        printf("Error: file is not a LNK file");
        return -1;
    }

    //Buffer to copy the name in the LNK file too
    char buf[BLKSIZE];
    get_block(dev, mip->INODE.i_block[0], buf);

    //Find the file from the name now
    dev = buf[0] == '/' ? root->dev : running->cwd->dev;

    //Get the info so we can confirm the found name is a valid file
    int lnkino = getino(buf);
    if(lnkino == -1){
        printf("Error: File linked is invalid");
        return -1;
    }

    //Get memory inode for file found
    MINODE *lnkmip = iget(dev,lnkino);

    //Print
    printf("File size: %d\n",lnkmip->INODE.i_size);
}