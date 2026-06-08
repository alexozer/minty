# TODO

## Prototyping

<!-- Render Roboto font -->
<!-- Decode split images -->
<!-- Replace most of shitty platform layer with SDL -->
<!-- Render PNGs -->
Write simple prototype stacking renderer
    How am I supposed to change the text contents? Size?
        A: SDL_RenderTexture() or whatever takes src/dest size
<!-- Error handling -->

## Research

File Pilot render system interview
kb_text_shape shaping/segmentation
GPU font rendering (slug)
Modern rendering APIs (webgpu, sdl gpu)
Settings UI
Layout
Smooth resize (how does Ghostty do it?!?)
Global hotkeys
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
