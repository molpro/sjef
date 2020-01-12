# parent image
FROM alpine:latest

# Install any needed packages specified in requirements.txt
RUN apk update && apk add --no-cache \ 
cmake g++ gfortran libxml2-dev git doxygen boost-dev boost-filesystem wget tar build-base binutils file util-linux bash rsync

