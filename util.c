/*********** util.c file ****************/
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <ext2fs/ext2_fs.h>
#include <string.h>
#include <libgen.h>
#include <sys/stat.h>
#include "unistd.h"
#include <time.h>

#include "type.h"


/**** globals defined in main.c file ****/
extern MINODE minode[NMINODE];
extern MINODE *root;
extern PROC proc[NPROC], *running;

extern char gpath[128];
extern char *name[64];
extern int n;

extern int fd, dev;
extern int nblocks, ninodes, bmap, imap, iblk;

extern char line[128], cmd[32], pathname[128];

extern MOUNT mounts[NMOUNT];


/**
 * Gets a mount in our mount table from device number
 * @param dev device number to search for in table
 * @return mount
 */
MOUNT *getMount(int dev) {
    for (int i = 0; i < NMOUNT; ++i) {
        if (mounts[i].dev == dev)return &mounts[i];
    }
}

/**
 *
 * Gets the block of data and reads it into a buffer
 *
 * @param dev device number we are getting the block from
 * @param blk int number representing where to find the block
 * @param buf buffer to read the information into
 * @return nothing
 */
int get_block(int dev, int blk, char *buf) {
    lseek(dev, (long) blk * BLKSIZE, 0);
    read(dev, buf, BLKSIZE);
}

/**
 *
 * @param dev
 * @param blk
 * @param buf
 * @return
 */
int put_block(int dev, int blk, char *buf) {
    lseek(dev, (long) blk * BLKSIZE, 0);
    write(dev, buf, BLKSIZE);
}

/**
 *
 * @param pathname
 * @return
 */
int tokenize(char *pathname) {
    int i;
    char *s;
    printf("tokenize %s\n", pathname);

    strcpy(gpath, pathname);   // tokens are in global gpath[ ]
    n = 0;

    s = strtok(gpath, "/");
    while (s) {
        name[n] = s;
        n++;
        s = strtok(0, "/");
    }
    name[n] = 0;

    for (i = 0; i < n; i++)
        printf("%s  ", name[i]);
    printf("\n");
}

// return minode pointer to loaded INODE
/**
 *
 * @param dev
 * @param ino
 * @return
 */
MINODE *iget(int dev, int ino) {
    //Get the mount for this device
    MOUNT *mp = getMount(dev);

    //Search for ino on the in our list of MINODE's
    for (int i = 0; i < NMINODE; i++) {
        //Grab the memory inode pointer for this iteration
        MINODE *mip = &minode[i];

        //Confirm the reference count is 1, we are on the same device and the ino's match
        if (mip->refCount && mip->dev == dev && mip->ino == ino) {
            mip->refCount++;
            return mip;
        }
    }

    //If we cannot find find an ino MINODE above we need to allocate a new one in a free slot
    for (int i = 0; i < NMINODE; i++) {
        //Grab the memory inode pointer for this iteration
        MINODE *mip = &minode[i];

        //Find a memory inode with 0 references so we can use it to put in a new node
        if (mip->refCount == 0) {

            //Increase the reference count by one and set up the device and ino values
            mip->ino = ino;
            mip->dev = dev;
            mip->refCount = 1;

            // This is where we are going to look on the disk for this files information
            int blk = (ino - 1) / 8 + mp->iblock;
            int offset = (ino - 1) % 8;

            //Get the data for this INODE on the disk and adjust it by the offset so that we can find the real INODE value
            char buf[BLKSIZE];
            get_block(dev, blk, buf);
            INODE *ip = (INODE *) buf + offset;

            //Store the INODE value on the Memory INODE (wrapper of INODE)
            mip->INODE = *ip;
            return mip;
        }
    }

    printf("Error: we are out of MINODE's\n");
    return 0;
}

/**
 *
 * @param mip
 */
void iput(MINODE *mip) {
    int i, block, offset;
    char buf[BLKSIZE];
    INODE *ip;
    MOUNT *mp = getMount(dev);
    if (mip == 0)return;

    mip->refCount--;

    if (mip->refCount > 0) return;
    if (!mip->dirty) return;

    /* write INODE back to disk */
    block = ((mip->ino - 1) / 8) + mp->iblock;
    offset = (mip->ino - 1) % 8;

    //Find the block containing the inode in question
    get_block(mip->dev, block, buf);

    //Point to the INODE
    INODE *start = (INODE *) buf;
    ip = start + offset;

    //Write ip to  the inode in the block
    *ip = mip->INODE;

    //Write to disk finally
    put_block(mip->dev, block, buf);

    /**************** NOTE ******************************
     For mountroot, we never MODIFY any loaded INODE
                    so no need to write it back
     FOR LATER WROK: MUST write INODE back to disk if refCount==0 && DIRTY

     Write YOUR code here to write INODE back to disk
    *****************************************************/
}

