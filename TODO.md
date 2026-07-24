# TODO

<!-- Next: -->

<!-- Make timer actually do something to increase motivation -->
<!--     Load real splits -->
<!--     Run and display the timer state machine -->

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
<!-- Draw split/segment times -->
<!-- Generate header files with function forward declarations -->
<!-- Test on Windows -->
<!--     Port shaders to HLSL I suppose? -->
<!-- Evaluate using plain C -->
<!--     No practical advantage, it's mostly just a flex -->

## Research

GUI elements
SDF curve rendering
    Realistically just squircle for now
Settings UX design
Permanent split history / rollback / undo
Windowing
    <!-- Generally consistent/correct hidpi scaling factors -->
    <!--     Pretty sure there's just some SDL thing that tells you -->
    Cross-platform smooth scrolling (again, how does Ghostty do it?)
        Do I have to make a whole-ass Swift application shell just to get smooth scrolling?
    Smooth resize
        How does Zed do it??
        Window isn't resizable on Windows also btw? SDL's windowing stuff seems pretty janky in general
    Latency reduction
        Is vsync necessary on macos?
        If presenting immediately, what's the best way to best-effort sync to display?
Cross-platform global hotkeys
    See how OBS does it?
Faster PNG/JPEG decode
    JPEG: libjpeg-turbo over stb_image?
    Look into jpeg-xl?
Memory limits
    TigerStyle mandates allocating all memory to fixed limits at startup.
    Arenas are certainly closer to this than malloc(), but it's an issue when those limits collide
    with GPU memory limits
Make my hashtable implementation...
<!-- UI scaling -->

<!-- File Pilot render system interview -->
<!-- Split icon atlasing -->
<!-- Blog posts on text rendering / SDFs -->
<!-- kb_text_shape shaping/segmentation -->
<!-- GPU font rendering (slug) -->
<!-- Modern rendering APIs (webgpu, sdl gpu) -->
<!-- Pixel-perfect rendering -->
<!--     Idea: preserve pixel coordinates until shader execution? -->
<!-- Layout -->
<!-- Clearer error handling strategy -->
<!--     I can't think of good invariants for "just let garbage data propagate through the system and -->
<!--     only check for problems at key points where the outcome could matter" -->
<!--     Maybe the issue is: trading control flow combinatorics for state combinatorics -->
<!-- Font gradients -->
<!--     Simplest solution for now within my exp. level is: -->
<!--     Vertex colors picked out by fragment shader, interpolated in good colorspace on CPU -->
<!-- Font outlines -->
<!--     SDFs would probably be helpful here... -->
<!-- Improve font rendering -->
<!--     Sub-pixel positioning (Chrome maybe uses four subpixel positions?) -->
<!--     Sub-pixel antialiasing (maybe not on macos?) -->
<!--     Look into FreeType outline support? -->

## Low Prio

<!-- Upgrade SDL version -->
<!-- Vendor some deps so things like the above are easier -->
Prune SDL features to bring down binary size
Update freetype
<!-- Forward decl generator -->
Include file/line info in asserts / error messages, but only in non-release builds
<!-- Only increase max quad count for debug UI -->
<!--     Don't want to see zeroing frame arena show up in profiler -->
Transform mesh on GPU
    Also: reduce mesh size (with instancing? pos+size reduction?)
Split up types.h
    Use `pub` to codegen structs/includes/derives in header
    Generate a public/private header
    Move generated files out of repo
Cache shaping
<!-- Try enabling LTO again -->
<!--     Not available on macos, I think -->
<!-- Do depth normalization during layout -->

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

<!-- Box can be -->
<!--     Text (content, size, color) -->
<!--     Texture (scale?) -->
<!--     Nothing (padding)? -->
<!-- Box can have -->
<!--     Padding (but not margin) -->
<!--     Width/Height (no constraints atm) -->
<!--     Hstack/Vstack children -->
<!--         In this case, box width/height is determined by children? -->
<!--         Maybe only for vertical? -->
<!--     Maybe also allow option for absolute size with children? -->

# Split icon atlasing

Instead of computing atlases CPU-side, we can use transfer buffers / partial
texture upload to pack them on GPU at runtime!

<!-- For all new textures: -->
<!--     Pack with stb_rect_pack, for now -->
<!--     Pack into transfer buffer -->
<!--     Upload to new positions in a copy pass -->

<!-- For a first pass: just rect pack, transfer buffer pack, and upload in one go -->
<!-- Make basic UI engine use atlas -->
    <!-- Box renderer just appends to list of verts/indices for now -->

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
<!-- - Render all glyphs in font to atlas -->
    <!-- - Convert to hashmap stuff -->
    - Actually, simpler for now: just keep using glyph index as key, make atlas packer resilient to empty textures
