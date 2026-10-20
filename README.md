# SteamVR overlay depth repro

A minimal program that reproduces a SteamVR compositor problem. When two overlays overlap and the nearer overlay is drawn first, its transparent and translucent texels hide the farther overlay, and the game shows through.

It looks as if the compositor writes depth for every texel of an overlay quad, including fully transparent ones.

This is easy to trigger with overlays that have the same sort order. They are documented to be rendered back to front based on distance from the HMD, but in the "edge" layout below, the nearer overlay was drawn first. Measuring the distance to each overlay's center would explain this, and so would measuring it to the nearest point of each overlay. We have not verified how SteamVR measures the distance.

The program uses only OpenVR and Direct3D 11 (one source file, `main.cpp`).

## What it shows

Two overlays are attached to the HMD, facing the user:

- **Back**: an opaque blue square.
- **Front**: 3 cm closer. Either a white circle whose corners outside the circle are fully transparent, or a white square that is translucent.

All overlays are flat and use premultiplied `R8G8B8A8_UNORM` textures with `VROverlayFlags_IsPremultiplied`. Positions are in meters relative to the HMD (-z is forward):

- **centered**: back at (0, 0, -1.00), width 0.5; front at (0, 0, -0.97), width 0.3.
- **edge**: back at (0, 0, -1.00), width 1.2; front at (0.45, 0, -0.97), width 0.3. The front overlay is 3 cm nearer, but its center is farther from the HMD (about 1.07 m) than the back overlay's center (1.00 m). Its nearest point (about 1.015 m) is also farther than the back overlay's nearest point (1.00 m).

Each stage is shown for 8 seconds. In every stage, the correct result is that the blue overlay is visible behind the white overlay.

| Stage | Front overlay | Layout | Sort order (back / front) | Observed |
|---|---|---|---|---|
| A1 | circle, transparent corners | centered | 0 / 0 | correct |
| A2 | circle, transparent corners | centered | 1 / 0 (front drawn first) | game visible in the corners |
| A3 | circle, transparent corners | edge | 0 / 0 | game visible in the corners |
| A4 | circle, transparent corners | edge | 0 / 1 (back drawn first) | correct |
| B1 | white, `SetOverlayAlpha(0.5)` | centered | 0 / 0 | correct |
| B2 | white, `SetOverlayAlpha(0.5)` | centered | 0 / 1 (back drawn first) | correct |
| B3 | white, `SetOverlayAlpha(0.5)` | centered | 1 / 0 (front drawn first) | game visible through the white square |
| C1 | white, texel alpha 0.5 | centered | 0 / 0 | correct |
| C2 | white, texel alpha 0.5 | centered | 0 / 1 (back drawn first) | correct |
| C3 | white, texel alpha 0.5 | centered | 1 / 0 (front drawn first) | game visible through the white square |
| D1 | white, `SetOverlayAlpha(0.5)` | edge | 0 / 0 | game visible through the white square |
| D2 | white, `SetOverlayAlpha(0.5)` | edge | 0 / 1 (back drawn first) | correct |
| D3 | white, `SetOverlayAlpha(0.5)` | edge | 1 / 0 (front drawn first) | game visible through the white square |

The results of A1 to A4 were observed with an earlier version of this test (same layout and sort orders) in the original application. The results of B1 to D3 were observed with this program.

In A2, if overlays were simply blended in sort order, the blue overlay (drawn last) would cover the circle. Instead, the circle stays visible and its transparent corners cut holes in the blue overlay.

## Environment where it was observed

- SteamVR 2.17.10
- Meta Quest 2 via Virtual Desktop 1.34.22
- NVIDIA GeForce RTX 3070, driver 610.88
- Windows 11 Home (build 26200)
- Game: VRChat

## Build

### MSVC

1. Get the OpenVR SDK: `git clone https://github.com/ValveSoftware/openvr`
2. Build:

```
cmake -S . -B build -DOPENVR_SDK_DIR=C:/path/to/openvr
cmake --build build --config Release
```

`openvr_api.dll` is copied next to the executable.

### MSYS2 (UCRT64, MinGW-w64)

Install the package `mingw-w64-ucrt-x86_64-openvr`, then:

```
cmake -S . -B build -G Ninja
cmake --build build
```

With MinGW-w64, the program includes `openvr_mingw.hpp` instead of `openvr.h`, because some calls do not work correctly with `openvr.h` under MinGW-w64.

## Run

Start SteamVR and a game, then run `overlay_depth_repro.exe`. The console prints the name of each stage and where to look. The program does not change any SteamVR settings. It destroys its overlays after each stage and calls `VR_Shutdown` at the end.

If SteamVR is not running, `VR_Init` starts it.
