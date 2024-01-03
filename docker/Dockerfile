FROM ubuntu:jammy-20220531
RUN apt update
RUN DEBIAN_FRONTEND=noninteractive apt install -y cmake git g++ doxygen bash rsync curl clang ccache libboost-all-dev libxml2-dev texlive-font-utils openssh-server procps

RUN  apt update && apt install -y wget && wget -nv https://repo.anaconda.com/miniconda/Miniconda3-latest-Linux-x86_64.sh -O /tmp/miniconda-installer.sh && bash /tmp/miniconda-installer.sh -b -p /opt/conda

RUN  . /opt/conda/etc/profile.d/conda.sh && conda create -c conda-forge -n pysjef -y python numpy boost-cpp pugixml cython lxml pip regex c-compiler cxx-compiler ghostscript pytest cmake make && conda activate pysjef

RUN ( \
    echo 'LogLevel DEBUG2'; \
    echo 'PermitRootLogin yes'; \
    echo 'PasswordAuthentication yes'; \
    echo 'Subsystem sftp /usr/lib/openssh/sftp-server'; \
  ) > /etc/ssh/sshd_config \
  && mkdir /run/sshd

RUN sed -i s/#PermitRootLogin.*/PermitRootLogin\ yes/ /etc/ssh/sshd_config \
  && echo "root:root" | chpasswd \
  && rm -rf /var/cache/apk/*
RUN mkdir -p /root/.ssh \
&& echo "Host *" >> /root/.ssh/config \
&& echo "StrictHostKeyChecking no" >> /root/.ssh/config
RUN sed -ie 's/#Port 22/Port 22/g' /etc/ssh/sshd_config
RUN sed -ri 's/#HostKey \/etc\/ssh\/ssh_host_key/HostKey \/etc\/ssh\/ssh_host_key/g' /etc/ssh/sshd_config
RUN sed -ir 's/#HostKey \/etc\/ssh\/ssh_host_rsa_key/HostKey \/etc\/ssh\/ssh_host_rsa_key/g' /etc/ssh/sshd_config
RUN sed -ir 's/#HostKey \/etc\/ssh\/ssh_host_dsa_key/HostKey \/etc\/ssh\/ssh_host_dsa_key/g' /etc/ssh/sshd_config
RUN sed -ir 's/#HostKey \/etc\/ssh\/ssh_host_ecdsa_key/HostKey \/etc\/ssh\/ssh_host_ecdsa_key/g' /etc/ssh/sshd_config
RUN sed -ir 's/#HostKey \/etc\/ssh\/ssh_host_ed25519_key/HostKey \/etc\/ssh\/ssh_host_ed25519_key/g' /etc/ssh/sshd_config
RUN /usr/bin/ssh-keygen -A
RUN ssh-keygen -t rsa -b 4096 -f  /etc/ssh/ssh_host_key
RUN ssh-keygen -f /root/.ssh/id_rsa -N '' && cp /root/.ssh/id_rsa.pub /root/.ssh/authorized_keys
EXPOSE 22
ENTRYPOINT /usr/sbin/sshd && /bin/bash