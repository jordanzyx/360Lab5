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
extern PROC   proc[NPROC], *running;

extern char gpath[128];
extern char *name[64];
extern int n;

extern int fd, dev;
extern int nblocks, ninodes, bmap, imap, iblk;

extern char line[128], cmd[32], pathname[128];

/**
 *
 * Gets the block of data and reads it into a buffer
 *
 * @param dev device number we are getting the block from
 * @param blk int number representing where to find the block
 * @param buf buffer to read the information into
 * @return nothing
 */
int get_block(int dev, int blk, char *buf)
{
   lseek(dev, (long)blk*BLKSIZE, 0);
   read(dev, buf, BLKSIZE);
}

/**
 *
 * @param dev
 * @param blk
 * @param buf
 * @return
 */
int put_block(int dev, int blk, char *buf)
{
   lseek(dev, (long)blk*BLKSIZE, 0);
   write(dev, buf, BLKSIZE);
}

/**
 *
 * @param pathname
 * @return
 */
int tokenize(char *pathname)
{
  int i;
  char *s;
  printf("tokenize %s\n", pathname);

  strcpy(gpath, pathname);   // tokens are in global gpath[ ]
  n = 0;

  s = strtok(gpath, "/");
  while(s){
    name[n] = s;
    n++;
    s = strtok(0, "/");
  }
  name[n] = 0;

  for (i= 0; i<n; i++)
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
MINODE *iget(int dev, int ino)
{
  int i;
  MINODE *mip;
  char buf[BLKSIZE];
  int blk, offset;
  INODE *ip;

  for (i=0; i<NMINODE; i++){
    mip = &minode[i];
    if (mip->refCount && mip->dev == dev && mip->ino == ino){
       mip->refCount++;
       //printf("found [%d %d] as minode[%d] in core\n", dev, ino, i);
       return mip;
    }
  }

  for (i=0; i<NMINODE; i++){
    mip = &minode[i];
    if (mip->refCount == 0){
       //printf("allocating NEW minode[%d] for [%d %d]\n", i, dev, ino);
       mip->refCount = 1;
       mip->dev = dev;
       mip->ino = ino;

       // get INODE of ino to buf
       blk    = (ino-1)/8 + iblk;
       offset = (ino-1) % 8;

       //printf("iget: ino=%d blk=%d offset=%d\n", ino, blk, offset);

       get_block(dev, blk, buf);
       ip = (INODE *)buf + offset;
       // copy INODE to mp->INODE
       mip->INODE = *ip;
       return mip;
    }
  }

  printf("PANIC: no more free minodes\n");
  return 0;
}

/**
 *
 * @param mip
 */
void iput(MINODE *mip)
{
 int i, block, offset;
 char buf[BLKSIZE];
 INODE *ip;

 if (mip==0)return;

 mip->refCount--;

 if (mip->refCount > 0) return;
 if (!mip->dirty)       return;

 /* write INODE back to disk */
 block = ((mip->ino - 1) / 8) + iblk;
 offset = (mip->ino - 1) % 8;

 //Find the block containing the inode in question
 get_block(mip->dev,block,buf);

 //Point to the INODE
 INODE* start = (INODE*)buf;
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
int search(MINODE *mip, char *name)
{
   int i;
   char *cp, c, sbuf[BLKSIZE], temp[256];
   DIR *dp;
   INODE *ip;

   printf("search for %s in MINODE = [%d, %d]\n", name,mip->dev,mip->ino);
   ip = &(mip->INODE);

   /*** search for name in mip's data blocks: ASSUME i_block[0] ONLY ***/

   get_block(dev, ip->i_block[0], sbuf);
   dp = (DIR *)sbuf;
   cp = sbuf;
   printf("  ino   rlen  nlen  name\n");

   while (cp < sbuf + BLKSIZE){
     strncpy(temp, dp->name, dp->name_len);
     temp[dp->name_len] = 0;
     printf("%4d  %4d  %4d    %s\n",
           dp->inode, dp->rec_len, dp->name_len, dp->name);
     if (strcmp(temp, name)==0){
        printf("found %s : ino = %d\n", temp, dp->inode);
        return dp->inode;
     }
     cp += dp->rec_len;
     dp = (DIR *)cp;
   }
   return 0;
}

/**
 *
 * @param pathname
 * @return
 */
int getino(char *pathname)
{
  int i, ino, blk, offset;
  char buf[BLKSIZE];
  INODE *ip;
  MINODE *mip;

  printf("getino: pathname=%s\n", pathname);
  if (strcmp(pathname, "/")==0)
      return 2;

  // starting mip = root OR CWD
  if (pathname[0]=='/')
     mip = root;
  else
     mip = running->cwd;

  mip->refCount++;         // because we iput(mip) later

  tokenize(pathname);

  for (i=0; i<n; i++){
      printf("===========================================\n");
      printf("getino: i=%d name[%d]=%s\n", i, i, name[i]);

      ino = search(mip, name[i]);

      if (ino==0){
         iput(mip);
         printf("name %s does not exist\n", name[i]);
         return -1;
      }
      iput(mip);
      mip = iget(dev, ino);
   }

   iput(mip);
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
int findmyname(MINODE *parent, u32 myino, char myname[ ])
{
  // WRITE YOUR code here
  // search parent's data block for myino; SAME as search() but by myino
  // copy its name STRING to myname[ ]
    char *cp, c, sbuf[BLKSIZE], temp[256];
    DIR *dp;
    MINODE *ip = parent;

    //Loop through at max 12 blocks of the parent node searching
    for (int i = 0; i < 12; ++i) {

        //Make sure the block is around
        if(ip->INODE.i_block[i] != 0){

            //Copy the blcok into the buffer defined above
            get_block(ip->dev,ip->INODE.i_block[i],sbuf);

            //Set up the cariables to use while searching through the block
            dp = (DIR *)(sbuf); //dir pointer
            cp = sbuf;

            //Loop through the block
            while (cp < sbuf + BLKSIZE){
                //Check if the ino matches
                if(dp->inode == myino){
                    //Copy the name to the pointer we were given
                    strncpy(myname,dp->name,dp->name_len);
                    myname[dp->name_len] = 0; //get rid of \n

                    //Return successfully and stop looping
                    return 0;
                }

                //Advance through the block
                cp += dp->rec_len;
                dp = (DIR *)(cp);
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
  dp = (DIR *)buffer;
  cp = buffer;

  //Write to the inode pointer supplied
  *myino = dp->inode;

  //Advance the temporary pointer in the buffer
  cp += dp->rec_len;

  //Cast dp to the new directory found
  dp = (DIR *)cp;

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
int incFreeInodes(int dev){
    //Buffer to read the device information into
    char buf[BLKSIZE];

    //Read dev. info
    get_block(dev, 1, buf);

    //Cast buffer into the SUPER_block & adjust free inodes
    sp = (SUPER *)buf;
    sp->s_free_inodes_count++;

    //Write to block the updates to the super block
    put_block(dev, 1, buf);

    //Get the group description block
    get_block(dev, 2, buf);

    //Cast to Group descriptor & increase free inodes then write to disk the changes
    gp = (GD *)buf;
    gp->bg_free_inodes_count++;
    put_block(dev, 2, buf);
}

/**
 *
 * @param dev
 * @return
 */
int incFreeBlocks(int dev){
    char buf[BLKSIZE];

    get_block(dev, 1, buf);
    sp = (SUPER *)buf;
    sp->s_free_blocks_count++;
    put_block(dev, 1, buf);

    get_block(dev, 2, buf);
    gp = (GD *)buf;
    gp->bg_free_blocks_count++;
    put_block(dev, 2, buf);
}

/**
 *
 * @param dev
 * @return
 */
int decFreeBlocks(int dev){
    char buf[BLKSIZE];

    get_block(dev, 1, buf);
    sp = (SUPER *)buf;
    sp->s_free_blocks_count--;
    put_block(dev, 1, buf);

    get_block(dev, 2, buf);
    gp = (GD *)buf;
    gp->bg_free_blocks_count--;
    put_block(dev, 2, buf);
}

/**
 *
 * @param dev
 * @return
 */
int decFreeInodes(int dev){
    char buf[BLKSIZE];

    get_block(dev, 1, buf);
    sp = (SUPER *)buf;
    sp->s_free_inodes_count--;
    put_block(dev, 1, buf);

    get_block(dev, 2, buf);
    gp = (GD *)buf;
    gp->bg_free_inodes_count--;
    put_block(dev, 2, buf);
}

/**
 *
 * @param buf
 * @param bitnum
 * @return
 */
int tst_bit(char *buf, int bitnum){
    return buf[bitnum / 8] & (1 << (bitnum % 8));
}

/**
 *
 * @param buf
 * @param bitnum
 * @return
 */
int set_bit(char *buf, int bitnum){
    int bit, byte;
    byte = bitnum / 8;
    bit = bitnum % 8;
    if (buf[byte] |= (1 << bit))
    {
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
int clr_bit(char *buf, int bitnum){
    int bit, byte;
    byte = bitnum / 8;
    bit = bitnum % 8;
    if (buf[byte] &= ~(1 << bit))
    {
        return 1;
    }
    return 0;
}

/**
 *
 * @param dev
 * @return
 */
int balloc(int dev){
    int i;
    char buf[BLKSIZE];

    get_block(dev, bmap, buf);

    for (i = 0; i < nblocks; i++)
    {
        if (tst_bit(buf, i) == 0)
        {
            set_bit(buf, i);
            decFreeBlocks(dev);
            put_block(dev, bmap, buf);
            printf("Free disk block at %d\n", i + 1); // bits count from 0; ino from 1
            return i + 1;
        }
    }
    return 0;
}

/**
 *
 * @param dev
 * @return
 */
int ialloc(int dev){
    int i;
    char buf[BLKSIZE];

    get_block(dev, imap, buf); // read inode_bitmap block

    for (i = 0; i < ninodes; i++)
    {
        if (tst_bit(buf, i) == 0)
        {
            set_bit(buf, i);
            put_block(dev, imap, buf);
            decFreeInodes(dev);
            printf("allocated ino = %d\n", i + 1); // bits count from 0; ino from 1
            return i + 1;
        }
    }
    return 0;
}

/**
 *
 * @param pip
 * @param myino
 * @param myname
 * @return
 */
int enter_name(MINODE *pip, int myino, char *myname){
    char buf[BLKSIZE], *cp;
    int bno;
    INODE *ip;
    DIR *dp;

    int need_len = 4 * ((8 + strlen(myname) + 3) / 4); //ideal length of entry

    ip = &pip->INODE; // get the inode

    for (int i = 0; i < 12; i++)
    {

        if (ip->i_block[i] == 0)
        {
            break;
        }

        bno = ip->i_block[i];
        get_block(pip->dev, ip->i_block[i], buf); // get the block
        dp = (DIR *)buf;
        cp = buf;

        while (cp + dp->rec_len < buf + BLKSIZE) // Going to last entry of the block
        {
            printf("%s\n", dp->name);
            cp += dp->rec_len;
            dp = (DIR *)cp;
        }

        // at last entry
        int ideal_len = 4 * ((8 + dp->name_len + 3) / 4); // ideal len of the name
        int remainder = dp->rec_len - ideal_len;          // remaining space

        if (remainder >= need_len)
        {                            // space available for new netry
            dp->rec_len = ideal_len; //trim current entry to ideal len
            cp += dp->rec_len;       // advance to end
            dp = (DIR *)cp;          // point to new open entry space

            dp->inode = myino;             // add the inode
            strcpy(dp->name, myname);      // add the name
            dp->name_len = strlen(myname); // len of name
            dp->rec_len = remainder;       // size of the record

            put_block(dev, bno, buf); // save block
            return 0;
        }
        else
        {                         // not enough space in block
            ip->i_size = BLKSIZE; // size is new block
            bno = balloc(dev);    // allocate new block
            ip->i_block[i] = bno; // add the block to the list
            pip->dirty = 1;       // ino is changed so make dirty

            get_block(dev, bno, buf); // get the blcok from memory
            dp = (DIR *)buf;
            cp = buf;

            dp->name_len = strlen(myname); // add name len
            strcpy(dp->name, myname);      // name
            dp->inode = myino;             // inode
            dp->rec_len = BLKSIZE;         // only entry so full size

            put_block(dev, bno, buf); //save
            return 1;
        }
    }
}

/**
 *
 * @param dev
 * @param bno
 * @return
 */
int bdealloc(int dev, int bno){
    char buf[BLKSIZE]; // a sweet buffer

    get_block(dev, bmap, buf); // get the block
    clr_bit(buf, bno - 1);     // clear the bits to 0
    put_block(dev, bmap, buf); // write the block back
    incFreeBlocks(dev);        // increment the free block count
    return 0;
}

/**
 *
 * @param dev
 * @param ino
 * @return
 */
int idealloc(int dev, int ino){
    int i;
    char buf[BLKSIZE];

    if (ino > ninodes)
    {
        printf("inumber %d out of range\n", ino);
        return 0;
    }
    get_block(dev, imap, buf);
    clr_bit(buf, ino - 1);
    // write buf back
    put_block(dev, imap, buf);
    // update free inode count in SUPER and GD
    incFreeInodes(dev);
}

/**
 *
 * @param parent
 * @param name
 * @return
 */
int rm_child(MINODE *parent, char *name){
    DIR *dp, *prevdp, *lastdp;
    char *cp, *lastcp, buf[BLKSIZE], tmp[256], *startptr, *endptr;
    INODE *ip = &parent->INODE;

    for (int i = 0; i < 12; i++) // loop through all 12 blocks of memory
    {
        if (ip->i_block[i] != 0)
        {
            get_block(parent->dev, ip->i_block[i], buf); // get block from file
            dp = (DIR *)buf;
            cp = buf;

            while (cp < buf + BLKSIZE) // while not at the end of the block
            {
                strncpy(tmp, dp->name, dp->name_len); // copy name
                tmp[dp->name_len] = 0;                // add name delimiter

                if (!strcmp(tmp, name)) // name found
                {
                    if (cp == buf && cp + dp->rec_len == buf + BLKSIZE) // first/only record
                    {
                        bdealloc(parent->dev, ip->i_block[i]);
                        ip->i_size -= BLKSIZE;

                        while (ip->i_block[i + 1] != 0 && i + 1 < 12) // filling hole in the i_blocks since we deallocated this one
                        {
                            i++;
                            get_block(parent->dev, ip->i_block[i], buf);
                            put_block(parent->dev, ip->i_block[i - 1], buf);
                        }
                    }

                    else if (cp + dp->rec_len == buf + BLKSIZE) // Last record in the block, previous absorbs size
                    {
                        prevdp->rec_len += dp->rec_len;
                        put_block(parent->dev, ip->i_block[i], buf);
                    }

                    else // Record between others, must shift
                    {
                        lastdp = (DIR *)buf;
                        lastcp = buf;

                        while (lastcp + lastdp->rec_len < buf + BLKSIZE) // finding last record in the block
                        {
                            lastcp += lastdp->rec_len;
                            lastdp = (DIR *)lastcp;
                        }

                        lastdp->rec_len += dp->rec_len; // adding size to last one

                        startptr = cp + dp->rec_len; // start of copy block
                        endptr = buf + BLKSIZE;      // end of copy block

                        memmove(cp, startptr, endptr - startptr); // Shift left
                        put_block(parent->dev, ip->i_block[i], buf);
                    }

                    parent->dirty = 1;
                    iput(parent);
                    return 0;
                }

                prevdp = dp;
                cp += dp->rec_len;
                dp = (DIR *)cp;
            }
        }
    }
    printf("ERROR: child not found\n");
    return -1;
}

/**
 *
 * @param mip
 * @return
 */
int freeINodes(MINODE *mip) {
    char buf[BLKSIZE];
    INODE *ip = &mip->INODE;
    // 12 direct blocks
    for (int i = 0; i < 12; i++) {
        if (ip->i_block[i] == 0)
            break;
        // now deallocate block
        bdealloc(dev, ip->i_block[i]);
        ip->i_block[i] = 0;
    }
    // now worry about indirect blocks and doubly indirect blocks
    // (see pp. 762 in ULK for visualization of data blocks)
    // indirect blocks:
    if (ip->i_block[12] != NULL) {
        get_block(dev, ip->i_block[12], buf); // follow the ptr to the block
        int *ip_indirect = (int *)buf; // reference to indirect block via integer ptr
        int indirect_count = 0;
        while (indirect_count < BLKSIZE / sizeof(int)) { // split blksize into int sized chunks (4 bytes at a time)
            if (ip_indirect[indirect_count] == 0)
                break;
            // deallocate indirect block
            bdealloc(dev, ip_indirect[indirect_count]);
            ip_indirect[indirect_count] = 0;
            indirect_count++;
        }
        // now all indirect blocks have been dealt with, deallocate reference to indirect
        bdealloc(dev, ip->i_block[12]);
        ip->i_block[12] = 0;
    }

    // doubly indirect blocks (same code as above, different variables):
    if (ip->i_block[13] != NULL) {
        get_block(dev, ip->i_block[13], buf);
        int *ip_doubly_indirect = (int *)buf;
        int doubly_indirect_count = 0;
        while (doubly_indirect_count < BLKSIZE / sizeof(int)) {
            if (ip_doubly_indirect[doubly_indirect_count] == 0)
                break;
            // deallocate doubly indirect block
            bdealloc(dev, ip_doubly_indirect[doubly_indirect_count]);
            ip_doubly_indirect[doubly_indirect_count] = 0;
            doubly_indirect_count++;
        }
        bdealloc(dev, ip->i_block[13]);
        ip->i_block[13] = 0;
    }

    mip->INODE.i_blocks = 0;
    mip->INODE.i_size = 0;
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
    MINODE* mip = iget(dev, ino);

    //If we are the owner check the owner bits
    if(mip->INODE.i_uid == running->uid){
        if(mode == 'r' && ( (mip->INODE.i_mode & S_IRUSR) > 0))result = 1;
        if(mode == 'w' && ( (mip->INODE.i_mode & S_IWUSR) > 0))result = 1;
        if(mode == 'x' && ( (mip->INODE.i_mode & S_IXUSR) > 0))result = 1;
    }

    //If we are not the owner check the other bits
    if(mip->INODE.i_uid != running->uid){
        if(mode == 'r' && ( (mip->INODE.i_mode & S_IROTH) > 0))result = 1;
        if(mode == 'w' && ( (mip->INODE.i_mode & S_IWOTH) > 0))result = 1;
        if(mode == 'x' && ( (mip->INODE.i_mode & S_IXOTH) > 0))result = 1;
    }

    //Return the memory inode
    iput(mip);

    return result;
}
