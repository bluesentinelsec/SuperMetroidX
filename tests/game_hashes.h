/*
 * Recorded by smx_test_game_run --record from a build whose pictures matched
 * Mesen 2 on both runs. These are hashes of output, not ROM bytes.
 */
#ifndef SMX_GAME_HASHES_H_
#define SMX_GAME_HASHES_H_

#include <SDL3/SDL.h>

/* FNV-1a of the XRGB8888 picture, and of all S16 stereo samples so far, every 300 frames. */
#define SMX_GAME_CHECKPOINT_FRAMES 300u
#define SMX_GAME_CHECKPOINTS 20u
#define SMX_GAME_INTERPRETED 4u

static const Uint32 smx_game_idle[SMX_GAME_CHECKPOINTS][2] = {
    {0xa0379dc5u, 0x465739c5u},
    {0xc164420cu, 0x4e9a7afdu},
    {0x308754acu, 0x8a2d95c8u},
    {0x6e0e4968u, 0x858f0f7au},
    {0x136018dau, 0xf6cffc3au},
    {0x59e84757u, 0xf3c5dafdu},
    {0xf167ee34u, 0xbcd646f3u},
    {0xfef50d3fu, 0x2213763du},
    {0xda61bf27u, 0x3c4db0adu},
    {0xe20f85e3u, 0x3d54243cu},
    {0x40bcc016u, 0x9f8e71ffu},
    {0xda150657u, 0xb6e82bd6u},
    {0x6d1e8a7au, 0xc645dc4du},
    {0xb26b089cu, 0xc8cba201u},
    {0x4e0d3287u, 0x8ac5d718u},
    {0x9807ff69u, 0x3e02896bu},
    {0xea0ac39eu, 0x30a945edu},
    {0x1c9b51e5u, 0xccd20637u},
    {0x5a54bc64u, 0x5bc62d85u},
    {0x6412cea1u, 0x74bd4c8fu},
};

static const Uint32 smx_game_new_game[SMX_GAME_CHECKPOINTS][2] = {
    {0xa0379dc5u, 0x465739c5u},
    {0xc164420cu, 0x4e9a7afdu},
    {0x308754acu, 0x8a2d95c8u},
    {0x6e0e4968u, 0x858f0f7au},
    {0x11521357u, 0x719fce7du},
    {0x13131d13u, 0x62915ce5u},
    {0x141725e5u, 0xcd5ee8cau},
    {0x141725e5u, 0x4bc18a6cu},
    {0xa0379dc5u, 0x7baaa354u},
    {0xd3b4e89du, 0x03ad3694u},
    {0xc4d418c7u, 0x1a0ed2b5u},
    {0x1f4bf533u, 0xe8fb4a0au},
    {0x6d78347eu, 0xe275b512u},
    {0x6d78347eu, 0xc08f0d81u},
    {0x1f4bf533u, 0xb118a9a5u},
    {0xbd73abc1u, 0xc9da4625u},
    {0xfa5c2abfu, 0xe79b3dcdu},
    {0x0cbc6541u, 0x1ac6f0adu},
    {0x1089341bu, 0x5d3bd7d9u},
    {0xc604d8bau, 0xf3ecad6au},
};

#endif /* SMX_GAME_HASHES_H_ */
