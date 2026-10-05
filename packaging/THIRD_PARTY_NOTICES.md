# Third-party notices

Original project code: GPL-3.0-only. No third-party component is relicensed by this project.

Qt 6.8.3 retains its applicable LGPL/GPL and module notices. AngelScript 2.38.0 uses the zlib license. JsonCpp 1.9.6 uses its MIT license; zlib 1.3.1 uses its zlib license. libass 0.17.5 uses ISC; its font dependencies retain their own licenses (FreeType, HarfBuzz, FriBidi, Brotli, libpng, bzip2 and zlib). WebView2 SDK and Runtime retain Microsoft's distribution terms. SubRenderIntf.h retains its BSD notice. MPCVR and LAV retain upstream GPL notices.

Build dependencies and their original license files remain in `.deps/`. A public binary distribution requires a complete corresponding source archive and all third-party license files for the actual shipped versions. The generated development directory is not a public release package.

The optional `helpers/ffmpeg.exe` is a separate FFmpeg CLI process used solely to remux live compressed packets without re-encoding. This development package uses the user's Gyan FFmpeg 9.0.1 full build, reporting GPL version 3 or later (`--enable-gpl --enable-version3`). Its own license/build output and SHA-256 manifest are in `helpers/`; the complete GPL v3 text is the package's `LICENSE`. FFmpeg and every enabled library retain their respective notices. A public distribution still requires the complete corresponding sources and notices for this exact build, rather than only an upstream link.

BilibiliPotPlayer scripts are not committed or shipped. Local development may use the user's own copy under `.local/scripts/` or a selected extension directory. Public redistribution authorization for the selected commit has not been verified.

Bluesky FRC is never bundled. Public distribution with an in-process BFRC integration remains subject to the upstream GPL and BFRC authorization review described in the design.
