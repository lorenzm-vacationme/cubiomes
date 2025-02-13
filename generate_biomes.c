// generate an image of the world
#include "generator.h"
#include "util.h"

int main()
{
    Generator g;
    setupGenerator(&g, MC_1_20, 0);

    uint64_t seed = 12345;
    applySeed(&g, DIM_OVERWORLD, seed);

    Range r;
    // 1:16, a.k.a. horizontal chunk scaling
    r.scale = 4;
    // Define the position and size for a horizontal area:
    r.x = 0, r.z = 0;   // position (x,z)
    r.sx = 400, r.sz = 400; // size (width,height)
    // Set the vertical range as a plane near sea level at scale 1:4.
    r.y = 15, r.sy = 1;

    // Allocate the necessary cache for this range.
    int *biomeIds = allocCache(&g, r);

    // Generate the area inside biomeIds, indexed as:
    // biomeIds[i_y*r.sx*r.sz + i_z*r.sx + i_x]
    // where (i_x, i_y, i_z) is a position relative to the range cuboid.
    genBiomes(&g, biomeIds, r);

    // Map the biomes to an image buffer, with 4 pixels per biome cell.
    int pix4cell = 16;
    int imgWidth = pix4cell*r.sx, imgHeight = pix4cell*r.sz;
    unsigned char biomeColors[256][3];
    initBiomeColors(biomeColors);
    unsigned char *rgb = (unsigned char *) malloc(3*imgWidth*imgHeight);
    biomesToImage(rgb, biomeColors, biomeIds, r.sx, r.sz, pix4cell, 2);

    // Save the RGB buffer to a PPM image file.
    savePPM("map.ppm", rgb, imgWidth, imgHeight);

    // Clean up.
    free(biomeIds);
    free(rgb);

    return 0;
}