#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <ext2fs/ext2_fs.h>
#include <string.h>
#include <libgen.h>
#include <sys/stat.h>
#include "unistd.h"
#include <time.h>

#include "func.h"
#include "util.h"

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

/************* cd_ls_pwd.c file **************/
int cd(char *pathname)
{
  printf("cd: under construction READ textbook!!!!\n");

  //Get the ino
  int ino = getino(pathname);

  //Confirm ino is valid
  if(ino != -1){

      //Get MINODE
      MINODE *node = iget(dev,ino);

      //Confirm node is directory
      if(S_ISDIR(node->INODE.i_mode)){
          //Check permissions
          if (func_access(pathname,'x') != 1){
              printf("Error: You do not have permission to view this directory\n");
              return -1;
          }

          //Change directory
          iput(running->cwd);
          running->cwd = node;

          //return success
          return 1;
      } else {
          printf("error: node found is not directory");
          return  0;
      }

  } else {
      printf("error: ino cannot be found from cd()");
      return 0;
  }

}

int ls_file(MINODE *mip, char *name)
{
    //Get what type of file/node we are looking at
    u16 type = mip->INODE.i_mode;

    //Print out the type for this file (dir/reg/lnk)
    if(S_ISDIR(type))printf("d");
    if(S_ISREG(type))printf("-");
    if(S_ISLNK(type))printf("l");

    //Create strings to use while printing out the permissions for
    char *t1 = "xwrxwrxwr-------";

    for (int i = 8; i >= 0 ; --i) {
        if(type & (1 << i))printf("%c",t1[i]);
        else printf("%c", '-');
    }

    //Print out basic information now
    //link amount
    printf("%4d ",mip->INODE.i_links_count);
    //user gid
    printf("%4d ",mip->INODE.i_gid);
    //uid
    printf("%4d ",mip->INODE.i_uid);
    //file size
    printf("%8d ",mip->INODE.i_size);

    //Create string to store filetime
    char fileTime[64];

    //get the time for the file
    time_t *time = &(mip->INODE.i_mtime);

    //Get string into human form
    strcpy(fileTime, ctime(time));

    //Remove the newline at end
    fileTime[strlen(fileTime) - 1] = 0;

    //Print file time
    printf("%s ",fileTime);

    //Print file name
    printf("%s ",name);

    //Print the linked name if we need too
    if(S_ISLNK(type)){
        char buf[BLKSIZE];
        get_block(dev,mip->INODE.i_block[0],buf);
        printf(" -> %s",buf);
    }

    //Print newline
    printf("\n");
}

int ls_dir(MINODE *mip)
{

  char buf[BLKSIZE], temp[256];
  DIR *dp;
  char *cp;

  get_block(dev, mip->INODE.i_block[0], buf);
  dp = (DIR *)buf;
  cp = buf;
  
  while (cp < buf + BLKSIZE){
     strncpy(temp, dp->name, dp->name_len);
     temp[dp->name_len] = 0;

     //Get the node we are going to display
     MINODE *item = iget(dev,dp->inode);

     //Display node
     ls_file(item,temp);

     //Set to dirty & re-insert
     item->dirty = 1;
     iput(item);

     cp += dp->rec_len;
     dp = (DIR *)cp;
  }
  printf("\n");
}

int ls(char *path)
{
    //Handle ls for cwd
    if (strlen(path) == 0){
        ls_dir(running->cwd);
        return 0;
    }

    //Handle when we are actually given a path to try and ls into
    int ino = getino(pathname);

    //If the path cannot be found exit out
    if(ino == -1){
        printf("Invalid INODE\n");
        return -1;
    }

    //Continue with ease
    dev = root->dev;
    MINODE *mip = iget(dev,ino);

    //Store type so we can see if we are working with a directory or file
    int type = mip->INODE.i_mode;

    //Handle directory
    if(S_ISDIR(type))ls_dir(mip);
    else ls_file(mip,basename(path));

    //Re-establish node
    iput(mip);
}

void pwd_recur(MINODE *node){
    //Make sure the node is not root
    if(node != root){
        //Used later when we are finding the parent ino we store the current here
        int ino = 0;

        //Create buffer we use when we lseek & read
        char buffer[BLKSIZE];

        //Create a string to store the name of the directory we are printing
        char dirString[1024];

        //Get memory block
        get_block(dev, node->INODE.i_block[0],buffer);

        //Store the parent ino for later so we can retrieve the parent minode
        int pINO = findino(node, &ino);

        //Get the parent node
        MINODE *parentNode = iget(dev,pINO);

        //Store the name of the current node
        findmyname(parentNode,ino,dirString);

        //Call the same thing on the parent so we end up printing from the top down
        pwd_recur(parentNode);

        //Set the nparent node to dirty
        parentNode->dirty = 1;

        //Write node back 2 disk
        iput(parentNode);

        //Print
        printf("/%s",dirString);
    }
}

char* pwd(MINODE *wd)
{
  printf("pwd: READ HOW TO pwd in textbook!!!!\n");

  //If root just print out /
  if (wd == root){
    printf("/\n");
  }

  //Handle anything else by recursively going and printing each dir that is chained
  else {
      //Print recursively
      pwd_recur(wd);

      //Add new line after
      printf("\n");
  }
}



