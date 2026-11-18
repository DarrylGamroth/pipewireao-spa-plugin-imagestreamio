# PipeWireAO ImageStreamIO plugin

[![CI](https://github.com/DarrylGamroth/pipewireao-spa-plugin-imagestreamio/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/DarrylGamroth/pipewireao-spa-plugin-imagestreamio/actions/workflows/ci.yml)
[![Coverage](https://codecov.io/github/DarrylGamroth/pipewireao-spa-plugin-imagestreamio/branch/main/graph/badge.svg)](https://app.codecov.io/github/DarrylGamroth/pipewireao-spa-plugin-imagestreamio)

This repository provides `api.imagestreamio.source` and
`api.imagestreamio.sink`, which bridge PipeWireAO ndarray buffers to milk
ImageStreamIO shared-memory streams. Include it only in deployments that
exchange data through ImageStreamIO.

The repository is an independent build unit. It depends on PipeWireAO, the
public headers installed by
[`pipewireao-spa-plugins-core`](https://github.com/DarrylGamroth/pipewireao-spa-plugins-core),
and ImageStreamIO.

## Build and stage

```console
meson setup build --prefix=/usr \
  -Dimagestreamio=enabled \
  -Dimagestreamio-prefix=/opt/ImageStreamIO
meson compile -C build
meson test -C build 'spa-imagestreamio*' --print-errorlogs
DESTDIR="$PWD/stage" meson install -C build
```

The default ImageStreamIO prefix is `/usr/local`. See
[`spa/plugins/imagestreamio/README.md`](spa/plugins/imagestreamio/README.md) for
the array mapping, shared-memory ownership, and factory properties.

## Container and package recipes

The Docker Bake file offers `debian-13-deploy` and `ubuntu-26-04-deploy` image
targets. `debian-13-package` and `ubuntu-26-04-package` export `.deb` files when
that deployment format is wanted. The supplied image recipes use the package
artifact internally; Meson builds are not restricted to Debian or Ubuntu.

The container build must be given authorized sources for PipeWireAO, the core
development files, and ImageStreamIO through its build arguments or BuildKit
secrets.

## CI and coverage

GitHub Actions builds and tests against PipeWireAO 1.7.3 on Debian 13.
The SPA factory, shared-memory stream, and source/sink tests with a local
ImageStreamIO installation.

CI requires more than 90% line coverage for the code compiled in this
configuration. The workflow saves the LCOV report as an artifact and uses
GitHub OIDC to upload coverage to Codecov. Fork pull requests run the
same local gate.

With the workflow dependencies installed, run the same native tests locally.

```sh
CC=gcc CXX=g++ meson setup build-coverage -Dtests=enabled -Db_coverage=true \
  -Dimagestreamio=enabled -Dimagestreamio-prefix=/path/to/imagestreamio
meson test -C build-coverage --print-errorlogs
```

Collect a native coverage report from that build.

```sh
gcovr --root . --lcov build-coverage/coverage-c.info --print-summary build-coverage
```
