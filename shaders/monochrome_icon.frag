// Subpixel-antialiased rendering of a monochrome icon.
//
// A font engine gets LCD antialiasing by computing coverage per colour
// channel: the three emitters of an LCD pixel are separate samples, so
// resolving horizontally at three times the pixel rate and giving each sample
// to its own channel triples the effective horizontal resolution. Qt does that
// for glyphs and not for images, which is why an icon drawn from an SVG beside
// a Font Awesome glyph reads as the softer of the two.
//
// This does the same for an icon. The source is the icon rasterised at three
// times the output resolution in both axes. Horizontal LCD layouts take their
// three channel coverages along x and average the three y samples; vertical
// layouts do the inverse. Both run the classic five-tap 1-2-3-2-1 filter along
// the subpixel sequence so a hard edge does not leave a saturated colour
// fringe. Reversed layouts swap the red and blue coverages.
//
// Per-channel coverage cannot go through ordinary alpha blending, which has one
// alpha for all three channels, and Qt Quick does not expose dual-source
// blending. So the icon composites against what is behind it and writes an
// opaque pixel. That background arrives as a texture rather than a colour,
// which is what lets the icon sit correctly on a button whose fill animates
// under it.
//
// lcd_subpixel_order is already resolved for the screen displaying the icon:
// NONE=0, RGB=1, BGR=2, VRGB=3, VBGR=4. NONE and every unknown value use one
// grayscale coverage for all channels, so disabled smoothing and unrecognised
// policy fail closed without colour fringes.

#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    vec4 foreground_color;
    vec2 source_extent;
    vec2 output_extent;
    float qt_Opacity;
    int lcd_subpixel_order;
};

layout(binding = 1) uniform sampler2D icon_source;
layout(binding = 2) uniform sampler2D background_source;

// Coverage of one supersample, addressed in source texels.
float sample_coverage(float column, float row)
{
    vec2 clamped = vec2(
        clamp(column, 0.0, source_extent.x - 1.0),
        clamp(row,    0.0, source_extent.y - 1.0));
    return texture(icon_source, (clamped + 0.5) / source_extent).a;
}

// One subpixel's coverage: the three supersampled rows inside the output
// pixel, averaged. Vertical resolution stays at the pixel rate, which is all
// the display offers.
float horizontal_subpixel_coverage(float column, float pixel_row)
{
    float first_row = pixel_row * 3.0;
    return (sample_coverage(column, first_row) +
            sample_coverage(column, first_row + 1.0) +
            sample_coverage(column, first_row + 2.0)) / 3.0;
}

// One vertical subpixel's coverage, averaging the three supersampled columns
// inside the output pixel.
float vertical_subpixel_coverage(float pixel_column, float row)
{
    float first_column = pixel_column * 3.0;
    return (sample_coverage(first_column, row) +
            sample_coverage(first_column + 1.0, row) +
            sample_coverage(first_column + 2.0, row)) / 3.0;
}

float grayscale_coverage(vec2 pixel)
{
    vec2 first_sample = pixel * 3.0;
    float coverage = 0.0;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            coverage += sample_coverage(
                first_sample.x + float(column),
                first_sample.y + float(row));
        }
    }
    return coverage / 9.0;
}

vec3 filter_coverages(float coverage[7])
{
    return vec3(
        (coverage[0] + 2.0 * coverage[1] + 3.0 * coverage[2] +
            2.0 * coverage[3] + coverage[4]) / 9.0,
        (coverage[1] + 2.0 * coverage[2] + 3.0 * coverage[3] +
            2.0 * coverage[4] + coverage[5]) / 9.0,
        (coverage[2] + 2.0 * coverage[3] + 3.0 * coverage[4] +
            2.0 * coverage[5] + coverage[6]) / 9.0);
}

void main()
{
    vec2 pixel = floor(qt_TexCoord0 * output_extent);
    vec3 filtered;

    if (lcd_subpixel_order == 1 || lcd_subpixel_order == 2) {
        float first_column = pixel.x * 3.0;
        float coverage[7];
        for (int i = 0; i < 7; ++i) {
            coverage[i] = horizontal_subpixel_coverage(
                first_column + float(i) - 2.0,
                pixel.y);
        }
        filtered = filter_coverages(coverage);
        if (lcd_subpixel_order == 2) {
            filtered = filtered.bgr;
        }
    } else if (lcd_subpixel_order == 3 || lcd_subpixel_order == 4) {
        float first_row = pixel.y * 3.0;
        float coverage[7];
        for (int i = 0; i < 7; ++i) {
            coverage[i] = vertical_subpixel_coverage(
                pixel.x,
                first_row + float(i) - 2.0);
        }
        filtered = filter_coverages(coverage);
        if (lcd_subpixel_order == 4) {
            filtered = filtered.bgr;
        }
    } else {
        filtered = vec3(grayscale_coverage(pixel));
    }

    vec3 behind = texture(background_source, qt_TexCoord0).rgb;
    vec3 composited = mix(behind, foreground_color.rgb, filtered);
    fragColor = vec4(composited, 1.0) * qt_Opacity;
}
