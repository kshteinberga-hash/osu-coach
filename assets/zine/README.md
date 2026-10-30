# Overview artwork

Original assets generated with the built-in image_gen tool for the selected rhythm-zine direction. No stock reference imagery is included. Both PNGs preserve transparency and are embedded in the executable through `src/ui_art.rc`; no loose asset folder is needed at runtime.

The masthead is decorative branding. All coaching text, scores, bars and actions are rendered from application state. `paper.png` is tinted and placed behind the live text. WIC decodes the embedded PNGs once at startup, and Direct3D textures are released on shutdown. If loading fails, the UI uses flat surfaces and a text heading.

## Generation prompts

**masthead.png:** Create a production UI artwork asset on genuinely transparent background. Wide horizontal roughly 3:1 composition. Handmade underground rhythm-game zine masthead. Exact lettering 'osu! COACH' in enormous expressive black dry-brush lettering, two staggered lines, on overlapping torn hot-magenta paper scraps over cream ripped newsprint and a small cobalt blue scrap. Strong irregular silhouette, real tactile paper fibers and ink distress, energetic handcut editorial collage, not clean vector geometry, not generic futuristic font. Surround the right edge with a small yellow hand-drawn looping arrow and a few cyan/pink dust specks breaking off. Lettering occupies left 70 percent; right 30 percent stays mostly transparent. No other text, no watermark, no fake UI, no frame. Whole collage fully inside canvas with generous transparent margins. This will be a header inside a dark navy desktop application.

**paper.png:** Production UI texture asset: a single blank rectangular piece of off-white ivory paper, photographed flat front-on, isolated on transparent background. Landscape 4:3. Authentically hand-torn irregular fibrous edges on all four sides, asymmetrical small tears and a few subtle creases near corners. Fine scanned paper grain, subdued texture with a nearly uniform very light warm-gray center suitable for readable small UI text. No writing, no tape, no symbols, no shadow outside paper, no illustration, no objects, no watermark. Paper fills 94 percent of image, slim transparent margins. Not a repeating sawtooth or vector shape. Actual tactile handmade zine collage material.

## Visual verification

Run `python tools/preview_ui.py` for deterministic screenshots using explicitly fabricated sample data. `python tools/preview_ui.py 900 900` checks the minimum supported window width. This builds an isolated preview entry point in `build/` and never modifies production source or reads a real replay library.
