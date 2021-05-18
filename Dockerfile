# Copyright 2017 OpenCensus Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

ARG BASE_IMAGE
FROM $BASE_IMAGE
ARG GOOGLE_CREDENTIALS_BASE64
ARG CLOUDSDK_ACTIVE_CONFIG_NAME
ARG GOOGLE_PROJECT_ID
ARG PHP_DOCKER_GOOGLE_CREDENTIALS

RUN mkdir -p /build && \
    apt-get update -y && \
    apt-get install -y -q --no-install-recommends \
        apt-transport-https \
        build-essential \
        ca-certificates \
        g++ \
        gcc \
        gnupg \
        libc-dev \
        make \
        autoconf \
        curl \
        git-core \
        nano \
        valgrind \
        unzip

RUN echo "deb [signed-by=/usr/share/keyrings/cloud.google.gpg] http://packages.cloud.google.com/apt cloud-sdk main" | tee -a /etc/apt/sources.list.d/google-cloud-sdk.list && curl https://packages.cloud.google.com/apt/doc/apt-key.gpg | apt-key --keyring /usr/share/keyrings/cloud.google.gpg  add - && apt-get update -y && apt-get install google-cloud-sdk -y

COPY . /build/

WORKDIR /build
RUN chmod 0755 /build/build.sh

ENV GOOGLE_CREDENTIALS_BASE64=${GOOGLE_CREDENTIALS_BASE64:-}
ENV CLOUDSDK_ACTIVE_CONFIG_NAME=${CLOUDSDK_ACTIVE_CONFIG_NAME:-default}
ENV GOOGLE_PROJECT_ID=${GOOGLE_PROJECT_ID:-google-cloud}
ENV PHP_DOCKER_GOOGLE_CREDENTIALS=${PHP_DOCKER_GOOGLE_CREDENTIALS:-/build/gcp-creds.json}
ENV GOOGLE_APPLICATION_CREDENTIALS=${PHP_DOCKER_GOOGLE_CREDENTIALS}
RUN /build/scripts/install_test_dependencies.sh

ENV TEST_PHP_ARGS="-q" \
    REPORT_EXIT_STATUS=1

RUN /build/build.sh
#ENTRYPOINT [ "/bin/bash" ]
