FROM ubuntu:20.04
RUN apt-get update && apt-get install -y nmap iputils-ping gcc gdb make net-tools traceroute netcat

RUN chmod 777 /home

RUN mkdir -p /gns3volumes/home/Projeto

COPY ./ /gns3volumes/home/Projeto

WORKDIR /gns3volumes/home/Projeto

RUN gcc -o Projeto_client Projeto_client.c
RUN gcc -o Projeto_serv Projeto_serv.c
