FROM debian:bullseye

RUN apt-get update && \
    apt-get install -y gcc make iputils-ping net-tools

WORKDIR /powerudp

COPY . .

RUN make

CMD ["bash"]