/**
 *
 * @param mip
 * @param name
 * @return
 */
int search(MINODE *mip, char *name) {
    //Get the literal INODE for this memory inode we will use this to read in its information
    INODE *ip = &(mip->INODE);

    //We are going to look within the first block of data for this directory looking for an entry with a matching name
    char sbuf[BLKSIZE];
    get_block(dev, ip->i_block[0], sbuf);
    DIR *dp = (DIR *) sbuf;

    //Current position. This points to where we are at inside of this block of data
    char *cp = sbuf;

    //Loop over the block of data and search for matching entries
    while (cp < sbuf + BLKSIZE) {
        //Create temp string to hold the name of the directory entry
        char temp[256];

        //Copy in the name of the entry to a string and set the last value to 0(null)
        strncpy(temp, dp->name, dp->name_len);
        temp[dp->name_len] = 0;

        //Check to see if the name matches the one we are looking for
        if (strcmp(temp, name) == 0)return dp->inode;

        //Increase the current position by the length of each record
        cp += dp->rec_len;

        //Set up the next entry (record)
        dp = (DIR *) cp;
    }
    return 0;
}

/**
 *
 * @param pathname
 * @return
 */
int getino(char *pathname) {
    int i, blk, offset;
    char buf[BLKSIZE];
    INODE *ip;


    //Check if we are at root and just return 2
    if (strcmp(pathname, "/") == 0)return 2;

    //This is where we are going to start our search for the ino of a path
    int ino;

    // Adjust the device and INO based on if we are looking from root or locally cwd
    if (pathname[0] == '/') {
        dev = root->dev;
        ino = root->ino;
    } else {
        dev = running->cwd->dev;
        ino = running->cwd->ino;
    }

    //Turn the path of this search into pieces so we can look by piece
    tokenize(pathname);

    //Get the Memory inode for the place we are starting to search from
    MINODE *mip = iget(dev, ino);

    printf("INO starting from %d\n",ino);

    //Loop over each string that we tokenized from our path and search
    for (i = 0; i < n; i++) {
        //Search for the name inside of the current ino we are at
        ino = search(mip, name[i]);

        //If we cannot find any ino to work with return -1 and put make the memory inode we were working with
        if (ino == 0) {
            iput(mip);
            printf("name %s does not exist\n", name[i]);
            return -1;
        }

        //If we are dealing with root but the devices are differenct we need to traverse up
        if (ino == 2 && dev != root->dev) {
            //Go through all of the mounts and look for a device that matches and grab its mount point
            for (int i = 0; i < 8; i++) {

                //Confirm the device is the same
                if (mounts[i].dev == dev) {

                    //Put back the MIP we were using to search with
                    iput(mip);

                    //Assign the new MIP to search with to the mount point
                    mip = mounts[i].mountPoint;

                    //Adjust our global device number
                    dev = mip->dev;
                    break;
                }
            }
        } else {
            //Make the memory inode as changed and return it
            mip->dirty = 1;
            iput(mip);

            //Grab new MIP from the device with the ino
            mip = iget(dev, ino);

            //If this MIP is mounted we need to adjust to the mounting point of the device mounted
            if (mip->mounted) {
                //Grab the mount on top of this MIP
                MOUNT *mp = mip->mptr;

                //Adjust the global device to the new device we are looking at
                dev = mp->dev;

                //Make sure we are starting at the root of this new device
                ino = 2;

                //Return the current MIP
                iput(mip);

                //Get the root for this device
                mip = iget(dev, ino);
            }
        }
    }

    //Mark MIP as dirty and put back the MIP
    mip->dirty = 1;
    iput(mip);

    //Return the found INO
    return ino;
}

// These 2 functions are needed for pwd()
/**
 *
 * @param parent
 * @param myino
 * @param myname
 * @return
 */
