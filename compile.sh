cd a11ul-3.10.28-g72f7e8d
export TOP=/root
export PATH=$TOP/arm-eabi-4.6/bin:$PATH
export ARCH=arm
export SUBARCH=arm
export CROSS_COMPILE=arm-eabi-

make a11ul_defconfig
make clean  
make -j2