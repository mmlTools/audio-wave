# Audio Wave (Simple) - Audio Visualizer for OBS Studio

**Audio Wave (Simple)** is a lightweight, real-time audio visualizer source for OBS Studio.  
It listens to any audio source in your scene and draws a clean, customizable waveform or shape that reacts to the sound.

Perfect for:
- Music streams  
- Just chatting overlays  
- Podcast visuals  
- “Now playing” scenes  
- Minimal visualizers for background music  

---

![Audio Wave Preview](docs/assets/img/preview.png)

---

## ❤️ Support the Project  

If you want to support the development of my future OBS tools:

**Lower Thirds Shop** https://ko-fi.com/mmltech/shop
**Ko-fi:** https://ko-fi.com/mmltech  
**PayPal:** https://paypal.me/mmlTools  

Your help keeps the project alive.

---

## ✨ Features

- 🎧 **Attach to any audio source**  
  Select your mic, desktop audio, music player, or any other audio source inside OBS.

- 🎨 **Customizable look & feel**
  - Wave color picker  
  - Custom width & height  
  - Adjustable amplitude (gain)  
  - Curve control for how smooth or punchy the wave looks  

- 🧱 **Multiple shapes**
  - Line (Horizontal)  
  - Rectangle  
  - Circle  
  - Hexagon  
  - Star  
  - Triangle  
  - Diamond  

- 📈 **Multiple styles**
  - **Wave (Line)** - classic oscilloscope-style line  
  - **Wave (Bars)** - bar segments along the line or shape  
  - **Wave (Linear Smooth)** - smoothed line for elegant movement  
  - **Wave (Linear Filled)** - filled waveform for a bold visual  

- 🪞 **Mirror mode**
  - Mirror the wave for a symmetrical look (especially nice with Line and Shapes).

- 🔧 **Shape Density & Curve**
  - Control how many bars/segments are drawn around shapes  
  - Curve power to fine-tune how the visual reacts to quiet vs loud sounds  

---

## 🧩 Installation

1. **Download** the plugin build for your platform (Windows / Linux / macOS).  
2. **Extract** the contents into your OBS Studio folder:
   - On Windows, typically:  
     `C:\Program Files\obs-studio\`  
   - On Linux/macOS, place files into the usual `obs-plugins` and `data/obs-plugins` directories.  
3. **Restart OBS Studio** if it was open.

> After restart, OBS should detect the new source type: **“Audio Wave (Simple)”**.

---

## 🚀 Getting Started

1. In OBS, click **`+`** in the **Sources** list.  
2. Choose **_Audio Wave (Simple)_**.  
3. Name the source (e.g., `Music Visualizer`) and click **OK**.  
4. In the properties window:
   - **Audio Source**: pick the audio source you want to visualize  
     - Example: `Desktop Audio`, `Mic/Aux`, `Music Player`, etc.
   - **Shape**: select Line, Circle, Rectangle, Hex, Star, Triangle, or Diamond  
   - **Style**: choose between Line, Bars, Linear Smooth, or Linear Filled  
   - **Wave Color**: pick any color that fits your overlay  
   - **Width / Height**: set the base resolution the plugin uses internally  
   - **Amplitude (%)**: increase if the wave looks too small, decrease if it’s clipping or too aggressive  
   - **Curve Power (%)**: tweak how the waveform responds to low vs high volume  
   - **Shape Density (%)**: adjust how many bars/points are drawn around shapes  
   - **Mirror wave horizontally**: toggle symmetrical visual around the center  

5. Click **OK**, then resize/reposition the visualizer in your scene like any other source.

---

## 🔧 Settings Overview

Here’s what each setting does in simple terms:

### 🎧 Audio Source
Select which audio you want to visualize.  
If nothing moves:
- Make sure the correct source is selected  
- Check that the source actually has audio signal in the OBS mixer

### 🧱 Shape
Defines the base shape the wave will follow:

- **Line (Horizontal)** - straight line across the screen  
- **Rectangle** - wave wraps around a rectangle frame  
- **Circle** - circular audio ring  
- **Hexagon / Triangle / Diamond / Star** - various geometric or “logo-like” outlines

### 📈 Style
How the waveform is drawn:

- **Wave (Line)** - thin line, classic look  
- **Wave (Bars)** - vertical bars rising from the shape  
- **Wave (Linear Smooth)** - line with smoothed motion  
- **Wave (Linear Filled)** - filled waveform area (works great for lower thirds or banners)

### 🎨 Color
Pick the main color of the wave or bars.

### 📏 Width & Height
Internal drawing resolution of the visualizer.  
- You can usually leave this at default (e.g., `800 x 200`) and resize in OBS.  
- If visuals look too pixelated or stretched, try increasing width/height.

### 🔊 Amplitude (%)
Controls how strong the visual reacts to the sound.
- Lower value: softer, more subtle movement  
- Higher value: bigger spikes and bars

### 📉 Curve Power (%)
Fine-tunes the response curve:
- Lower curve: more even reaction across volumes  
- Higher curve: quiet sounds stay subtle, loud sounds hit harder

### 🧬 Shape Density (%)
Controls how many bars/segments are drawn around shapes:
- Lower value: fewer, chunkier bars  
- Higher value: smoother, more detailed outline

### 🪞 Mirror Wave Horizontally
If enabled:
- For Line shape: draws a mirrored version of the wave up/down  
- For Shapes: draws bars on both inside and outside of the shape path

---

## 💡 Tips & Use Cases

- **Now Playing Scene**  
  Place the visualizer under your track title text and sync its color to your brand.

- **Just Chatting**  
  Add a subtle circle wave behind your webcam frame reacting to background music.

- **Podcast / Talk Show**  
  Use a Line (Horizontal) wave under your lower thirds to show voice activity.

- **Logo Visualizer**  
  Combine a circle or hex shape with your logo in the middle for a clean idle screen.

---

## 🧪 Troubleshooting

**Wave is a flat line**
- Check the selected **Audio Source** actually has audio in the OBS mixer.  
- Make sure the source isn’t muted.  
- Increase **Amplitude (%)** if the movement is too tiny.

**Too much noise / very aggressive movement**
- Lower **Amplitude (%)**.  
- Increase **Curve Power (%)** a bit to compress the visual response.

**Performance issues**
- Reduce **Shape Density (%)** if you’re using complex shapes with very high density.  
- Lower the internal **Width/Height** if you set them very high.

---

Enjoy using **Audio Wave (Simple)** in your scenes!  
If you create cool layouts with it, consider sharing screenshots or presets with the community. 💜
