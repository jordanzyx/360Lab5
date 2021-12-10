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
 * Creates a hard link between two files
 *
 * @param old path to the file we are making a hard link to
 * @param new path of the file containing the hard link
 * @return
 */
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
    if(newIno != -1){
        printf("Error: %s already exists\n",new);
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

/**
 *
 * Unlinks & removes a file from our filesystem
 *
 * @param path to the file
 * @return -1 on failure
 */
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

    //Check permissions
    if (func_access(path,'w') != 1){
        printf("Error: You do not have permission to unlink this file\n");
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

