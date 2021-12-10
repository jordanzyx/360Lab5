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
 * Prepares a INODE for a directory being created in our file system
 *
 * @param ip pointer to the inode we are preparing
 * @param bno block number for the first data block we are giving this directory
 */
void prepareInode(INODE *ip, int bno){
    ip->i_mode = DIR_MODE;       // OR 040755: DIR type and permissions
    ip->i_uid = running->uid;    // Owner uid
    ip->i_gid = running->gid;    // Group Id
    ip->i_size = BLKSIZE;        // Size in bytes
    ip->i_links_count = 2;       // Links count=2 because of . and ..
    ip->i_atime = time(0L);      // set to current time
    ip->i_ctime = time(0L);      // set to current time
    ip->i_mtime = time(0L);      // set to current time
    ip->i_blocks = 2;            // LINUX: Blocks count in 512-byte chunks
    ip->i_block[0] = bno;        // new DIR has one data block
    for (int i = 1; i < 15; i++) // clears all the block memeory
    {
        ip->i_block[i] = 0;
    }
}

/**
 *
 * Creates the '.' entry inside of a directory we just created, used for pointing to itsself
 * This is used in something like './a.out
 *
 * @param dp pointer to the directory
 * @param ino inode of the directory we just made
 */
void prepareSelfEntry(DIR *dp, int ino){
    //We set this to the ino of the directory we just made
    dp->inode = ino;

    //Set record length to 12 because of 4 * [(8 + nameLength + 3) / 4]
    dp->rec_len = 12;

    //Prepare name & name length
    dp->name_len = 1;
    dp->name[0] = '.';
}

/**
 *
 * Prepares a parent entry for an entry we created '..'
 * This is used in something like 'cd ..'
 *
 * @param dp
 * @param parent
 */
void prepareParentEntry(DIR *dp,MINODE* parent){
    //Set to parent ino because thats where the entry goes
    dp->inode = parent->ino;

    //Use the remainder of the block we allocated
    dp->rec_len = BLKSIZE - 12;

    //Set up name & length
    dp->name_len = 2;
    dp->name[0] = '.';
    dp->name[1] = '.';
}

/**
 *
 * Helper function for creating a directory. Saves space within #func_mkdir
 *
 * @param parent this is the Memory INODE of the directory we are putting our new directory in
 * @param dirName name of the directory we are creating
 * @return 0 on success
 */
int mkdir_helper(MINODE* parent, char* dirName){
    //Allocate space for a new inode & block
    int ino = ialloc(dev);
    int bno = balloc(dev);

    //Get the Memory inode for the newly created node
    MINODE *mip = iget(dev, ino);

    //Set up the INode proprly
    INODE *ip = &mip->INODE;
    prepareInode(ip,bno);

    //Mark for updates and write to disk
    mip->dirty = 1;
    iput(mip);

    //Find the block we just allocated
    char *buf[BLKSIZE];
    get_block(dev, bno, buf);

    //Create variables to use while setting up new entries in our directory we are making
    DIR *dp = (DIR *)buf;
    char *cp = buf;

    //Debug
    printf("Setting up . and .. in %s\n",dirName);

    //Create . entry so that we can use ./
    prepareSelfEntry(dp,ino);

    //Advance to end of . entry so we can create .. entry
    cp += dp->rec_len;
    dp = (DIR *)cp;

    //Create .. entry so we can go to parent
    prepareParentEntry(dp,parent);

    //Write to the block the two new entries
    put_block(dev, bno, buf);

    //Put the name of our directory into the parent
    enter_name(parent, ino, dirName);

    return 0;
}

/**
 * Validates that you can create a new entry in our filesystem & a specific path
 *
 * @param pip parent memory inode where we want to create this item
 * @param parent string we will write the parents path too
 * @param child name of the child
 * @param pathname this is where we pass in the path to validate
 * @return 0 if valid, -1 if not valid
 */
int validate_creation_function(MINODE *pip,char *parent,char *child,char *pathname){
    //Store two instances of the name 1 for basename and 1 for dirname
    char pathStr1[256];
    char pathStr2[256];

    //Copy the name over so we can use it as a starting point. We need to do this because basename & dirname destroy strings
    strcpy(pathStr1, pathname);
    strcpy(pathStr2, pathname);

    //Search for a / to start the location this is for handling of absolute/relative paths
    dev = pathname[0] == '/' ? root->dev : running->cwd->dev;

    //Parse the pathname to get valid string names to use for the rest of creating a directory
    parent = dirname(pathStr1);
    child = basename(pathStr2);

    //Find the parent inode so that we can verify the location exists
    int parentIno = getino(parent); // get partent inode numbe

    //Make sure that the parent exists
    if(parentIno >= 0){
        pip = iget(dev, parentIno);

        //Confirm we are looking at a directory & not a file or link
        if (!S_ISDIR(pip->INODE.i_mode))
        {
            printf("ISSUE: cannot create a directory in a non-directory\n", parent);
            return -1;
        }

        //Make sure there is no child with this name
        if (search(pip, child))
        {
            printf("ISSUE: child with name already exists parent: %s child: %s", parent, child);
            return -1;
        }

        return 0;
    } else{
        printf("ISSUE: parent in mkdir does not exists: %s\n",parent);
        return -1;
    }
}

