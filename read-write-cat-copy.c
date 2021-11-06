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

int func_read(int fd,char *buffer,int bytes){

    //Obtain the MINODe & OFT for the file we are reading from
    OFT *oftp = running->fd[fd];
    MINODE *mip = oftp->mptr;

    //How many bytes we have read
    int count = 0;

    //What block we are on
    int lbk;

    //Byte we are starting to read from
    int startByte;

    //Integer representation of block returned from the i_block[lbk]
    int blk;

    //Integer buffer we use when reading from indirect blocks
    int ibuf[BLKSIZE] = { 0 };

    //Integer buffer we use when reading from double indirect blocks.
    int dibuf[BLKSIZE] = {0 };

    //How many bytes are available to read from when we start
    int avil = mip->INODE.i_size - oftp->offset;

    //Adjust bytes we want to read incase it is greater than the amount available
    if (bytes > avil)bytes = avil;

    //This is the start of the buffer and we use this to know we are writing too at all times (writing within the read process)
    char *cq = buffer;

    //Loop while bytes is still positive & available is aswell
    while (bytes && avil) {

        //This is where we are starting to read from
        lbk = oftp->offset / BLKSIZE;

        //Adjust the start byte to read from
        startByte = oftp->offset % BLKSIZE;

        //Read from the direct blocks to start
        if(lbk < 12)blk = mip->INODE.i_block[lbk];

        //Read from the first section of 256 indirect blocks
        if(lbk >= 12 && lbk < 256 + 12){
            //Read the block numbers into the integerBuffer then find block
            get_block(mip->dev, mip->INODE.i_block[12], ibuf);

            //Subtract 12 from the lbk we are finding because that is the offset for the indirect section
            blk = ibuf[lbk-12];
        }

        //Read from the double indirect blocks (note: we do not have to deal with triple indirect blocks but if we wanted this to be more solid we would need another section for that)
        if(lbk >= 256 + 12){
            //Start by reading the first set of indirect blocks into integer buffer
            get_block(mip->dev, mip->INODE.i_block[13], ibuf);

            //Find the proper block number based on how many pointers can be in a block
            lbk = lbk - (BLKSIZE / sizeof(int)) - 12;
            blk = ibuf[lbk / (BLKSIZE / sizeof(int))];

            //Now with this blk value read blocks into the double_integer buffer
            get_block(mip->dev, blk, dibuf);

            //Map to correct lbk
            lbk = lbk % (BLKSIZE / sizeof(int));

            //Get proper blk
            blk = dibuf[lbk];
        }

        //We use this buffer to read from the blk
        char rBuf[BLKSIZE];
        get_block(mip->dev, blk, rBuf);

        // Prepare to copy starting @ startByte into our buffer
        char *cp = rBuf + startByte;
        int remainder = BLKSIZE - startByte;

        //Adjust based on amount of bytes
        int amt = bytes <= remainder ? bytes : remainder;

        //Adjust offsets
        memcpy(cq, cp, amt);
        oftp->offset += amt;
        avil -= amt;
        cq += amt;
        count += amt;

        //Handle differences
        if (bytes <= remainder)remainder -= amt;
        else bytes -= amt;

        //Handle differences
        if (bytes <= remainder)bytes = 0;
        else remainder = 0;
    }

    return count;
}

int func_write_cmd(){
    int fd = 0;
    char buffer[BLKSIZE];

    //Read in the file descriptor to write too
    printf("Enter file descriptor to write too");
    bzero(buffer, BLKSIZE);
    fgets(buffer, BLKSIZE, stdin);
    sscanf("%d", buffer, &fd);

    //Read in the text to write
    printf("Enter what you want to write: ");
    bzero(buffer, BLKSIZE);
    fgets(buffer, BLKSIZE, stdin);

    //Validate that the fd is valid
    if (!(fd >= 0 && fd < NFD)) {
        printf("Error writing: invalid file descriptor\n");
        return -1;
    }

    // verify fd is open for RD or RW
    int mode = running->fd[fd]->mode;
    if (mode != WRITE && mode != READ_WRITE) {
        printf("Error writing: invalid mode of file we are attempting to write too\n");
        return -1;
    }

    //Write to file
    int n_bytes = sizeof(buffer);
    return func_write(fd, buffer, n_bytes);
}