<!-- - Alphabet (simple shaping, aligned to pixel boundaries) -->

<!-- BUGS: -->

<!-- - Blurry fonts? -->
<!--     - It's almost like we're rendering at half resolution or something -->
<!--     - Or it could just be (lack of) gamma correction -->
<!-- - Neovim LSP autosave failing on blank documents, lololol, just create autocmd on lspattach autocmd? -->

# Gamma correction

<!-- - Understand gamma correction math better -->
<!-- - Figure out how to do gamma encode/decode using GPU texture buffer formats, if it makes sense -->
<!--     - See what noclip is doing? Or ask Jasper if I can't figure it out? -->

# Font shaping

<!-- Figure out how these silly font coordinate systems work -->
<!-- Figure out how I'm supposed to position glyphs w.r.t. shaping results -->
    <!-- Read refpad -->
<!-- Subpixel positioning -->

# Texture caching

Idea for kinda-sorta immediate mode texture caching system:

API takes "request" to draw texture comprising of e.g. raw texture bitmap buffer, and
    - Produces handle / hash of contents
    - Produces instructions for drawing (e.g. atlas texture ID / coords)
    - Schedules request to batch upload textures etc. as needed

Idk the simplest/most elegant shape of the API, but the point is to avoid manually managing the
persistent state of atlases and treat them as implicit caches

You could imagine doing this for other state too, like parsing split files

Text cache API could wrap the atlas cache API, instead taking e.g. font, glyph ID, pixel size,
subpixel position, next lower/higher size?

# Cross-platform testing notes

Eventually got shader cross-compilation working with `shadercross`
Had to assign textures/samplers to Vulkan descriptor sets, still not sure what that even means

Issues:
<!-- - Flickering on Windows when typing -->
<!--     - Occasionally appears on Mac too -->
- Window not resizable
- Window size seems to be measured in real pixels on Linux and Windows, even in hidpi mode, unlike macos
- Shouldn't be able to maximize on Windows

# C port

Very, very close w/ the macro approach

TODO:
<!-- Implement vec__grow() -->
    <!-- Pass it a "generic" vec struct type to avoid passing a bajillion parameters? -->
    <!-- Implement using arena_realloc()? -->

We did it! And it's pretty nice! Yay!

# New UI system

<!-- Basic UI Box data structure -->
<!-- Basic UI builder API -->
<!-- Basic layout algorithm -->
Render icons with new UI system

Before trying to show more stuff on the screen... maybe it'd be more productive to think through
caching a bit more first.

How about a "cache graph" concept? I _think_ maybe you want an immediate mode-style API throughout
each graph layer/node. The part after UI layout which "renders" the layout probably wants to use an
API like:

```
render_text("my text", font, pos, size)
render_texture(Texture, pos, size)
```

# Texture cache POC

<!-- Test that stbrp supports incremental repacking -->
<!-- Init texture system -->
    <!-- Allocate starter textures + atlases -->
Function to enqueue texture draw request
Function to "render" requests
    <!-- Clear texture on first render -->
    <!-- Pack + upload textures -->
    Btw - perhaps glyph system can cache text bitmaps in CPU memory indefinitely? Font count +
    glyph count + font size can only get so big, and this is easier with arenas?

First POC demo: render individual icons / text through the cache
    Don't need to do any fancy cache eviction or anything yet
    Just render some icons man, text can wait
How to manage the various resources used across the frame?
    Complex option: implement some sort of render graph
    Simple starter option: pass GfxState around to things that need it, instead of trying to split
    up resources everywhere
    But what's the point of, say, TextureSystem not rendering textured quads itself?

Frame outline:
    UI builder uh, builds UI primitives
    UI layout
    Text + texture requests emitted from UI
        Text system converts text to texture requests
    Texture cache packs+uploads atlases as needed
    Texture mesh generated

I'm not confident enough in how I plan to implement SDFs to know how they'd slot into the
pipeline...

Aha! Maybe we can go back to the noclip.website idea of "render inst lists" - you independently
construct objects equivalent to draw calls, and then they're all chained together at the end.

Thinking back to texture stuff: we probably want to build a draw call for each "layer", including
bg, icons/text, transparent split selector, etc. Each render inst in our case contains:

- Pipeline (PSO?)
- A texture to bind (optional?)
- Vertex/fragment shader
- Runtime mesh to upload

We can't append directly into the vertex transfer buffer like before as easily (at least, building
independent render instances makes for a cleaner architecture)

## Packer render inst

