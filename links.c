#include <sys/stat.h>
#include "stdio.h"
#include "string.h"
#include "libgen.h"
#include <ext2fs/ext2_fs.h>

#include "func.h"
#include "util.h"

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

int validate(char *item,int *ino){
    //Get the ino for the item
    *ino = getino(item);

    //Confirm if its valid
    if(*ino == -1){
        printf("Error: %s does not exist\n",item);
        return -1;
    }

    return 0;
}

int func_link(char *old, char *new){
    //Set up the device to be accurate for what we are dealing with
    if (old[0] == '/')dev = root->dev;
    else dev = running->cwd->dev;

    //Get the inode of the old
    int oldInode = getino(old);
    if(oldInode == -1){
        printf("Error: %s does not exist\n",old);
        return -1;
    }

    //Find the Memory Inode for the old
    MINODE *mip = iget(dev, oldInode);

    //Confirm we do not have a directory on our hands
    if (S_ISDIR(mip->INODE.i_mode)) {
        printf("Error: Cannot link a directory\n");
        return -1;
    }

    //Store the device we used for the old
    int olddev = dev;

    //Set up the device for getting new properly
    if (new[0] == '/')dev = root->dev;
    else dev = running->cwd->dev;

    //Confirm the devices are the same
    if (olddev != dev) {
        printf("Error: You cannot link two files from different devices\n");
        return -1;
    }

    //Get the ino for the new file
    int newIno = getino(new);
    if(newIno == -1){
        printf("Error: %s does not exist\n",new);
        return -1;
    }

    // Look for that directory newname exists but the file does not exist yet in the directory
    char parent[256], child[256];
    strcpy(child, basename(new));
    strcpy(parent, dirname(new));

    // Get the inode of the parent so we can confirm it is real
    int parentIno = getino(parent);

    // Make sure the parent dir exists
    if (parentIno == -1) {
        printf("can't create link in parent dir %s\n", parent);
        return -1;
    }

    //Get the parents memory inode
    MINODE *mip_new = iget(dev, parentIno);

    //Add the name link into the parent
    enter_name(mip_new, mip->ino, child);

    //Increase the amount of links for the old
    mip->INODE.i_links_count++;
    mip->dirty = 1;
    mip_new->dirty = 1;

    //Write to disk
    iput(mip);
    iput(mip_new);
}


int func_unlink(char *path){
    //Set up device properly for the path
    dev = path[0] == '/' ? root->dev : running->cwd->dev;

    //Get the parent & child names
    char parent[256], child[256];
    strcpy(parent, dirname(path));
    strcpy(child, basename(path));

    //Get the inode for the path supplied & confirm it is valid
    int inode = getino(path);
    if (inode == -1){
        printf("Error: %s does not exist\n",path);
        return -1;
    }

    //Get the Memory Inode
    MINODE *mip = iget(dev, inode);

    //Confirm the item inquestion is not a directory
    if (S_ISDIR(mip->INODE.i_mode)) {
        printf("Directory cannot be a link therefore we cannot unlink %s\n", path);
        return -1;
    }

    //Drop the link count down
    mip->INODE.i_links_count--;

    //Check if the file is not a link
    if(!S_ISLNK(mip->INODE.i_mode)){
        //If the links count == 0 then free all nodes
        if(mip->INODE.i_links_count == 0){
            freeINodes(mip);
        }
    }

    //Set the memory inode to dirty and write to disk
    mip->dirty = 1;
    iput(mip);

    //Get the parent so we can remove the child from the parent
    int parentIno = getino(parent);
    if (parentIno >= 0) {
        //Get parent memory inode
        MINODE *pip = iget(mip->dev, parentIno);

        //Remove the child
        rm_child(pip, child);
    }  else {
        printf("Could not find parent for %s\n",path);
        return -1;
    }
}


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
 * (1). get file’s INODE in memory; verify it’s a LNK file
 * (2). copy target filename from INODE.i_block[ ] into buffer;
 * (3). return file size;
 *
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