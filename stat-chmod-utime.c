//
// Created by Jordan Muehlbauer on 11/3/21.
//
#include <sys/stat.h>
#include <libgen.h>
#include <sys/time.h>
#include <ext2fs/ext2_fs.h>
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

int func_stat(char *fileName){

}

int func_chmod(char *path){

}

int func_utime(char *path){
    //Adjust device based on path
    dev = path[0] == '/' ? root->dev : running->cwd->dev;

    //Get ino and check if this is a real entry in our fs
    int ino = getino(path);

    //Validate
    if(ino > 0){

        //Get memory INODE
        MINODE *mip = iget(dev,ino);

        //Adjust time
        mip->INODE.i_atime = time(0L);

        //Mark dirty & update
        mip->dirty = 1;
        iput(mip);
    } else {
        printf("Error: %s is an invalid entry for utime \n",path);
        return -1;
    }
}