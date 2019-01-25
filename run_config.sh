#!/bin/bash

./config/configure.py \
--with-shared=1 --with-x=0 --with-mpi=1 \
--with-mpi-dir=/usr/local/openmpi \
--with-valgrind-dir=/home/pabeata/software/valgrind/install \
--COPTFLAGS=”-O3” \
--CXXOPTFLAGS=”-O3” \
--FOPTFLAGS=”-O3” \
--download-fblaslapack \
--download-hypre

# Enter these commands manually after running this script:
# make PETSC_DIR=/home/pabeata/software/petsc PETSC_ARCH=x86_64 all
# make PETSC_DIR=/home/pabeata/software/petsc PETSC_ARCH=x86_64 check

