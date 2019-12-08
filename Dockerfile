FROM ubuntu:16.04 as builder

LABEL maintainer="Mate Soos"
LABEL version="5.0"
LABEL Description="An advanced SAT solver"

# get curl, etc
RUN apt-get update
RUN apt-get install --no-install-recommends -y software-properties-common
RUN add-apt-repository -y ppa:ubuntu-toolchain-r/test
RUN apt-get update
RUN apt-get install --no-install-recommends -y libboost-program-options-dev gcc g++ make cmake zlib1g-dev wget python3 python3-setuptools python3-dev

# set up build env
RUN groupadd -r solver -g 433
RUN useradd -u 431 -r -g solver -d /home/solver -s /sbin/nologin -c "Docker image user" solver
RUN mkdir -p /home/solver/cms
RUN mkdir -p /home/solver/cms/src
RUN mkdir -p /home/solver/cms/cmake
RUN chown -R solver:solver /home/solver

# build CMS
USER root
COPY src /home/solver/cms/src
COPY cmake /home/solver/cms/cmake
COPY CMakeLists.txt /home/solver/cms/
COPY cryptominisat5Config.cmake.in /home/solver/cms/
WORKDIR /home/solver/cms
RUN mkdir build
WORKDIR /home/solver/cms/build
RUN cmake -DENABLE_PYTHON_INTERFACE=OFF ..
RUN make -j6 VERBOSE=1
RUN make install

# set up for running
FROM ubuntu:16.04
RUN apt-get update && apt-get install --no-install-recommends -y libboost-program-options1.58.0 libpython3.5 && rm -rf /var/lib/apt/lists/*
COPY --from=builder /usr/local/bin/cryptominisat5 /usr/bin/
COPY --from=builder /usr/local/lib/libcryptominisat5.so.5.6 /usr/lib/
COPY --from=builder /usr/local/lib/libcryptominisat5.so /usr/lib/
#ENTRYPOINT ["/usr/bin/cryptominisat5"]

# --------------------
# HOW TO USE
# --------------------
# on file through STDIN:
#    zcat mizh-md5-47-3.cnf.gz | docker run --rm -i -a stdin -a stdout cms

# on a file:
#    docker run --rm -v `pwd`/myfile.cnf.gz:/in cms in

# echo through STDIN:
#    echo "1 2 0" | docker run --rm -i -a stdin -a stdout cms

# hand-written CNF:
#    docker run --rm -ti -a stdin -a stdout cms

