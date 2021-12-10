cmake_minimum_required(VERSION 3.15)
project(360Lab5 C)

set(CMAKE_C_STANDARD 11)

add_executable(360Lab5 main.c util.c type.h cd_ls_pwd.c util.h func.h stat-chmod-utime.c mkdir_creat.c rmdir.c link_unlink.c symlink.c open_close.c read_cat.c write_cp.c)