#!/bin/bash
set -ex

if [ -z "${BUILD_DIR}" ]; then
    BUILD_DIR='/build'
fi

export BUILD_DIR
export BUILD_LOG_DIR='/build/log'
mkdir -p ${BUILD_DIR} ${BUILD_LOG_DIR}

phpize && \
./configure --enable-stackdriver-debugger && \
make clean && \
make && \
make test || ((find . -name '*.diff' | xargs cat) && false) && \
make install && \
(composer -V || scripts/install_composer.sh) && \
scripts/run_functional_tests.sh