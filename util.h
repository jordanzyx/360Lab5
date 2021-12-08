//
// Created by Jordan Muehlbauer on 10/27/21.
//

#ifndef INC_360LAB5_UTIL_H
#define INC_360LAB5_UTIL_H

#include "type.h"

int get_block(int dev, int blk, char *buf);
int put_block(int dev, int blk, char *buf);
int tokenize(char *pathname);
MINODE *iget(int dev, int ino);
void iput(MINODE *mip);
int search(MINODE *mip, char *name);
int getino(char *pathname);
int findmyname(MINODE *parent, u32 myino, char myname[]);
int findino(MINODE *mip, u32 *myino);
int balloc(int dev);
int ialloc(int dev);
int decFreeBlocks(int dev);
int decFreeInodes(int dev);
int incFreeInodes(int dev);
int incFreeBlocks(int dev);
int tst_bit(char *buf, int bitnum);
int set_bit(char *buf, int bitnum);
int clr_bit(char *buf, int bitnum);
int enter_name(MINODE *pip, int myino, char *myname);
int bdealloc(int dev, int bno);
int idealloc(int dev, int ino);
int rm_child(MINODE *parent, char *name);
int freeINodes(MINODE *mip);
int func_access(char *filename, char mode);

#endif //INC_360LAB5_UTIL_H
