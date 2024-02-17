# Set the base image to Ubuntu
FROM ubuntu

USER root
WORKDIR /home/app

RUN apt-get update
RUN apt-get -y install curl gnupg

RUN curl -sL https://deb.nodesource.com/setup_18.x | bash -
RUN apt-get -y install nodejs

RUN apt-get install -y gcc g++ make
RUN apt-get install -y can-utils
#RUN apt-get install -y libnode-dev
#RUN apt-get install linux-modules-extra-$(uname -r)