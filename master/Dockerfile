FROM ubuntu:latest
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    iproute2 \
    iputils-ping \
    cmake \
    make \
    g++ \
    build-essential

WORKDIR /usr/src/app

COPY . .

RUN cmake -S . -B build && cmake --build build

# RUN tc qdisc add dev eth0 root netem delay 300ms
# RUN tc qdisc change dev eth0 root netem loss 30%
# tc qdisc change dev eth0 root netem duplicate 30%

CMD ["./build/master"]
