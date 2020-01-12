# parent image
FROM alpine:latest

# Install any needed packages specified in requirements.txt
RUN apk update && apk add --no-cache \ 
cmake g++ gfortran libxml2-dev git doxygen boost-dev boost-filesystem wget tar build-base binutils file util-linux bash rsync

RUN apk update && apk add --no-cache procps
#RUN apk update && apk add --no-cache curl && curl -O https://gist.github.com/ashsmith/55098099d2a5b5dfed9935dd4488abd6/raw/9113834d220fca40ed69e53c198a8891a4357d8e/ps_opt_p_enabled_for_alpine.sh \
#  && mv ps_opt_p_enabled_for_alpine.sh /usr/local/bin/ps \
#  && chmod +x /usr/local/bin/ps
