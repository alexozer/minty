# TODO

## Next

<!-- Put all glyph bitmaps for font in atlas -->
    <!-- Continue using arrays for now, make packer eat empty textures -->
<!-- Fix glyph color bug -->

## Prototyping

<!-- Render Roboto font -->
<!-- Decode split images -->
<!-- Replace most of shitty platform layer with SDL -->
<!-- Render PNGs -->
<!-- Write simple prototype stacking renderer -->
<!--     How am I supposed to change the text contents? Size? -->
<!--         A: SDL_RenderTexture() or whatever takes src/dest size -->
<!--         For text: fixed font sizes for now -->
<!-- Error handling -->
<!-- Timer logic loop -->
<!-- Put splits/timer/texture state etc. on dedicated arena -->
<!-- Load/unload session -->
<!--     Right-click menu? -->
<!-- Either handle SDL errors or assert their absence -->
Draw split/segment times
Generate header files with function forward declarations
Test on Windows
    Port shaders to HLSL I suppose?

## Research

<!-- File Pilot render system interview -->
<!-- Split icon atlasing -->
<!-- Blog posts on text rendering / SDFs -->
<!-- kb_text_shape shaping/segmentation -->
<!-- GPU font rendering (slug) -->
<!-- Modern rendering APIs (webgpu, sdl gpu) -->
Pixel-perfect rendering
    Idea: preserve pixel coordinates until shader execution?
Settings UI
Layout
Smooth resize
    How does Ghostty do it?!?
Global hotkeys
    See how OBS does it?
Permanent split history / rollback / undo
Clearer error handling strategy
    I can't think of good invariants for "just let garbage data propagate through the system and
    only check for problems at key points where the outcome could matter"
    Maybe the issue is: trading control flow combinatorics for state combinatorics
Font gradients
    Simplest solution for now within my exp. level is:
    Vertex colors picked out by fragment shader, interpolated in good colorspace on CPU
Font outlines
    SDFs would probably be helpful here...

## Low Prio

Upgrade SDL version
Vendor some deps so things like the above are easier
Prune SDL features to bring down binary size
Forward decl generator

## Error handling

<!-- Some sort of context system where you -->
<!--     Say errctx("load split icon") -->
<!--     Pass ErrorContext* to fallible functions -->
<!--     First error: sets "bottom" error -->
<!--     Subsequent error contexts -->
<!--         Check if current error -->
<!--         If so, stack on top -->
<!--     Eventually, you process and clear the error context -->

## Simple starter renderer

Box can be
    Text (content, size, color)
    Texture (scale?)
    Nothing (padding)?
Box can have
    Padding (but not margin)
    Width/Height (no constraints atm)
    Hstack/Vstack children
        In this case, box width/height is determined by children?
        Maybe only for vertical?
    Maybe also allow option for absolute size with children?

# Split icon atlasing

Instead of computing atlases CPU-side, we can use transfer buffers / partial
texture upload to pack them on GPU at runtime!

<!-- For all new textures: -->
<!--     Pack with stb_rect_pack, for now -->
<!--     Pack into transfer buffer -->
<!--     Upload to new positions in a copy pass -->

<!-- For a first pass: just rect pack, transfer buffer pack, and upload in one go -->
Make basic UI engine use atlas
    Box renderer just appends to list of verts/indices for now

<!-- Scroll to see icons! -->

BUGS:

<!-- - Atlas bleeding issues -->
<!-- - Weird incorrect positioning -->
<!-- - Memory leak? -->
<!-- - Stuff not aligned to pixel boundaries (more important for text though) -->
<!-- - Pixelated textures, compared to SDL renderer -->
<!-- - Corrupted textures? -->
    <!-- - These both look like I just need to alpha blend properly -->

# Freetype rendering

Eventual goal is to make line editor, but that's kind of overwhelming to begin with.
Baby steps.

<!-- - Rip out SDL_ttf -->
<!-- - Draw a single character bitmap rendered with freetype -->
<!-- - Make it possible to create more than one atlas/rendering pipeline -->
<!-- - Colored text -->
<!-- - 2px atlas gap? -->
<!-- - Wipe atlas textures in render pass before writing -->
- Render all glyphs in font to atlas
    <!-- - Convert to hashmap stuff -->
    - Actually, simpler for now: just keep using glyph index as key, make atlas packer resilient to empty textures
- Alphabet (simple shaping, aligned to pixel boundaries)

BUGS:

<!-- - Blurry fonts? -->
<!--     - It's almost like we're rendering at half resolution or something -->
<!--     - Or it could just be (lack of) gamma correction -->
<!-- - Neovim LSP autosave failing on blank documents, lololol, just create autocmd on lspattach autocmd? -->

# Gamma correction

<!-- - Understand gamma correction math better -->
<!-- - Figure out how to do gamma encode/decode using GPU texture buffer formats, if it makes sense -->
<!--     - See what noclip is doing? Or ask Jasper if I can't figure it out? -->
