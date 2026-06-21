#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

inline std::vector<unsigned char> GetClipboardImagePNGData();

#ifdef _WIN32
#include <iostream>
#include <windows.h>

inline ClipboardImage GetClipboardImage() {
  ClipboardImage img;

  if (!OpenClipboard(nullptr)) {
    std::cerr << "[Clipboard] Failed to open clipboard.\n";
    return img;
  }

  HANDLE hData = GetClipboardData(CF_DIB);
  if (!hData) {
    CloseClipboard();
    return img;
  }

  BITMAPINFO *bmpInfo = (BITMAPINFO *)GlobalLock(hData);
  if (!bmpInfo) {
    CloseClipboard();
    return img;
  }

  int width = bmpInfo->bmiHeader.biWidth;
  int height = abs(bmpInfo->bmiHeader.biHeight);
  int bpp = bmpInfo->bmiHeader.biBitCount / 8;
  if (bpp < 3) {
    GlobalUnlock(hData);
    CloseClipboard();
    return img;
  }

  const unsigned char *src =
      (unsigned char *)bmpInfo + bmpInfo->bmiHeader.biSize;
  size_t size = width * height * bpp;

  img.size.x = width;
  img.size.y = height;
  img.pixels.resize(width * height * 4);

  for (int i = 0; i < width * height; i++) {
    img.pixels[i * 4 + 0] = src[i * bpp + 2];                    // R
    img.pixels[i * 4 + 1] = src[i * bpp + 1];                    // G
    img.pixels[i * 4 + 2] = src[i * bpp + 0];                    // B
    img.pixels[i * 4 + 3] = (bpp == 4) ? src[i * bpp + 3] : 255; // A
  }

  GlobalUnlock(hData);
  CloseClipboard();
  return img;
}

#endif // _WIN32

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#include <vector>

inline ClipboardImage GetClipboardImage() {
  ClipboardImage img;
  @autoreleasepool {
    NSPasteboard *pb = [NSPasteboard generalPasteboard];
    NSImage *nsimg = [[NSImage alloc] initWithPasteboard:pb];
    if (!nsimg)
      return img;

    NSBitmapImageRep *rep = [[nsimg representations] firstObject];
    if (!rep)
      return img;

    NSInteger width = [rep pixelsWide];
    NSInteger height = [rep pixelsHigh];
    img.size.x = (int)width;
    img.size.y = (int)height;
    img.pixels.resize(width * height * 4);

    unsigned char *data = [rep bitmapData];
    memcpy(img.pixels.data(), data, width * height * 4);
  }
  return img;
}
#endif // __APPLE__

#ifdef __linux__
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>

#include <stb/stb_image.h>

inline std::vector<unsigned char> GetClipboardImagePNGData() {
  // Try wl-paste first (Wayland)
  FILE *pipe = popen("wl-paste --type image/png 2>/dev/null", "r");
  if (!pipe) {
    // Fallback to xclip (X11)
    pipe = popen("xclip -selection clipboard -t image/png -o 2>/dev/null", "r");
  }

  if (!pipe)
    throw std::runtime_error(
        "No clipboard utility found (need wl-paste or xclip)");

  std::vector<unsigned char> pngData;
  unsigned char buffer[4096];
  size_t n;
  while ((n = fread(buffer, 1, sizeof(buffer), pipe)) > 0) {
    pngData.insert(pngData.end(), buffer, buffer + n);
  }
  pclose(pipe);

  if (pngData.empty())
    throw std::runtime_error(
        "No PNG data in clipboard");

  return pngData;
}

#endif // __linux__
