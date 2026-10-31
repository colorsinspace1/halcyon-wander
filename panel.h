#pragma once
// Written by tools/make_panel.py: the artwork's node indices and ring positions.
#include <opxy/types.hpp>
namespace hwander { namespace panel {
const unsigned bar0 = 0;
const unsigned ring0 = 8;
const unsigned halo = 12;
const unsigned sun = 13;
const unsigned dot0_0 = 14;
const unsigned ripple0 = 62;
const unsigned trail0 = 65;
const unsigned marble = 69;
const unsigned node_count = 70, text_count = 16, palette_count = 9;
const unsigned shade_black = 0;
const unsigned shade_lit = 1;
const unsigned shade_dim = 2;
const unsigned shade_ring = 3;
const unsigned shade_grey0 = 4;
const unsigned shade_grey1 = 5;
const unsigned shade_grey2 = 6;
const unsigned shade_grey3 = 7;
const unsigned shade_grey4 = 8;
const opxy::i32 centre_x = 240, centre_y = 130;
// Each ring's twelve places, Q10 pixels.
const opxy::i32 place_x[4][12] = {{245760,280576,306063,315392,306063,280576,245760,210944,185457,176128,185457,210944},{245760,300032,339762,354304,339762,300032,245760,191488,151758,137216,151758,191488},{245760,319488,373461,393216,373461,319488,245760,172032,118059,98304,118059,172032},{245760,338944,407159,432128,407159,338944,245760,152576,84361,59392,84361,152576}};
const opxy::i32 place_y[4][12] = {{102400,106516,117760,133120,148480,159724,163840,159724,148480,133120,117760,106516},{87040,93214,110080,133120,156160,173026,179200,173026,156160,133120,110080,93214},{71680,79911,102400,133120,163840,186329,194560,186329,163840,133120,102400,79911},{56320,66609,94720,133120,171520,199631,209920,199631,171520,133120,94720,66609}};
}}
