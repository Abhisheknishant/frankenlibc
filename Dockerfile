# Basic build of frankenlibc with Docker

FROM ubuntu:15.04

MAINTAINER Justin Cormack

RUN apt-get update
RUN apt-get install -y build-essential libseccomp2 libseccomp-dev

COPY . /usr/src/frankenlibc
WORKDIR /usr/src/frankenlibc

RUN ./build.sh -d /usr/local seccomp notests
RUN /usr/local/bin/rumprun-cc -g -O2 tests/hello.c -o /usr/local/bin/rump-helloworld
