# Set the base image to Ubuntu
FROM ubuntu

USER root
WORKDIR /home/app

RUN apt-get update
RUN apt-get -y install curl gnupg git python3-pip

RUN apt-get install -y gcc g++ make
RUN apt-get install -y can-utils
#RUN apt-get install -y libnode-dev
#RUN apt-get install linux-modules-extra-$(uname -r)

RUN curl -sL https://deb.nodesource.com/setup_20.x | bash -
RUN apt-get -y install nodejs