/****************************************************************************
*                   KCW: mount root file system                             *
*****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <ext2fs/ext2_fs.h>
#include <string.h>
#include <libgen.h>
#include <sys/stat.h>
#include <time.h>

#include "util.h"
#include "type.h"
#include "func.h"

extern MINODE *iget();

MINODE minode[NMINODE];
MINODE *root;
PROC   proc[NPROC], *running;

char gpath[128]; // global for tokenized components
char *name[64];  // assume at most 64 components in pathname
int   n;         // number of component strings

//Mount Information
int fd, dev;
int nblocks, ninodes, bmap, imap, iblk;
char line[128], cmd[32], pathname[128], pathname2[128];

//Mounts
MOUNT mounts[NMOUNT];


int init()
{
  int i, j;
  MINODE *mip;
  PROC   *p;
  MOUNT *mountP;

  printf("init()\n");

  for (i=0; i<NMINODE; i++){
    mip = &minode[i];
    mip->dev = mip->ino = 0;
    mip->refCount = 0;
    mip->mounted = 0;
    mip->mptr = 0;
  }
  for (i=0; i<NPROC; i++){
    p = &proc[i];
    p->pid = i;
    p->uid = p->gid = 0;
    p->cwd = 0;

      for (int k = 0; k < NFD; ++k) {
          p->fd[k] = 0;
      }
    
  }
    for (int i = 0; i < NMOUNT; ++i) {
        mountP = &mounts[i];
        mountP->dev = 0;
    }

  root = 0;
}

// load root INODE and set root pointer to it
int mount_root()
{  
  printf("mount_root()\n");
  root = iget(dev, 2);
}

char *disk = "samples/disk2";

int quit()
{
    int i;
    MINODE *mip;
    for (i=0; i<NMINODE; i++){
        mip = &minode[i];
        if (mip->refCount > 0)
            iput(mip);
    }
    exit(0);
}

int main(int argc, char *argv[ ])
{
  int ino;
  char buf[BLKSIZE];

  printf("checking EXT2 FS ....");
  if ((fd = open(disk, O_RDWR)) < 0){
    printf("open %s failed\n", disk);
    exit(1);
  }

  dev = fd;    // global dev same as this fd   

  /********** read super block  ****************/
  get_block(dev, 1, buf);
  sp = (SUPER *)buf;

  /* verify it's an ext2 file system ***********/
  if (sp->s_magic != 0xEF53){
      printf("magic = %x is not an ext2 filesystem\n", sp->s_magic);
      exit(1);
  }     
  printf("EXT2 FS OK\n");
  ninodes = sp->s_inodes_count;
  nblocks = sp->s_blocks_count;

  get_block(dev, 2, buf); 
  gp = (GD *)buf;

  bmap = gp->bg_block_bitmap;
  imap = gp->bg_inode_bitmap;
  iblk = gp->bg_inode_table;
  printf("bmp=%d imap=%d inode_start = %d\n", bmap, imap, iblk);

  init();  
  mount_root();
  printf("root refCount = %d\n", root->refCount);

  printf("creating P0 as running process\n");
  running = &proc[0];
  running->status = READY;
  running->cwd = iget(dev, 2);
  printf("root refCount = %d\n", root->refCount);

  //Ensure p1 is a user process & we have a circular relationship
  proc[1].uid = 1;

  proc[0].next = &proc[1];
  proc[1].next = &proc[0];

  
  while(1){
    printf("input command : [ls|cd|pwd|quit] ");
    fgets(line, 128, stdin);
    line[strlen(line)-1] = 0;



    if (line[0]==0)
       continue;
    pathname[0] = 0;

    sscanf(line, "%s %s", cmd, pathname);
    printf("cmd=%s pathname=%s\n", cmd, pathname);
  
    if (strcmp(cmd, "ls")==0)
       ls(pathname);
    else if (strcmp(cmd, "cd")==0)
       cd(pathname);
    else if (strcmp(cmd, "pwd")==0)
       pwd(running->cwd);
    else if (strcmp(cmd, "mkdir") == 0)
        func_mkdir(pathname);
    else if (strcmp(cmd, "rmdir") == 0)
        func_rmdir(pathname);
    else if (strcmp(cmd, "creat") == 0)
        func_creat(pathname);
    else if (strcmp(cmd, "link") == 0){
        sscanf(line,"%s %s %s",cmd, pathname,pathname2);
        func_link(pathname,pathname2);
    }
    else if (strcmp(cmd, "unlink") == 0){
        func_unlink(pathname);
    }
    else if (strcmp(cmd, "symlink") == 0){
        sscanf(line,"%s %s %s",cmd, pathname,pathname2);
        func_symlink(pathname,pathname2);
    }
    else if (strcmp(cmd, "readlink") == 0){
        func_readlink(pathname);
    }
    else if (strcmp(cmd, "utime") == 0)func_utime(pathname);
    else if (strcmp(cmd, "open") == 0){
        int mode = -1;
        sscanf(line, "%s %s %d",cmd,pathname,&mode);
        func_open(pathname,mode);
    }
    else if (strcmp(cmd, "write") == 0){
        func_write_cmd();
    }
    else if (strcmp(cmd, "pfd") == 0)func_pfd();
    else if (strcmp(cmd, "cat") == 0)func_cat(pathname);
    else if (strcmp(cmd, "close") == 0){
        int fd = -1;
        sscanf(line, "%s %d",cmd,&fd);
        func_close(fd);
    }
    else if (strcmp(cmd, "cp") == 0){
        sscanf(line,"%s %s %s",cmd, pathname,pathname2);
        func_cp(pathname,pathname2);
    }
    else if (strcmp(cmd, "quit")==0){
        quit();
        break;
    }
    else if (strcmp(cmd,"switch") == 0)func_switch();
    else if (strcmp(cmd,"mount")==0){
        sscanf(line,"%s %s %s",cmd,pathname,pathname2);
        func_mount(pathname,pathname2);
    }

  }
}