/**
 *
 * Creates a directory based on a pathname in our file system
 *
 * @param pathname of the directory passed in by a command
 * @return 0 success, -1 failure
 */
int func_mkdir(char *pathname){
    //Create variables to be written too in our validation function
    MINODE *pip;
    char *parent;
    char *child;

//    //Validate that we can create something here
//    int r = validate_creation_function(pip,parent,child,pathname);
//    if(r != 0)return -1;

    //Store two instances of the name 1 for basename and 1 for dirname
    char pathStr1[256];
    char pathStr2[256];

    //Copy the name over so we can use it as a starting point. We need to do this because basename & dirname destroy strings
    strcpy(pathStr1, pathname);
    strcpy(pathStr2, pathname);

    //Search for a / to start the location this is for handling of absolute/relative paths
    dev = pathname[0] == '/' ? root->dev : running->cwd->dev;

    //Parse the pathname to get valid string names to use for the rest of creating a directory
    parent = dirname(pathStr1);
    child = basename(pathStr2);

    //Find the parent inode so that we can verify the location exists
    int parentIno = getino(parent); // get partent inode numbe

    //Make sure that the parent exists
    if(parentIno >= 0){
        pip = iget(dev, parentIno);

        //Confirm we are looking at a directory & not a file or link
        if (!S_ISDIR(pip->INODE.i_mode))
        {
            printf("ISSUE: cannot create a directory in a non-directory; %s\n", parent);
            return -1;
        }

        //Make sure there is no child with this name
        if (search(pip, child))
        {
            printf("ISSUE: child with name already exists parent: %s child: %s", parent, child);
            return -1;
        }

        //Make the directory
        mkdir_helper(pip, child);

        //Increase the links on the parents
        pip->INODE.i_links_count++;

        //Adjust access time
        pip->INODE.i_atime = time(0L); // reset the time

        //Let the system know we updated and write changes to the block
        pip->dirty = 1;
        iput(pip);

        //Return success
        return 0;
    } else{
        printf("ISSUE: parent in mkdir does not exists: %s\n",parent);
        return -1;
    }


}

/**
 *
 * Prepares the inode for a file that we just created so that it is recognizable in our filesystem
 *
 * @param ip pointer to the inode we are modifying
 */
void prepareFileINODE(INODE *ip){
    //Set up that this is a file & not a directory or linker
    ip->i_mode = FILE_MODE;
    ip->i_uid = running->uid; // Owner uid
    ip->i_gid = running->gid; // Group Id
    ip->i_size = BLKSIZE;     // Size in bytes
    ip->i_links_count = 1;    // Links count=1 since it's a file
    ip->i_atime = time(0L);   // set to current time
    ip->i_ctime = time(0L);
    ip->i_mtime = time(0L);
    ip->i_blocks = 2;            // LINUX: Blocks count in 512-byte chunks
    ip->i_block[0] = 0;          // new File has 0 data blocks
    for (int i = 1; i < 15; i++) // clear block memory
    {
        ip->i_block[i] = 0;
    }
}

/**
 *
 * Helps us create a file in our file system. Saves space of code within #func_creat.
 * This is where we allocate a new inode & write it to disk
 *
 * @param pip
 * @param fileName
 * @return 0 on success
 */
int creat_helper(MINODE* pip, char *fileName){
    //Allocate new space for inode and block
    int ino = ialloc(dev);

    //Get the Memory inode for the newly allocated inode
    MINODE *mip = iget(dev, ino);

    //Prepare the INODE for this new file
    INODE *ip = &mip->INODE;
    prepareFileINODE(ip);

    //Mark this file as changed and write to disk
    mip->dirty = 1;
    iput(mip);

    //Add this file's name to its directory block
    enter_name(pip, ino, fileName);

    //Return success
    return 0;
}


/**
 *
 * Function used to creat regular files on our filesystem.
 *
 * @param path | name of the file we are creating and the path
 * @return 0 success, -1 failure
 */
int func_creat(char *path){
    //Create variables to be written too in our validation function
    MINODE *pip;
    char *parent;
    char *child;

    //Store two instances of the name 1 for basename and 1 for dirname
    char pathStr1[256];
    char pathStr2[256];

    //Copy the name over so we can use it as a starting point. We need to do this because basename & dirname destroy strings
    strcpy(pathStr1, path);
    strcpy(pathStr2, path);

    //Search for a / to start the location this is for handling of absolute/relative paths
    dev = pathname[0] == '/' ? root->dev : running->cwd->dev;

    //Parse the pathname to get valid string names to use for the rest of creating a directory
    parent = dirname(pathStr1);
    child = basename(pathStr2);

    //Find the parent inode so that we can verify the location exists
    int parentIno = getino(parent); // get partent inode numbe

    //Make sure that the parent exists
    if(parentIno >= 0){
        pip = iget(dev, parentIno);

        //Confirm we are looking at a directory & not a file or link
        if (!S_ISDIR(pip->INODE.i_mode))
        {
            printf("ISSUE: cannot create a directory in a non-directory\n", parent);
            return -1;
        }

        //Make sure there is no child with this name
        if (search(pip, child))
        {
            printf("ISSUE: child with name already exists parent: %s child: %s", parent, child);
            return -1;
        }

        //Create the file
        creat_helper(pip, child);

        //Set up the access time for the directory
        pip->INODE.i_atime = time(0L);

        //Mark as changed and write to block
        pip->dirty = 1;
        iput(pip);

        //Return success
        return 0;
    } else{
        printf("ISSUE: parent in mkdir does not exists: %s\n",parent);
        return -1;
    }
}

