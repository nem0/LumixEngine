git clone https://github.com/bkaradzic/genie
cd genie
make
cp bin/linux/genie /usr/local/bin/genie_linux
cd ..
rm -rf genie
genie_linux --static-plugins --no-unit-tests --gcc=linux-gcc-5 gmake
