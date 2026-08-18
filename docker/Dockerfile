FROM continuumio/miniconda3:v25.11.1

RUN . /opt/conda/etc/profile.d/conda.sh && conda install -y conda-anaconda-tos && conda tos accept --override-channels --channel https://repo.anaconda.com/pkgs/main \
 && conda tos accept --override-channels --channel https://repo.anaconda.com/pkgs/r

RUN  . /opt/conda/etc/profile.d/conda.sh && conda install -c conda-forge -y python 'numpy>=1.17' 'libboost-devel>=1.90' 'pugixml>=1.10' 'cython>=3.0' 'lxml>=4.0' pip regex c-compiler cxx-compiler ghostscript 'pytest>=7' 'cmake>=3.27' 'make>=4' scipy

# conda-forge's c-compiler/cxx-compiler only provide an arch-prefixed ar
# (e.g. x86_64-conda-linux-gnu-ar), not a plain `ar` on PATH, and don't set
# $AR either. CMake's compiler-id detection can't infer the prefix from an
# unprefixed `gcc`/`cc`, so CMAKE_AR ends up NOTFOUND and linking any static
# library (e.g. the bundled libyaml) fails. Symlink whatever ar gcc itself
# resolves to, which is correct for whichever architecture this is built on.
RUN . /opt/conda/etc/profile.d/conda.sh && conda activate base \
 && ln -sf "$(gcc -print-prog-name=ar)" /opt/conda/bin/ar

ENV CONDA_PREFIX=/opt/conda

RUN apt-get update

RUN DEBIAN_FRONTEND=noninteractive apt install -y bash rsync curl openssh-server procps doxygen

RUN ( \
    echo 'LogLevel DEBUG2'; \
    echo 'PermitRootLogin yes'; \
    echo 'PasswordAuthentication yes'; \
    echo 'Subsystem sftp /usr/lib/openssh/sftp-server'; \
  ) > /etc/ssh/sshd_config \
  && mkdir -p /run/sshd

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
#ENTRYPOINT /usr/sbin/sshd && /bin/bash
ENTRYPOINT ["/usr/sbin/sshd"]