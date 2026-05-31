# Linux test environment for cc89: builds and runs the compiler against gcc.
# The repo is bind-mounted at /work (see tt.ps1 / the git hooks), so source edits
# are live and all build/test artifacts land back on the host filesystem.
#
#   docker build -t cc89-test .
#   docker run --rm -v "<repo>:/work" cc89-test run 9
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends \
        g++ cmake make gcc libc6-dev python3 ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
# clang-format is intentionally absent: CMakeLists gates its format target on
# find_program(), so without the binary the container build never reformats the
# host src/ tree.
ENTRYPOINT ["python3", "tools/tt.py"]
