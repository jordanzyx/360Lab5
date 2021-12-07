cmake_minimum_required(VERSION 3.15)
project(360Lab5 C)

set(CMAKE_C_STANDARD 11)

add_executable(360Lab5 main.c util.c type.h cd_ls_pwd.c util.h func.h mkdir_creat_rmdir.c links.c stat-chmod-utime.c open-close-lseek.c read-write-cat-copy.c mounts.c)