int func_write(int fd, char* buffer, int byteCount){
    //Get the OFT for the file we want too write too
    OFT *oftp = running->fd[fd];
    MINODE *mip = oftp->mptr;
    INODE *ip = &mip->INODE;


    int lblk, startByte, blk, remainder, doubleblk;

    //Buffers we store the integers for blocks in when reading from the blocks
    char ibuf[BLKSIZE] = { 0 };
    char dibuf[BLKSIZE] = { 0 };

    //Starting point for copying the buffer
    char *cq = buffer;

    while (byteCount > 0){
        lblk = oftp->offset / BLKSIZE;
        startByte = oftp->offset % BLKSIZE;

        //Handle writing the direct blocks
        if (lblk < 12){
            //Make sure that we have somewhere to write too
            if(ip->i_block[lblk] == 0)ip->i_block[lblk] = balloc(mip->dev);

            //Set the blk up so we can write
            blk = ip->i_block[lblk];
        }

        //Handle writing the indirect blocks
        if(lblk >= 12 && lblk < 268){

            //Make sure we have a place to write too
            if(ip->i_block[12] == 0){
                //Allocate space
                ip->i_block[12] = balloc(mip->dev);

                //Confirm the space is there if not exit
                if(ip->i_block[12] == 0)return 0;

                //Get the block that we just allocated's information into a buffer
                get_block(mip->dev, ip->i_block[12], ibuf);

                //Go to each section in the list of blocks stored and set them to 0
                int *ptr = (int *)ibuf;

                //Loop through the chunks and set them to null
                for(int i = 0; i < (BLKSIZE / sizeof(int)); i++){
                    ptr[i] = 0;
                }

                //Write to disk
                put_block(mip->dev, ip->i_block[12], ibuf);

                //Increase the # of blocks
                mip->INODE.i_blocks++;
            }

            //This is where we store the indirect pointers to blocks
            int indir_buf[BLKSIZE / sizeof(int)] = { 0 };

            //Read the information in
            get_block(mip->dev, ip->i_block[12], (char *)indir_buf);

            //Set the block we are using. adjust by the offste of 12 since we discard the first 12 direct blocks
            blk = indir_buf[lblk - 12];

            //If the blk is not around
            if(blk == 0){
                //Allocate new block and set its location
                indir_buf[lblk - 12] = balloc(mip->dev);

                //Set up blk so we can use it
                blk = indir_buf[lblk - 12];

                //Increase the amount of blocks
                ip->i_blocks++;

                //Write this update to the block on top
                put_block(mip->dev, ip->i_block[12], (char *)indir_buf);
            }
        }

        //Handle writing the doubly indirect blocks
        if(lblk >= 268){
            //Adjust the lblk based on the the size of each chunk & the offset of the direct blocks
            lblk = lblk - (BLKSIZE/sizeof(int)) - 12;

            //Make sure we have a block to write too
            if(mip->INODE.i_block[13] == 0){

                //Allocate block
                ip->i_block[13] = balloc(mip->dev);

                //Confirm the allocation worked
                if (ip->i_block[13] == 0)return 0;

                //Get the data in the block we just allocated
                get_block(mip->dev, ip->i_block[13], ibuf);
                int *ptr = (int *)ibuf;

                //Make sure that all the blocks are null
                for(int i = 0; i < (BLKSIZE / sizeof(int)); i++){
                    ptr[i] = 0;
                }

                //Write the updates to the block
                put_block(mip->dev, ip->i_block[13], ibuf);

                //Increase the amount of blocks
                ip->i_blocks++;
            }

            //This is where we have we have the first set of indirect's
            int doublebuf[BLKSIZE/sizeof(int)] = {0};
            get_block(mip->dev, ip->i_block[13], (char *)doublebuf);

            //Determine the outside blk based on the blk passed in divided by the size of chunks
            doubleblk = doublebuf[lblk/(BLKSIZE / sizeof(int))];

            //Make sure we have a outside blk
            if(doubleblk == 0){
                //Allocate a block
                doublebuf[lblk/(BLKSIZE / sizeof(int))] = balloc(mip->dev);

                //Store newly allocated block
                doubleblk = doublebuf[lblk/(BLKSIZE / sizeof(int))];

                //Confirm we successfully allocated the block
                if (doubleblk == 0)return 0;

                //Get the block for what we just allocated
                get_block(mip->dev, doubleblk, dibuf);
                int *ptr = (int *)dibuf;

                //Set all of the chunks to null
                for(int i = 0; i < (BLKSIZE / sizeof(int)); i++){
                    ptr[i] = 0;
                }

                //Write the updates to the inside blk
                put_block(mip->dev, doubleblk, dibuf);

                //Increase amount of blocks
                ip->i_blocks++;

                //Write the changes to the outside blk
                put_block(mip->dev, mip->INODE.i_block[13], (char *)doublebuf);
            }

            //Set the entire section of blks we are looking @ to 0
            memset(doublebuf, 0, BLKSIZE / sizeof(int));

            //Get the lowest most set of blocks
            get_block(mip->dev, doubleblk, (char *)doublebuf);

            //Make sure the lowest block is available
            if (doublebuf[lblk % (BLKSIZE / sizeof(int))] == 0) {
                //Allocate the lowest block
                doublebuf[lblk % (BLKSIZE / sizeof(int))] = balloc(mip->dev);

                //Assign the blk and use modulous to find the correct blk
                blk = doublebuf[lblk % (BLKSIZE / sizeof(int))];

                //Confirm we successfully allocated
                if (blk == 0)return 0;

                //Increase amount of blocks
                ip->i_blocks++;

                //Write the updates to blk
                put_block(mip->dev, doubleblk, (char *)doublebuf);
            }
        }

        //Create buffer & read in the blk we are looking at to write too
        char buf[BLKSIZE] = {0 };
        get_block(mip->dev, blk, buf);

        //Store the current position & how many blocks are remaining
        char *cp = buf + startByte;
        remainder = BLKSIZE - startByte;

        //How much we need to adjust by
        int amt = remainder <= byteCount ? remainder : byteCount;

        //Adjust the info for the next loop
        memcpy(cp,cq, amt);
        cq += amt;
        oftp->offset += amt;
        byteCount -= amt;

        //if the file offset is greater than the size of the INODE swap the sizes
        if(oftp->offset > mip->INODE.i_size)mip->INODE.i_size = oftp->offset;

        //Write to disk
        put_block(mip->dev, blk, buf);
    }

    //Mark as dirty
    mip->dirty = 1;

    //Bytes written
    return byteCount;
}

