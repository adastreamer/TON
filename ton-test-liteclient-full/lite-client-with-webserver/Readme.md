# Ton client with webserver
 Allows to get ton client data log to webserver

## Requirements
 - c++14 compiler
 - boost 1.69!!, not 1.70
 - OpenSSL
## How-to
 - First step get and install boost
 ```sh 
 wget https://sourceforge.net/projects/boost/files/boost/1.69.0/boost_1_69_0.tar.gz/download
 tar xzvf 
 cd boost
 ./bootstrap.sh --prefix=/usr/local
 #allow to see value of cores allow to install boost parallel
 cpuCores=`cat /proc/cpuinfo | grep "cpu cores" | uniq | awk '{print $NF}'`
 sudo ./b2 --with=all -j $cpuCores install 
 #allow to see installed boost version
 cat /usr/local/include/boost/version.hpp | grep "BOOST_LIB_VERSION"
 ```
 - cd ${YOUR_PATH}/lite-client-with-server
 - mkdir build && cd build
 - cmake .. -G "Unix Makefiles" -DOPENSSL_ROOT_DIR=/usr/local/ssl -DOPENSSL_LIBRARIES=/usr/local/ssl/lib
 - make
 - copy .json config to executable
 ### Run
 - ./test-lite-client-with-webserver -C .json config