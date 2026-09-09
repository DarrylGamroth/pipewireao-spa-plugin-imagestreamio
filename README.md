# PipeWireAO ImageStreamIO plugin

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