int findmyname(MINODE *parent, u32 myino, char myname[]) {
    // WRITE YOUR code here
    // search parent's data block for myino; SAME as search() but by myino
    // copy its name STRING to myname[ ]
    char *cp, c, sbuf[BLKSIZE], temp[256];
    DIR *dp;
    MINODE *ip = parent;

    //Loop through at max 12 blocks of the parent node searching
    for (int i = 0; i < 12; ++i) {

        //Make sure the block is around
        if (ip->INODE.i_block[i] != 0) {

            //Copy the blcok into the buffer defined above
            get_block(ip->dev, ip->INODE.i_block[i], sbuf);

            //Set up the cariables to use while searching through the block
            dp = (DIR *) (sbuf); //dir pointer
            cp = sbuf;

            //Loop through the block
            while (cp < sbuf + BLKSIZE) {
                //Check if the ino matches
                if (dp->inode == myino) {
                    //Copy the name to the pointer we were given
                    strncpy(myname, dp->name, dp->name_len);
                    myname[dp->name_len] = 0; //get rid of \n

                    //Return successfully and stop looping
                    return 0;
                }

                //Advance through the block
                cp += dp->rec_len;
                dp = (DIR *) (cp);
            }
        }
    }

    return -1;
}

/**
 *
 * @param mip
 * @param myino
 * @return
 */
int findino(MINODE *mip, u32 *myino) // myino = i# of . return i# of ..
{
    // mip points at a DIR minode
    // WRITE your code here: myino = ino of .  return ino of ..
    // all in i_block[0] of this DIR INODE.

    //Create buffer and temporary pointer
    char buffer[BLKSIZE], *cp;

    //Create variable to store directory
    DIR *dp;

    //Read the first block in the MINODE
    get_block(mip->dev, mip->INODE.i_block[0], buffer);

    //Set up dp & cp
    dp = (DIR *) buffer;
    cp = buffer;

    //Write to the inode pointer supplied
    *myino = dp->inode;

    //Advance the temporary pointer in the buffer
    cp += dp->rec_len;

    //Cast dp to the new directory found
    dp = (DIR *) cp;

    //Return the inode found
    return dp->inode;
}

/**
 *
 * Increases the counter on a mount with how many free inodes are left
 *
 * @param dev device id for the mount we are adjusting the count for
 * @return nothing
 */
int incFreeInodes(int dev) {
    //Buffer to read the device information into
    char buf[BLKSIZE];

    //Read dev. info
    get_block(dev, 1, buf);

    //Cast buffer into the SUPER_block & adjust free inodes
    sp = (SUPER *) buf;
    sp->s_free_inodes_count++;

    //Write to block the updates to the super block
    put_block(dev, 1, buf);

    //Get the group description block
    get_block(dev, 2, buf);

    //Cast to Group descriptor & increase free inodes then write to disk the changes
    gp = (GD *) buf;
    gp->bg_free_inodes_count++;
    put_block(dev, 2, buf);
}

/**
 *
 * @param dev
 * @return
 */
int incFreeBlocks(int dev) {
    //Create a buffer to store the information we are reading in about this device
    char buf[BLKSIZE];

    //Get the super block info for this device
    get_block(dev, 1, buf);
    sp = (SUPER *) buf;

    //Increment the free blocks and write it back to the disk
    sp->s_free_blocks_count++;
    put_block(dev, 1, buf);

    //Get the group block for this device
    get_block(dev, 2, buf);
    gp = (GD *) buf;

    //Increment free blocks and write back to disk
    gp->bg_free_blocks_count++;
    put_block(dev, 2, buf);
}

/**
 *
 * @param dev
 * @return
 */
int decFreeBlocks(int dev) {
    //Create a buffer to store the information we are reading in about this device
    char buf[BLKSIZE];

    //Get the super block info for this device
    get_block(dev, 1, buf);
    sp = (SUPER *) buf;

    //Decrease the free blocks and write it back to the disk
    sp->s_free_blocks_count--;
    put_block(dev, 1, buf);

    //Get the group block for this device
    get_block(dev, 2, buf);
    gp = (GD *) buf;

    //Decrease free blocks and write back to disk
    gp->bg_free_blocks_count--;
    put_block(dev, 2, buf);
}

/**
 *
 * @param dev
 * @return
 */
