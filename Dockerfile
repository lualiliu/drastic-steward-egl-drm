FROM debian

RUN apt-get update
RUN apt-get install build-essential make cmake wget autogen autoconf automake -y
COPY toolchain.tar.gz /root/toolchain.tar.gz
RUN tar xvf /root/toolchain.tar.gz
RUN mv /mmiyoo /opt
RUN mv /prebuilt /opt
RUN export PATH=/opt/mmiyoo/bin/:$PATH
