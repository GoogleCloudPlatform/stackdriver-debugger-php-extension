#!/usr/bin/env bash
# Copyright 2017 Google Inc.
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

# A script for installing necessary software on CI systems.

set -ex

if [ -z "${CLOUDSDK_ACTIVE_CONFIG_NAME}" ]; then
    echo "You need to set CLOUDSDK_ACTIVE_CONFIG_NAME envvar."
    exit 1
fi

if [ -z "${GOOGLE_PROJECT_ID}" ]; then
    echo "You need to set GOOGLE_PROJECT_ID envvar."
    exit 1
fi

if [ -z "${CLOUDSDK_VERBOSITY}" ]; then
    CLOUDSDK_VERBOSITY='none'
fi

# gcloud configurations
gcloud config configurations create ${CLOUDSDK_ACTIVE_CONFIG_NAME} || /bin/true # ignore failure
gcloud config set project ${GOOGLE_PROJECT_ID}
gcloud config set app/promote_by_default false
gcloud config set verbosity ${CLOUDSDK_VERBOSITY}

# Dump the credentials from the environment variable.
php scripts/dump_credentials.php

# Set the timeout
gcloud config set container/build_timeout 3600

if [ ! -f "${PHP_DOCKER_GOOGLE_CREDENTIALS}" ]; then
    echo 'Please set PHP_DOCKER_GOOGLE_CREDENTIALS envvar.'
    exit 1
fi

# Use the service account for gcloud operations.
gcloud auth activate-service-account \
    --key-file "${PHP_DOCKER_GOOGLE_CREDENTIALS}"

# https://cloud.google.com/sdk/docs/install
# Note: Updating and removing components using gcloud components is disabled
# if you installed Cloud SDK using apt-get or yum.
#
# To manage the Cloud SDK in this case, continue using the package management tool
# used during installation.
