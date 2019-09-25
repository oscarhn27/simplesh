#!/bin/bash

[[ -f simplesh ]] || { echo "No existe el binario simplesh"; exit 1; }
[[ -f libreadline.supp ]] || { echo "No existe el fichero libreadline.supp"; exit 1; }

valgrind --leak-check=full --show-leak-kinds=all --suppressions=libreadline.supp --trace-children=no --child-silent-after-fork=yes -v ./simplesh 2> valgrind.out