/**
 *
 * Checks if a directory is empty
 *
 * @param mip memory inode pointer to the directory in question
 * @return 1 for empty, -1 for not empty
 */
int dir_empty(MINODE *mip){

    //Create directory variable to use while looking through the directory
    DIR *dp;

    //Create character pointer to use while advancing through records
    char *cp;

    //Create buffer to use while reading through blocks looking for entries
    char buf[BLKSIZE];


    //Get the INODE for the so we can read the real information
    INODE *ip = &mip->INODE;

    //Quickly check if there are any directories inside that would make it not empty
    if (ip->i_links_count > 2)return -1;

    //Loop through the direct blocks looking for entries
    for (int i = 0; i < 12; i++) // Search DIR direct blocks only
    {
        //Stop when we see a block thats 0
        if (ip->i_block[i] == 0)break;

        //Read the current block into the buffer so we can view its records
        get_block(mip->dev, mip->INODE.i_block[i], buf);

        //Prepare the searching variables for the buffer/block
        dp = (DIR *)buf;
        cp = buf;

        //Loop until we have read through the entire buffer
        while (cp < buf + BLKSIZE) // while not at the end of the block
        {
            //Create temporary string to hold the name of the entry we are looking at
            char temp[256];
            strncpy(temp, dp->name, dp->name_len);
            temp[dp->name_len] = 0;

            //When the name of an entry is not . or .. stop because this directory is not empty
            if (strcmp(temp, ".")  && strcmp(temp, "..") )return -1;

            //Advance to the next entry
            cp += dp->rec_len; // go to next entry in block
            dp = (DIR *)cp;
        }
    }

    //The directory was empty so return a positive 1 for success
    return 1;
}

/**
 *
 * @param pip
 * @param myino
 * @param myname
 * @return
 */
int enter_name(MINODE *pip, int myino, char *myname) {
    //Get the inode for the parent we are storing this entry under
    INODE *ip = &pip->INODE; // get the inode

    //Loop over the direct blocks and look for space the enter our entry
    for (int i = 0; i < 12; i++) {

        //If we are at a block that has no information
        if (ip->i_block[i] == 0)break;

        //Get the block number for the data stored
        int bno = ip->i_block[i];

        //Read in that block into a buffer and search for space for entry
        char buf[BLKSIZE];
        get_block(pip->dev, bno, buf);

        //Create a dir entry pointer based on the information on the block
        DIR *dp = (DIR *) buf;

        //Store our current position in the buffer
        char *cp = buf;

        //Loop until we have gone through all of the entries on the block
        while (cp + dp->rec_len < buf + BLKSIZE) {
            //Increase the current position by the entry length
            cp += dp->rec_len;

            //Recast to new spot
            dp = (DIR *) cp;
        }

        //Find the amount of space remaining
        int remainder = dp->rec_len - (4 * ((8 + dp->name_len + 3) / 4));

        //If there is sufficient space for this entry continue
        if (remainder >= 4 * ((8 + strlen(myname) + 3) / 4)) {

            //Set the record length up to be proper
            dp->rec_len = (4 * ((8 + dp->name_len + 3) / 4));

            //Go to the end and find new open entry space
            cp += dp->rec_len;
            dp = (DIR *) cp;

            //Set up the entry with the ino, name, legnth of name, and the record size
            dp->inode = myino;
            dp->name_len = strlen(myname);
            dp->rec_len = remainder;
            strcpy(dp->name, myname);


            //Write to disk
            put_block(dev, bno, buf);
            return 0;
        }

        //Handle when we need to allocate space for this entry
        //Set the size of this entry
        ip->i_size = BLKSIZE;

        //Get a new block allocated and store it at this spot
        bno = balloc(dev);
        ip->i_block[i] = bno;

        //Since we have changed the ino mark it as dirty
        pip->dirty = 1;

        //Read the block in from memory
        get_block(dev, bno, buf);

        //Cast our current position and entry to where we are at with this new block of data
        dp = (DIR *) buf;
        cp = buf;

        //Store the name length, inode, record length, and name
        dp->name_len = strlen(myname);
        dp->inode = myino;
        dp->rec_len = BLKSIZE;
        strcpy(dp->name, myname);

        //Write to disk
        put_block(dev, bno, buf);

        //Return success
        return 1;
    }
}

