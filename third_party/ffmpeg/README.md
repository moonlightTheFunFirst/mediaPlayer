# FFmpeg development headers

Subset of headers from the official FFmpeg n7.1 tag, commit
`b08d7969c550a804a59511c7b83f2dd8cc0499b8`:
https://github.com/FFmpeg/FFmpeg/tree/n7.1

The original copyright and license notices are retained. See COPYING.LGPLv2.1.
`include/libavutil/avconfig.h` is the generated configuration equivalent for
the current little-endian Windows x64 target (fast unaligned access).

FrameStepper loads the existing Qt deployment's avformat-61, avcodec-61,
avutil-59 and swscale-8 DLLs dynamically and checks their ABI major versions.
These headers are build inputs, not additional deployment files.