int decFreeInodes(int dev) {
    //Create a buffer to store the information we are reading in about this device
    char buf[BLKSIZE];

    //Get the super block info for this device
    get_block(dev, 1, buf);
    sp = (SUPER *) buf;


    //Decrease the free inodes and write it back to the disk
    sp->s_free_inodes_count--;
    put_block(dev, 1, buf);

    //Get the group block for this device
    get_block(dev, 2, buf);
    gp = (GD *) buf;

    //Decrease the free inodes and write it back to disk
    gp->bg_free_inodes_count--;
    put_block(dev, 2, buf);
}

/**
 *
 * @param buf
 * @param bitnum
 * @return
 */
int tst_bit(char *buf, int bitnum) {
    return buf[bitnum / 8] & (1 << (bitnum % 8));
}

/**
 *
 * @param buf
 * @param bitnum
 * @return
 */
int set_bit(char *buf, int bitnum) {
    int bit, byte;
    byte = bitnum / 8;
    bit = bitnum % 8;
    if (buf[byte] |= (1 << bit)) {
        return 1;
    }
    return 0;
}

/**
 *
 * @param buf
 * @param bitnum
 * @return
 */
int clr_bit(char *buf, int bitnum) {
    int bit, byte;
    byte = bitnum / 8;
    bit = bitnum % 8;
    if (buf[byte] &= ~(1 << bit)) {
        return 1;
    }
    return 0;
}

/**
 *
 * @param dev
 * @return
 */
int balloc(int dev) {
    //Create a buffer to story information about this devices bitmap
    char buf[BLKSIZE];

    //Get the mount for this device
    MOUNT *mp = getMount(dev);

    //Get the information at the spot of the bitmap
    get_block(dev, mp->bmap, buf);

    //Iterate over each block to find a spot to store new info on a disk block
    for (int i = 0; i < mp->numBlocks; i++) {
        //Make sure that the bit is 0 and we have a free spot
        if (tst_bit(buf, i) == 0) {

            //Set the bit to 1 at its spot in the buffer
            set_bit(buf, i);

            //Decrease free blocks on the device
            decFreeBlocks(dev);

            //Write to disk
            put_block(dev, mp->bmap, buf);

            //Return the spot
            return i + 1;
        }
    }

    printf("Could not find space on device %d: there was %d blocks and they are all being used",dev,mp->numBlocks);
    //Return 0 we cou't allocate a block on the device
    return 0;
}

/**
 *
 * @param dev
 * @return
 */
int ialloc(int dev) {
    //Create a buffer to read in the inode bit map too
    char buf[BLKSIZE];

    //Get the mount for this device
    MOUNT *mp = getMount(dev);

    //Read in the inode bit map to the buffer
    get_block(dev, mp->imap, buf);

    //Loop over the imap and find a free spot for a inode
    for (int i = 0; i < mp->numINodes; i++) {
        //Confirm that we have a free spot in the map
        if (tst_bit(buf, i) == 0) {

            //Update the spot to taken
            set_bit(buf, i);

            //Write back to the disk the updates on the map
            put_block(dev, mp->imap, buf);

            //Decrease the amount of free inodes
            decFreeInodes(dev);
            return i + 1;
        }
    }

    //Return 0 we could not allocate a new inode
    return 0;
}


/**
 *
 * @param dev
 * @param bno
 * @return
 */
int bdealloc(int dev, int bno) {
    //Create a buffer to read in the bitmap for this device
    char buf[BLKSIZE];

    //Get the mount for this device
    MOUNT *mp = getMount(dev);

    //Read in the bitmap to buffer
    get_block(dev, mp->bmap, buf); // get the block

    //Clear the bit at the blk to 0
    clr_bit(buf, bno - 1);

    //Write to disk and increase free block count
    put_block(dev, mp->bmap, buf);
    incFreeBlocks(dev);
    return 0;
}

/**
 *
 * @param dev
 * @param ino
 * @return
 */
int idealloc(int dev, int ino) {
    //Create a buffer to read in the inode bit map for this device
    char buf[BLKSIZE];

    //Find the mount point for this device
    MOUNT *mp = getMount(dev);

    //Validate the ino is valid
    if (ino > mp->numINodes) {
        printf("invalid ino value: %d\n", ino);
        return 0;
    }

    //Read in the Inode bit map
    get_block(dev, mp->imap, buf);

    //Reset the bit for the ino we are deallocating
    clr_bit(buf, ino - 1);

    //Write to disk and increase the free inode count on the device
    put_block(dev, mp->imap, buf);
    incFreeInodes(dev);
}


