# wlr-idle-proxy

A simple org.freedesktop.ScreenSaver implementation for Wayland compositors that support idle-inhibit-unstable-v1.
This allows software that inhibits screen locking over DBus to transparently inhibit idling on these compositors.

## build

```
meson build
ninja -C build
```

## usage

Just run the output binary in `build`.
