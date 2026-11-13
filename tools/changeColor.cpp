#include <stdint.h>

const uint32_t TARGET_COLOR = 0x07C160FF;
uint32_t old_color = 0x228844FF;

for (auto &pixel : pixels) {
  if ((pixel & 0x00FFFFFF) == (old_color & 0x00FFFFFF)) {
    pixel = (pixel & 0xFF000000) | 0x0007C160;
  }
}
