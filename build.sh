export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-
make LOCALVERSION= -j8 Image modules dtbs