int func_cat(char *file){
    //Open the file to read
    int fd = func_open(file, READ);

    //Confirm the fd is valid
    if (!(fd >= 0 && fd < NFD)) {
        printf("Error: invalid file descriptor used for cat command\n");
        func_close(fd);
        return -1;
    }

    //Buffer to read the file into
    char buf[BLKSIZE];

    //How many bytes are being read so we can stop looping once it hits 0
    int n;

    //Loop through file and read it
    while ((n = func_read(fd, buf, BLKSIZE))) {
        //Set the current position to the start of the buffer
        char *cp = buf;

        //Set the end of the buffer read to null
        buf[n] = 0;

        //Loop through the buffer until we meet a null character
        while (*cp != '\0') {
            //Print newline if we need too or just print the character
            if (*cp == '\n')printf("\n");
            else printf("%c", *cp);

            //Increase the current position
            cp++;
        }
    }

    //Print new line
    printf("\n");

    //Close the file & return
    func_close(fd);
    return 0;
}

int func_cp(char* source, char* destination){

    //Open the two files
    int fdSource = func_open(source, READ);
    int fdDestination = func_open(destination, READ_WRITE);

    //Make sure we got two files back
    if (fdSource == -1 || fdDestination == -1) {
        if (fdDestination == -1) func_close(fdDestination);
        if (fdSource == -1) func_close(fdSource);
        return -1;
    }

    //Buffer to read into then write to the destination with
    char buf[BLKSIZE] = {0};

    //Make sure we are full of \0's
    memset(buf, '\0', BLKSIZE);

    //How many bytes we are reading
    int n = 0;

    //Loop while we can read
    while ((n = func_read(fdSource, buf, BLKSIZE))){
        //Write
        func_write(fdDestination, buf, n);
        //Reset the buffer
        memset(buf,'\0',BLKSIZE);
    }

    //Close the files & return
    func_close(fdSource);
    func_close(fdDestination);
    return 0;
}
