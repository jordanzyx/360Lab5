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
    int dibuf[BLKSIZE] = { 0 };

    //How many bytes are available to read from when we start
    int available = mip->INODE.i_size - oftp->offset;
    printf("Available: %d\n",available);

    //Adjust bytes we want to read incase it is greater than the amount available
    if (bytes > available)bytes = available;

    //This is the start of the buffer and we use this to know we are writing too at all times (writing within the read process)
    char *cq = buffer;

    //Loop while bytes is still positive & available is aswell
    while (bytes && available) {
        if(available <= 0)break;


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
            get_block(mip->dev, mip->INODE.i_block[13], (char *)ibuf);

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
        available -= amt;
        cq += amt;
        count += amt;
        cp += amt;

        //Handle differences
        if (bytes <= remainder){
            remainder -= amt;
            bytes = 0;
        }
        else {
            bytes -= amt;
            remainder = 0;
        }
    }

    return count;
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

    printf("finished printing file\n");

    //Print new line
    printf("\n");

    //Close the file & return
    func_close(fd);
    return 0;
}
