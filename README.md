# Audio Waves Visualizer for OBS Studio

Audio Waves Visualizer is a minimal OBS source plugin for building audio-reactive visuals with OBS `.effect` / HLSL shaders.

The plugin itself does not hardcode visual themes. It creates a transparent video source, reads level and spectrum data from a selected OBS audio source, then passes that data into the selected shader as uniforms.

## Core idea

```text
OBS audio source
    ↓
RMS / peak / FFT band analysis
    ↓
shader uniforms
    ↓
transparent OBS visual source
```

You can use the bundled effects or load your own `.effect` file directly from the source properties window.

## Features

- Transparent OBS source for audio-reactive VFX.
- Select any active OBS audio source.
- Load custom `.effect` files from any folder.
- Optional `.effect.ini` metadata file for friendly control names.
- Live shader controls exposed in the OBS properties window.
- Audio uniforms for level, peak, bass, mid, treble, and 64 spectrum bands.
- Bundled VFX effects, including rings, waves, bars, neon lines, vortex effects, and rounded wobble bars.

## Bundled effects

The plugin includes:

1. Pulse Ring
2. Neon Lines
3. Equalizer Grid
4. Smooth Waveform Ribbon
5. Rounded Wobble Bars
6. Spectrum Bars
7. Radial Spikes
8. Tunnel Waves
9. Liquid Blobs
10. Starfield Burst
11. Vortex Rings

Effects are installed under:

```text
data/obs-plugins/audio-wave/effects/
```

## Custom effects

A custom shader can be loaded from the source properties:

```text
Properties → HLSL / OBS .effect file → Browse
```

The shader should define a `Draw` technique. The plugin also checks `Solid` and `Default` as fallbacks.

## Available shader uniforms

```hlsl
uniform float4x4 ViewProj;
uniform float2 source_size;
uniform float2 resolution;
uniform float time;

uniform float audio_level;
uniform float audio_peak;
uniform float audio_bass;
uniform float audio_mid;
uniform float audio_treble;
uniform float band_count;
uniform float audio_bands[64];

uniform float option1;
uniform float option2;
uniform float option3;
uniform float option4;
uniform float option5;
uniform float option6;
uniform float option7;
uniform float option8;

uniform float4 color1;
uniform float4 color2;
uniform float4 color3;
uniform float4 color4;
```

## Metadata file

To expose clean control names in OBS, place an `.effect.ini` file beside your shader.

Example:

```text
my-effect.effect
my-effect.effect.ini
```

Example metadata:

```ini
[effect]
name=My Audio Effect

[options]
option1=Line Thickness
option2=Glow Strength
option3=Animation Speed

[colors]
color1=Primary Color
color2=Secondary Color
color3=Glow Color
```

Only named options/colors are shown in the properties window. Unnamed shader uniforms remain hidden.

## Minimal shader example

```hlsl
uniform float4x4 ViewProj;
uniform float2 source_size;
uniform float time;
uniform float audio_level;
uniform float4 color1;

struct VertIn {
    float4 pos : POSITION;
    float2 uv  : TEXCOORD0;
};

struct VertOut {
    float4 pos : POSITION;
    float2 uv  : TEXCOORD0;
};

VertOut VSDefault(VertIn v_in)
{
    VertOut o;
    o.pos = mul(float4(v_in.pos.xyz, 1.0), ViewProj);
    o.uv = v_in.uv;
    return o;
}

float4 PSDefault(VertOut v) : TARGET
{
    float2 p = v.uv * 2.0 - 1.0;
    p.x *= source_size.x / max(1.0, source_size.y);

    float radius = 0.25 + audio_level * 0.35;
    float d = abs(length(p) - radius);
    float ring = 1.0 - smoothstep(0.02, 0.05, d);

    return float4(color1.rgb, ring);
}

technique Draw
{
    pass
    {
        vertex_shader = VSDefault(v_in);
        pixel_shader  = PSDefault(v_in);
    }
}
```

## Source sizing

The source renders inside its own transparent source rectangle. Resize the source item in OBS normally, or set the manual canvas width/height in source properties for a different internal shader resolution.

## Building

This project uses the OBS plugin template structure and CMake. The `data` folder is installed into the OBS plugin data directory so bundled effects and locale files are packaged with GitHub Actions artifacts.
