#!/bin/sh

autoreconf --install || {
 echo 'autogen.sh failed';
 exit 1;
}

# ./configure --host target=arm-linux-gnueabi || {
./configure || {
 echo 'configure failed';
 exit 1;
}

echo
echo "Now type 'make' to compile this module."
echo
