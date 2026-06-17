# TODO

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
Draw split/segment times
Either handle SDL errors or assert their absence

## Research

<!-- File Pilot render system interview -->
Split icon atlasing
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

## Low Prio

Upgrade SDL version
Vendor some deps so things like the above are easier
Prune SDL features to bring down binary size

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

For all new textures:
    Pack with stb_rect_pack, for now
    Pack into transfer buffer
    Upload to new positions in a copy pass

<!-- For a first pass: just rect pack, transfer buffer pack, and upload in one go -->
Make basic UI engine use atlas
    Box renderer just appends to list of verts/indices for now

BUGS:

<!-- - Atlas bleeding issues -->
<!-- - Weird incorrect positioning -->
<!-- - Memory leak? -->
<!-- - Stuff not aligned to pixel boundaries (more important for text though) -->
<!-- - Pixelated textures, compared to SDL renderer -->
<!-- - Corrupted textures? -->
    <!-- - These both look like I just need to alpha blend properly -->
