# Device art

`ps5.png`, `ps4.png`, `ds4.png` and `dualsense.png` are the console and controller drawings the app and the design
gallery show; `unknown.png`, a console with a question mark on its lid, stands in for one not yet added (Home's
Add a console). They are original illustrations made for Hotaru with an image generation tool: no photographs,
stock images or official renders went into them, and they carry no logos or wordmarks (the face-button shapes are
hardware detail). They are distributed with the project, under its license.

The app ships scaled copies in `assets/devices/`; after changing a drawing here, run `scripts/make-device-art.sh`.

Where each console touches the floor, which Home needs to stand it on its floor line (`ART_PS5_FLOOR` and
`ART_PS4_FLOOR` in `src/ui/art.h`, `g_floor_below` in `../c_home.c`). Each image keeps its own small contact shadow
below that line.

| File | Size | Floor contact |
| --- | --- | --- |
| `ps5.png` | 600 x 1232 | 10 px from the bottom, the foot of the stand |
| `ps4.png` | 1496 x 724 | 20 px from the bottom, the lowest foot |
| `unknown.png` | 1201 x 695 | 43 px from the bottom, the solid lower edge (`ART_UNKNOWN_FLOOR`) |
| `ds4.png` | 1472 x 992 | the controllers share one canvas and centring, so they swap in place |
| `dualsense.png` | 1472 x 992 | |
