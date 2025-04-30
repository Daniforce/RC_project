FROM ubuntu:20.04
RUN apt-get update && apt-get install -y nmap iputils-ping gcc gdb make net-tools traceroute netcat

RUN chmod 777 /home

RUN mkdir -p /gns3volumes/home

COPY ./ /gns3volumes/home

WORKDIR /gns3volumes/home/Projeto
