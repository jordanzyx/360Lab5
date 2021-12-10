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
 * Removes a directory from our filesystem
 *
 * @param path to the directory
 * @return 0 success, -1 failure
 */
int func_rmdir(char *path){
    //Find the ino for the directory we are trying to remove
    int ino = getino(path);

    //Confirm existence of item @ pathname
    if (ino == -1)
    {
        printf("ERROR: ino doesn't exist\n");
        return -1;
    }

    //Get the memory inode for the item in question
    MINODE *mip = iget(dev, ino);

    //Confirm that we have a directory on our hands
    if (!S_ISDIR(mip->INODE.i_mode))
    {
        printf("ERROR: %s is not a directory\n",path);
        return -1;
    }

    //Confirm the directory is empty
    if (dir_empty(mip) == -1)
    {
        printf("Cannot remove %s as it is not empty\n",path);
        return -1;
    }

    //Confirm that it is not being used by other processes
    if (mip->refCount > 2)
    {
        printf("ERROR: %s is in use, the reference count > 2, refcount = %d\n", path, mip->refCount);
        return -1;
    }

    //Check permissions
    if (func_access(path,'w') != 1){
        printf("Error: You do not have permission to delete this directory\n");
        return -1;
    }

    //Loop through blocks & deallocate space for them
    for (int i = 0; i < 12; i++)
    {
        //if the block is empty go to the next block
        if (mip->INODE.i_block[i] == 0)continue;

        //deallocate the block
        bdealloc(mip->dev, mip->INODE.i_block[i]);
    }

    //Deallocate the ino for the directory
    idealloc(mip->dev, mip->ino);

    //Mark as dirty & write to block the changes made
    mip->dirty = 1;
    iput(mip);


    //Create two strings to store the path in because they will be destroyed by dirname and basename
    char pathStr1[256], pathStr2[256];
    strcpy(pathStr1, path);
    strcpy(pathStr2, path);

    //Destroy the strings
    char *parent = dirname(pathStr1);
    char *child = basename(pathStr2);

    //Get the ino for the parent so we can remove mip from them
    int parentINO = getino(parent);

    if (parentINO >= 0){
        //Get the parent memory node
        MINODE *pip = iget(mip->dev, parentINO);

        //Remove the directory we just got rid of from its parent
        rm_child(pip, child);

        //Decrease links since this was a directory we removed
        pip->INODE.i_links_count--;

        //Change times to NOW
        pip->INODE.i_atime = time(0L);
        pip->INODE.i_ctime = time(0L);

        //Mark as dirty and write updates to disk
        pip->dirty = 1;
        iput(pip);

        //Return success
        return 0;
    } else {
        printf("Error: Could not find parent inode for %s \n",path);
        return -1;
    }

    return 0;
}

/**
 *
 * @param parent
 * @param name
 * @return
 */
int rm_child(MINODE *parent, char *name) {
    //Get the INODE for the parent
    INODE *ip = &parent->INODE;

    //Loop through all blocks of memory in the parent inode
    for (int i = 0; i < 12; i++){

        //If the block actually has data
        if (ip->i_block[i] != 0) {

            //Create buffer to read in block data too
            char buf[BLKSIZE];

            //Read in the data from the block into our buffer
            get_block(parent->dev, ip->i_block[i], buf); // get block from file

            //Set up the current entry and the current position in the buffer
            DIR *dp = (DIR *) buf;
            char *cp = buf;

            //Create values that store the previous and last entries
            DIR *prevdp;

            //Loop over the block until we hit the end
            while (cp < buf + BLKSIZE){

                //Create a temporary string to store the name of the entry
                char temp[256];

                //Copy the name of the string and make sure there is a null value at the end
                strncpy(temp, dp->name, dp->name_len);
                temp[dp->name_len] = 0;

                //If this is the name we are looking for
                if (!strcmp(temp, name)){
                    //Handle when we are at the first record in the buffer
                    if (cp == buf && cp + dp->rec_len == buf + BLKSIZE) // first/only record
                    {
                        //Deallocate the current blk because it will now be empty and we dont need it
                        bdealloc(parent->dev, ip->i_block[i]);

                        //Decrease the size of the inode by the block we just delete so 1 BLKSIZE
                        ip->i_size -= BLKSIZE;

                        // filling hole in the i_blocks since we deallocated this one
                        while (ip->i_block[i + 1] != 0 && i + 1 < 12){
                            //Increase to next block
                            i++;

                            //Move each block down 1 starting
                            get_block(parent->dev, ip->i_block[i], buf);
                            put_block(parent->dev, ip->i_block[i - 1], buf);
                        }
                    }

                        //We are currently viewing the last entry in the block so we just make the size of the item we are removing go into that and increase it
                    else if (cp + dp->rec_len == buf + BLKSIZE)
                    {
                        //Add our record length to the previous records length so we get absorbed
                        prevdp->rec_len += dp->rec_len;

                        //Write to disk
                        put_block(parent->dev, ip->i_block[i], buf);
                    }

                        //Handle when we are inbetween the first and last entry on the block
                    else{

                        //Store the last entry and the last entry in the file
                        DIR *lastdp = (DIR *) buf;
                        char *lastcp = buf;

                        //Find the last entry in the block
                        while (lastcp + lastdp->rec_len < buf + BLKSIZE)
                        {
                            //Move our variables to this position
                            lastcp += lastdp->rec_len;
                            lastdp = (DIR *) lastcp;
                        }

                        //Increase the amount of size available
                        lastdp->rec_len += dp->rec_len;

                        //Find the point of the starting entry and the end of this block
                        char *start = cp + dp->rec_len;
                        char *end = buf + BLKSIZE;

                        //Shift everything left so that we have free space properly at the end of the block
                        memmove(cp, start, end - start);

                        //Write to disk
                        put_block(parent->dev, ip->i_block[i], buf);
                    }

                    //Update the parent node
                    parent->dirty = 1;
                    iput(parent);
                    return 0;
                }

                //Store previous entry
                prevdp = dp;

                //Advance current position by entry length
                cp += dp->rec_len;

                //Cast to next entry
                dp = (DIR *) cp;
            }
        }
    }

    //Error out
    printf("ERROR: could not find child\n");
    return -1;
}