/**
 *
 * @param mip
 * @return
 */
int freeINodes(MINODE *mip) {\
    //Find the literal INODE from the wrapper mip
    INODE *ip = &mip->INODE;

    // Loop over direct blocks and deallocate any that have data on them
    for (int i = 0; i < 12; i++) {

        //Skip over blocks with no data
        if (ip->i_block[i] == 0)break;

        // Deallocate data
        bdealloc(dev, ip->i_block[i]);

        //Mark the block as empty
        ip->i_block[i] = 0;
    }

    //Handle wiping our single indirect blocks
    if (ip->i_block[12] != NULL) {
        //Create a buffer to store indirect block in
        char buf[BLKSIZE];

        //Read the 256 integers in our 1024 char buffer in from the block
        get_block(dev, ip->i_block[12], buf);

        //Create a way to access those 256 integers instead of them being in a character buffer
        int *ibuf = (int *) buf;

        //Store how many integers we actually have here
        int count = 0;

        //Loop at max 256 times until we find a integer value that does not represent a block on the disk
        while (count < BLKSIZE / sizeof(int)) {
            //Stop looping we are done reading blocks with real information
            if (ibuf[count] == 0)break;

            // Deallocate the block on the disk
            bdealloc(dev, ibuf[count]);

            //Set the spot to free in the integer buffer
            ibuf[count] = 0;

            //Go to the next spot
            count++;
        }
        //Deallocate the block that had all this information now
        bdealloc(dev, ip->i_block[12]);

        //Set the single indirect block to free
        ip->i_block[12] = 0;
    }

    //Handle the doubly indirect blocks
    if (ip->i_block[13] != NULL) {

        //Create a buffer to read in the 256 blk positions on the dick
        char buf[BLKSIZE];

        //Read the information into the buffer
        get_block(dev, ip->i_block[13], buf);

        //Cast to int buffer so we can find the new locations on the disk
        int *ibuf = (int *) buf;

        //Create a count variable we use while iterating
        int count = 0;

        //Loop at max 256 times until we find a free spot and stop
        while (count < BLKSIZE / sizeof(int)) {
            //Stop because there arent any more real block locations to deallocate
            if (ibuf[count] == 0)break;

            //Get the final blk location for this iteration
            int finalBlock = ibuf[count];

            //Deallocate the block at the final position
            bdealloc(dev, finalBlock);

            //Set that location to free
            ibuf[count] = 0;

            //Iterate again
            count++;
        }

        //Deallocate the block that started this whole mess
        bdealloc(dev, ip->i_block[13]);

        //Set the block to free
        ip->i_block[13] = 0;
    }

    //Update how many blocks there are
    mip->INODE.i_blocks = 0;

    //Update size
    mip->INODE.i_size = 0;

    //Mark and return
    mip->dirty = 1;
    iput(mip);
}

/**
* Access checks for access with the current user process to a specific file with a mode of access.
* @param filename name of the file we are checking for access too
* @param mode what mode are we trying to use r/w/x
* @return result which is 1 for has access.
*/
int func_access(char *filename, char mode) {
    //This is the value we are going to return at the end showing if we have access or not
    int result = 0;

    //Check if we are the super user
    if (running->uid == 0)return 1;

    //Get the INODE for the file
    int ino = getino(filename);
    MINODE *mip = iget(dev, ino);

    //If we are the owner check the owner bits
    if (mip->INODE.i_uid == running->uid) {
        if (mode == 'r' && ((mip->INODE.i_mode & S_IRUSR) > 0))result = 1;
        if (mode == 'w' && ((mip->INODE.i_mode & S_IWUSR) > 0))result = 1;
        if (mode == 'x' && ((mip->INODE.i_mode & S_IXUSR) > 0))result = 1;
    }

    //If we are not the owner check the other bits
    if (mip->INODE.i_uid != running->uid) {
        if (mode == 'r' && ((mip->INODE.i_mode & S_IROTH) > 0))result = 1;
        if (mode == 'w' && ((mip->INODE.i_mode & S_IWOTH) > 0))result = 1;
        if (mode == 'x' && ((mip->INODE.i_mode & S_IXOTH) > 0))result = 1;
    }

    //Return the memory inode
    iput(mip);

    return result;
}


