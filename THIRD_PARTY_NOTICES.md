# Third-party notices

Chiaki webOS is licensed under the AGPL-3.0 with an additional permission to link OpenSSL ([LICENSE](LICENSE)). The
release package links the components below statically and carries their full license texts in `licenses/`; in
this repository they come with their sources (the `chiaki-ng` submodule, the dependency tarballs
`scripts/chiaki-lib-build-webos.sh` downloads, LVGL in `third_party/`) or are in [LICENSES/](LICENSES).

| Component | Version | License | Copyright | Text in the package |
| --- | --- | --- | --- | --- |
| [chiaki-ng](https://github.com/streetpea/chiaki-ng) (libchiaki) | 43728af | AGPL-3.0-only with an OpenSSL exception | Florian Märkl and the Chiaki and chiaki-ng contributors | `licenses/chiaki-ng.txt` |
| [LVGL](https://lvgl.io) | 8.3.11 | MIT | 2021 LVGL Kft | `licenses/lvgl.txt` |
| [LodePNG](https://lodev.org/lodepng/) (part of LVGL) | 20201017 | Zlib | 2005-2020 Lode Vandevenne | `licenses/lodepng.txt` |
| [Montserrat](https://github.com/JulietaUla/Montserrat) (LVGL's built-in fonts) | | SIL OFL 1.1 | The Montserrat Project Authors | `licenses/OFL-1.1.txt` |
| [Font Awesome Free](https://fontawesome.com) (LVGL's symbol glyphs; the tile icons in `src/vendor/font_icons_72.c`) | | SIL OFL 1.1 (the font) | Fonticons, Inc. | `licenses/OFL-1.1.txt` |
| [QR Code generator](https://www.nayuki.io/page/qr-code-generator-library) | 1.8.0 | MIT | Project Nayuki | `licenses/qrcodegen.txt` |
| [OpenSSL](https://www.openssl.org) | 3.2.1 | Apache-2.0 | The OpenSSL Project Authors | `licenses/openssl.txt` |
| [curl](https://curl.se) (libcurl) | 8.11.1 | curl (MIT-style) | 1996-2024 Daniel Stenberg and many contributors | `licenses/curl.txt` |
| [json-c](https://github.com/json-c/json-c) | 0.17 | MIT | 2009-2012 Eric Haszlakiewicz; 2004-2005 Metaparadigm Pte Ltd | `licenses/json-c.txt` |
| [Opus](https://opus-codec.org) | 1.4 | BSD-3-Clause | 2001-2011 Xiph.Org, Skype Limited, Octasic, Jean-Marc Valin and others | `licenses/opus.txt` |
| [libevent](https://libevent.org) | 2.1.12 | BSD-3-Clause | 2000-2012 Niels Provos and Nick Mathewson | `licenses/libevent.txt` |
| [MiniUPnPc](https://miniupnp.tuxfamily.org) | 2.2.7 | BSD-3-Clause | 2005-2024 Thomas Bernard | `licenses/miniupnpc.txt` |
| [Jerasure](https://github.com/ceph/jerasure) | chiaki-ng's submodule | BSD-3-Clause | 2013 James S. Plank and Kevin Greenan | `licenses/jerasure.txt` |
| [GF-Complete](https://github.com/ceph/gf-complete) | chiaki-ng's submodule | BSD-3-Clause | 2013 James S. Plank, Ethan L. Miller, Kevin M. Greenan and others | `licenses/gf-complete.txt` |
| [nanopb](https://jpa.kapsi.fi/nanopb/) | 0.4.9.2 | Zlib | 2011 Petteri Aimonen | `licenses/nanopb.txt` |

Not part of the package: SDL2 and NDL DirectMedia are the TV's own libraries, linked dynamically.

The console and controller illustrations are original drawings made for this project
([design/menu/art](design/menu/art/README.md)).
