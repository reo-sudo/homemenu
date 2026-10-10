# homemenu

A modern HOME-menu style launcher for the Nintendo 3DS: glass panels, a dusk skyline with a shooting star, tabs, an icon grid, themes, favorites and stereoscopic depth on the top screen.

It lists the titles installed on your SD card, shows their real icons and names, and launches the one you pick. Requires custom firmware (Luma3DS) to run from the Homebrew Launcher.

## Controls

| Input | Action |
| --- | --- |
| D-pad | Move in the grid / lists |
| L / R | Previous / next tab (All, Favorites, Themes, Settings) |
| A, or tap the selected icon, or tap Start | Launch the selected title |
| Y | Add or remove a favorite |
| X | Cycle theme |
| Swipe left/right on the grid | Change page |
| B or START, or tap Close | Quit back to the Homebrew Launcher |

## Tabs

- **All / Favorites**: 5x2 icon grid with pages. The selected title appears large on the top screen.
- **Themes**: Dusk, Ember, Aurora, Graphite. Applied instantly.
- **Settings**: 3D depth (Off / Low / High), shooting star on/off, sort order (A to Z / Z to A).

Settings and favorites are saved to `sdmc:/3ds/homemenu/config.bin`. The 3D effect follows the console's 3D slider.

## Build (no install needed): GitHub Actions

1. Create a new GitHub repository and upload everything in this folder, keeping `.github/workflows/build.yml`.
2. Open the Actions tab and run "Build 3DSX" (it also runs on every push).
3. Download the `homemenu` artifact and unzip it to get `homemenu.3dsx`.

## Build locally

Install devkitPro with the `3ds-dev` group, then run `make`.

## Install

Copy `homemenu.3dsx` to `/3ds/` on your SD card and start it from the Homebrew Launcher.

## Status

- Lists regular applications installed on the SD card (installed .cia titles, including homebrew).
- Not yet: cartridge games, DSiWare, system applet shortcuts, live previews, and launching .3dsx files directly.
- This code has not been compiled or run on a console yet. Launching uses libctru's `aptSetChainloader`, so the first test on hardware is the real check.
