# 🎨 Doodlebold

A free, open-source painting program for Windows and Linux, made to behave similar to Paint Tool SAI.

## ✨ What you can do

### 🖌️ Paint
- 🖊️ **Brushes that mix colour**: Pencil, Brush, Water Color, Marker, Eraser, Blur and Smudge.
- 🪣 **Bucket**: fills the area you click, the inside of your lines or a colour, and can close small gaps in the lines.
- 〰️ **Stabilizer**: multiple levels, to stabilize handshake.
- ✍️ **Pen pressure, tilt and barrel buttons**.
- 🪶 **Brush pictures**: custom tip shapes, blots, bristle tips, and brush textures.

### 🗂️ Layers and editing
- 📚 **Layers** with folders, clipping groups, masks and Preserve Opacity.
- ✂️ **Selections**, the Magic Wand included, **free transform** and text.
- ⌨️ **Your own keys** for every command and tool, your custom tools too, and for the keys held on the canvas (Options › Keys).
- 🎛️ **Color panel** with wheel, sliders, swatches and a scratch pad.
- 🪄 **Effects**: Brightness/Contrast, Hue/Saturation/Lightness, Levels, Color Balance, Invert, Desaturate, Posterize, Gaussian,
  Motion and Radial Blur, Sharpen, Add Noise and Chromatic Aberration, with a live preview on the canvas.

### 🎬 Animation and timelapse
- 🎞️ **Basic Animation**: a timeline under each canvas, tracks that draw over each other (and can be hidden), frames with their own
  layers held for several beats, layers shared by every frame, onion skins, playback with a loop, and sound clips you can
  trim, with their waveform.
- 📹 **Video**: draw over a video frame by frame, start a canvas from a video, and export your animation as an MP4.
- ⏺️ **Timelapse**: record your drawings, replay it, and export the whole thing or a 30-second cut.

### 💾 Files
- 📂 Opens and saves `.doodle`, PSD / PSB, PNG, JPEG, BMP and TGA.
- 🧩 **Plugins** can add more file formats. More documentation on this to follow.

## 💻 What you need

- 🧠 A processor with **AVX2**. Most PCs made since 2013 (Intel) or 2015 (AMD) have it.
- 🖥️ A graphics driver with **OpenGL 3.3** or newer. Most PCs from about 2012 onwards have it, but virtual machines and
  Remote Desktop often don't.
- ✍️ **For a pen:** on Windows, a tablet driver with Windows Ink. On Linux, a Wayland desktop.
- 🎬 **For video and timelapse export, and drawing over a video:** [FFmpeg](https://ffmpeg.org/download.html), a free
  program. Doodlebold tells you how to get it when you need it.

## 🚀 Good to know

- 💾 **Autosave Feature** Every few minutes Doodlebold keeps a recovery copy of each canvas with unsaved changes, and the next start
  offers to restore them. It never saves over your files, so save as usual. Edit > Options > Recovery sets how often and where.
- ✍️ **My pen acting up** Other > Tablet Diagnostics shows what your pen is sending.
- 🐢 **Doodlebold is lagging** Other > Options > Performance can show how long each stroke takes to paint, and turn off the frosted menus
  or the smooth zoomed-out view for weaker graphics.

## 🗺️ Coming later

Not built yet:

- 🗂️ **Layers**: blend modes, paper texture, linework, shape and ruler layers.
- ✂️ **Selections**: soft selection edges.
- 📋 **Menus**: Change Canvas Size.


## ❓ FAQ

### Is it free?
Yes, there's no account, trial or subscription, and the code is open source under the [MIT licence](LICENSE).

### Was AI Assisted Coding used in this project?
Yes, with Claude. I am a senior software engineer, and have been developing software for +15 years now. Most code has been reviewed and merged by me.

### Does it work on a Mac?
Not at the moment. Doodlebold runs on Windows and Linux.

### Why did you make this?
I love Paint Tool SAI, and have been using it for many years. I've tried many art programs, but I always keep coming back to SAI.
Unfortunately, SAI does not have a handful of features I wish it had, and the lack of linux support is a dealbreaker to me. I wanted an art program
that felt similar and resembled Paint Tool SAI, but with some of the modern features I want.

If there are features you want, please share with me.

### How close is it to PaintTool SAI?
It's made to behave like SAI, but don't expect a 100% faithful experience, as recreating it was not the goal.

The brushes are tuned by comparing their strokes with SAI. On a 1 to 10
scale of closeness to SAI, the Pencil, Brush, Water Color, Marker, Eraser, Blur, Smudge and the stabilizer all score
about 8 or more. Close, not identical: some of the brushes you use on SAI may feel different here.

### Is any code copied from SAI?
No. The brush engine was written from scratch, without access to SAI's code. It was tuned only from
descriptions of how SAI behaves, pictures of its strokes and measurements of those pictures. The rest of the program, such as layers, the interface, 
PSD and timelapse, is my own code. 

**In short**: Doodlebold was made by studying how SAI behaves, not by decompiling it.

## 🛠️ For developers

Written in C++20 with Qt 6.5 or newer, CMake and GoogleTest.

### Linux

On Fedora, install the dependencies. Other distributions work too with Qt 6.5 or newer, under their own package names.

```bash
sudo dnf install cmake ninja-build gcc-c++ qt6-qtbase-devel qt6-qtsvg-devel qt6-qtwayland-devel mesa-libGL-devel gtest-devel gmock-devel libjpeg-turbo-devel wayland-devel wayland-protocols-devel
```

Optional: `ffmpeg-free` for video and timelapse export, `qt6-qtmultimedia-devel` to hear an animation's sound clips while it
plays, and `ccache` for faster rebuilds.

Build, run the tests and start the app:

```bash
cmake --workflow --preset test-before-commit
```

```bash
./build/src/doodlebold
```

[tools/README.md](tools/README.md) describes every preset and tool, including how to check a change before committing it.

### Windows

You need Visual Studio 2022 with the C++ desktop workload, Qt 6.5 or newer (MSVC 2022 64-bit), CMake 3.25 or newer, and
`vcpkg install gtest libjpeg-turbo`. In a Developer prompt, with `VCPKG_ROOT` set and Qt's `bin` folder on the `PATH`:

```bat
cmake --workflow --preset windows-msvc
cmake --workflow --preset windows-zip
```

The first builds and runs the tests, the second makes the zip, with Qt beside `doodlebold.exe`.

## 💜 Credits

- 🔤 The interface font is [Inter](https://github.com/rsms/inter) (SIL Open Font License 1.1, `third_party/Inter-OFL.txt`).
- 📦 [miniz](https://github.com/richgel999/miniz) (MIT, `third_party/miniz/`) reads `.zip` packs.
- 🧰 Everything else is Qt (LGPLv3), libjpeg-turbo and the platform: see `THIRD-PARTY-NOTICES.txt`. FFmpeg runs as a separate
  program and is never linked.

Doodlebold is an independent project, not affiliated with SYSTEMAX, the makers of PaintTool SAI.

## 📄 Licence

[MIT](LICENSE)
