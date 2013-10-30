# mGet - the multi curl downloader
## Requires


* ncurses-devel
* curl-devel

## Tested on
Centos 6.4 x64.

## Build instructions
./build.sh

## Run instructions
./get http://www.domain.com/largefile.zip

This will fetch the file in 5 segments, join the file once it is done