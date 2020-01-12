# parent image
FROM alpine:latest

# Install any needed packages specified in requirements.txt
RUN apk update && apk add --no-cache \ 
cmake g++ gfortran libxml2-dev git doxygen boost-dev boost-filesystem wget tar build-base binutils file util-linux bash

#RUN  wget -qO- "https://cmake.org/files/v3.14/cmake-3.14.3-Linux-x86_64.tar.gz" | tar --strip-components=1 -xz -C /usr/local; echo 'PATH="$PATH:/usr/local/bin"; export PATH; cmake --version' >> /etc/profile