For copy pass: plop src/dest metadata into render inst?

# Codegen attempt 2

Definitely do:

<!-- - Split up main.c into separate files -->
<!-- - Rearrange / rename stuff in c files -->
<!-- - Build codegen.c -->
<!-- - Add codegen as blitter dependency -->
<!-- - Make sure it works with zig build watch -->

Maybe future do:

- Split up types.h
- Codegen typedef struct / enum
- pub / fn for non-static / static

# Shadercross build.zig integration

<!-- - Call shadercross as external tool -->
<!-- - Call xxd -i as external tool (todo: rewrite in C) -->
<!-- - Include result of xxd as source file dependency -->

# UI Next Steps

<!-- Pad split icons -->
<!--     What does an immediate-mode API look like for this? Maybe we don't give AF for now? -->
<!--     Rewatch file pilot episode to try to get a feel for this -->
<!-- Improve UI API (style stacks, except it's just a template system) -->
<!-- Start working on text -->

# Text centering
    <!-- Horizontal centering -->
    Vertical

<!-- How do we center vertical? -->
<!--     Compute "glyph-relatiave vertical center": ascender - descender -->
<!--     Actually, use actual cap height if font has "A", else use bbox height -->

<!-- Pass UI_Box directly to font system -->
<!-- Left align -->
<!-- Right align -->

<!-- Alignment is working great! -->

# Text: next

<!-- Clipping -->
<!-- Ellipsis (maybe only for left-aligned text for now?) -->
<!--     I think you just... iteratively remove clipping character, replace with ellipsis, check if still -->
<!--     clipping, remove another char? repeat? -->
<!--     I guess you need to reshape each time you remove a character... does this mean we need to -->
<!--     operate at the level of extended grapheme clusters? -->

# Basic timer layout next

<!-- ChildSum layout rule (?) -->
<!--     Is there a good CSS analogy? -->
<!--     Implement by doing recursive layout first, assuming it's fixed-size, then laying out current? -->
<!--     May need to layout axes independently in that case -->
<!-- Big timer + related stuff -->

# Basic layout loading

<!-- XML parsing -->
<!-- Update SDL to get JPEG parsing (hopefully) -->
<!-- Apply background image -->
<!-- Apply fonts -->
Apply colors

# Z ordering

<!-- UI renderer should automatically assign a depth to text? -->
<!--     Nah? Maybe everything should just have a default depth that you can override -->
<!-- For maximum control, Z layouts shouldn't automatically assign depth -->
<!-- I think we want the texture system to: -->
<!--     Batch quad requests by depth -->
<!--     Sub-batch by texture format (grayscale vs rgb) -->
<!-- Since we're manually controlling draw order with draw call batching/ordering, and we have no -->
<!-- intersecting geometry otherwise, we shouldn't need to compute Z? -->
<!--     Does this mean we can omit a Z component on our vertices? -->
<!--     Maybe save vertex optimization for _really_ optimizing it -->

# Outlines

<!-- Just treat it as another FontInst configuration for now? -->
<!--     P smart actually -->

# UI Scaling

Simple idea: build UI in "pixels-but-not-really", then layout engine scales them to "real pixels"
    Which is exactly what CSS "pixels" are
    Don't see the need for a macos-centric "logical pixels" concept - I don't care, all that really matters is
    physical pixels and how many of them you're using
Still can provide an option to round/snap pos/width to exact pixels
    For e.g. sharp+thin lines

# Async

Maybe we can implement threading with a promises kind of model
You dispatch work, expect it to arrive back and fill a Promise hole, and check the Promise every
frame
    Recursive promises? Like, for first draw we maybe want to block on entire UI promise, but
    otherwise be smart about remaining stable during background work?
Is this any better than Ryan's locking/refcount/pointer-based caching system?

# Hashmaps V2

<!-- Indices are i64 -->
    <!-- Initialized to -1 on grow (aka also on first insertion) -->
<!-- Slots contain next idx + key + value -->

<!-- Impl V2 -->
<!-- Testing -->

TODO:

<!-- Benchmark a little more? -->
<!--     Fix the stupid while loop! -->
<!--     Seems like there's a weird bug... changing string alignment can change results -->
<!-- Implement shape cache -->

# Basic actual timer functionality

<!-- Parse full LSS -->
    <!-- Segment history -->
    <!-- Attempt history -->
    <!-- Game icon -->
Render full LSS
    <!-- Big timer -->
    <!-- Segments -->
    <!-- Splits -->
    +/- diff
LSS colors / ahead / behind colors

BUGS:
    <!-- - Shape cache not accounting for font size -->
    - Right-aligned numbers jitter
