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

//re-write
int func_write(int fd, char* buffer, int byteCount){
    OFT *oftp = running->fd[fd];
    MINODE *mip = oftp->mptr;
    INODE *ip = &mip->INODE;
    int count = 0, lblk, startByte, blk, remainder, doubleblk;
    char ibuf[BLKSIZE] = { 0 }, doubly_ibuf[BLKSIZE] = { 0 };

    char *cq = buf;

    while (n_bytes > 0)
    {
        lblk = oftp->offset / BLKSIZE;
        startByte = oftp->offset % BLKSIZE;


        if (lblk < 12) // direct blocks
        {
            if(ip->i_block[lblk] == 0)
            {
                ip->i_block[lblk] = balloc(mip->dev);
            }
            blk = ip->i_block[lblk];
        }

        else if (lblk >= 12 && lblk < 256 + 12 )
        {
            char tbuf[BLKSIZE] = { 0 };

            if(ip->i_block[12] == 0)
            {
                int block_12 = ip->i_block[12] = balloc(mip->dev);

                if (block_12 == 0)
                    return 0;

                get_block(mip->dev, ip->i_block[12], ibuf);
                int *ptr = (int *)ibuf;
                for(int i = 0; i < (BLKSIZE / sizeof(int)); i++)
                {
                    ptr[i] = 0;
                }

                put_block(mip->dev, ip->i_block[12], ibuf);
                mip->INODE.i_blocks++;
            }
            int indir_buf[BLKSIZE / sizeof(int)] = { 0 };
            get_block(mip->dev, ip->i_block[12], (char *)indir_buf);
            blk = indir_buf[lblk - 12];

            if(blk == 0){
                blk = indir_buf[lblk - 12] = balloc(mip->dev);
                ip->i_blocks++;
                put_block(mip->dev, ip->i_block[12], (char *)indir_buf);

            }
        }

        else
        {
            lblk = lblk - (BLKSIZE/sizeof(int)) - 12;
            //printf("%d\n", mip->INODE.i_block[13]);
            if(mip->INODE.i_block[13] == 0)
            {
                int block_13 = ip->i_block[13] = balloc(mip->dev);

                if (block_13 == 0)
                    return 0;

                get_block(mip->dev, ip->i_block[13], ibuf);
                int *ptr = (int *)ibuf;
                for(int i = 0; i < (BLKSIZE / sizeof(int)); i++){
                    ptr[i] = 0;
                }
                put_block(mip->dev, ip->i_block[13], ibuf);
                ip->i_blocks++;
            }
            int doublebuf[BLKSIZE/sizeof(int)] = {0};
            get_block(mip->dev, ip->i_block[13], (char *)doublebuf);
            doubleblk = doublebuf[lblk/(BLKSIZE / sizeof(int))];

            if(doubleblk == 0){
                doubleblk = doublebuf[lblk/(BLKSIZE / sizeof(int))] = balloc(mip->dev);
                if (doubleblk == 0)
                    return 0;
                get_block(mip->dev, doubleblk, doubly_ibuf);
                int *ptr = (int *)doubly_ibuf;
                for(int i = 0; i < (BLKSIZE / sizeof(int)); i++){
                    ptr[i] = 0;
                }
                put_block(mip->dev, doubleblk, doubly_ibuf);
                ip->i_blocks++;
                put_block(mip->dev, mip->INODE.i_block[13], (char *)doublebuf);
            }

            memset(doublebuf, 0, BLKSIZE / sizeof(int));
            get_block(mip->dev, doubleblk, (char *)doublebuf);
            if (doublebuf[lblk % (BLKSIZE / sizeof(int))] == 0) {
                blk = doublebuf[lblk % (BLKSIZE / sizeof(int))] = balloc(mip->dev);
                if (blk == 0)
                    return 0;
                ip->i_blocks++;
                put_block(mip->dev, doubleblk, (char *)doublebuf);
            }
        }

        char writebuf[BLKSIZE] = { 0 };

        get_block(mip->dev, blk, writebuf);

        char *cp = writebuf + startByte;
        remainder = BLKSIZE - startByte;

        if(remainder <= n_bytes)
        {
            memcpy(cp, cq, remainder);
            cq += remainder;
            cp += remainder;
            oftp->offset += remainder;
            n_bytes -= remainder;
        } else {
            memcpy(cp, cq, n_bytes);
            cq += n_bytes;
            cp += n_bytes;
            oftp->offset += n_bytes;
            n_bytes -= n_bytes;
        }
        if(oftp->offset > mip->INODE.i_size)
            mip->INODE.i_size = oftp->offset;

        put_block(mip->dev, blk, writebuf);
    }

    mip->dirty = 1;
    return n_bytes;
}

//re-write
int func_cat(char *file){
    printf("CAT: running->cwd->ino, address: %d\t%x\n", running->cwd->ino, running->cwd);
    int n;
    char mybuf[BLKSIZE];
    int fd = open_file(filename, READ);
    if (is_invalid_fd(fd)) {
        printf("error, invalid fd to cat\n");
        close_file(fd);
        return -1;
    }
    while (n = myread(fd, mybuf, BLKSIZE)) {
        mybuf[n] = 0;
        char *cp = mybuf;
        while (*cp != '\0') {
            if (*cp == '\n') {
                printf("\n");
            } else {
                printf("%c", *cp);
            }
            cp++;
        }
        //printf("%s", mybuf); // to be fixed
    }
    //putchar('\n');
    close_file(fd);

    printf("END OF CAT: running->cwd->ino, address: %d\t%x\n", running->cwd->ino, running->cwd);
    return 0;
}

//re-write
int func_cp(char* source, char* destination){
    int n = 0;
    char mybuf[BLKSIZE] = {0};
    int fdsrc = open_file(src, READ);
    int fddest = open_file(dest, READ_WRITE);

    printf("fdsrc %d\n", fdsrc);
    printf("fddest %d\n", fddest);

    if (fdsrc == -1 || fddest == -1) {
        if (fddest == -1) close_file(fddest);
        if (fdsrc == -1) close_file(fdsrc);
        return -1;
    }

    memset(mybuf, '\0', BLKSIZE);
    while ( n = myread(fdsrc, mybuf, BLKSIZE)){
        mybuf[n] = 0;
        mywrite(fddest, mybuf, n);
        memset(mybuf, '\0', BLKSIZE);
    }
    close_file(fdsrc);
    close_file(fddest);
    return 0;
}
