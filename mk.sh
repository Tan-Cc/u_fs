fusermount -u 123
mkdir 123
rm hello
rm disk
dd bs=1K count=5K if=/dev/zero of=disk
make
./hello 123
