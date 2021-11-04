//
// Created by Jordan Muehlbauer on 10/27/21.
//

#ifndef INC_360LAB5_FUNC_H
#define INC_360LAB5_FUNC_H

#include "type.h"

//cd_ls_pwd.c
int cd(char *pathname);
int ls(char *path);
char *pwd(MINODE *wd);

//mkdir-creat-rmdir.c
int func_mkdir(char *pathname);
int func_creat(char *pathname);
int func_rmdir(char *pathname);

//links.c
int func_link(char *old, char *new);
int func_unlink(char *pathname);
int func_symlink(char *old, char *new);
int func_readlink(char *file);

//stat-chmod-utime.c
int func_stat(char *fileName);
int func_chmod(char *path);
int func_utime(char *path);


#endif //INC_360LAB5_FUNC_H
