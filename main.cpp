// Minimal repro: in the SteamVR compositor, the transparent and translucent texels of an overlay that is drawn
// earlier hide overlays that are drawn later behind it. The game shows through instead of the farther overlay.
//
// The program shows two head-locked overlays for a few seconds per stage:
//   - back:  an opaque blue square
//   - front: a white circle (fully transparent corners) or a translucent white square, 3 cm closer to the user
// and changes the draw order and the layout between stages. Watch the console and look at the front overlay.
//
// Uses only OpenVR and Direct3D 11. Run it while SteamVR and a game are running.
#include <d3d11.h>

#if defined(__MINGW32__)
#include <openvr_mingw.hpp>  // MinGW-w64: some calls are broken with openvr.h
#else
#include <openvr.h>
#endif

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

constexpr int kSecondsPerStage = 8;

enum class FrontImage { Circle, WhiteOpaque, WhiteHalf };

struct Plane {
  float x = 0, y = 0, z = -1.0f;  // meters, relative to the HMD (-z is forward)
  float width = 0.3f;             // meters
  uint32_t sortOrder = 0;         // SetOverlaySortOrder (lower is drawn first)
  float alpha = 1.0f;             // SetOverlayAlpha
};

struct Stage {
  const char* title;
  Plane back;
  Plane front;
  FrontImage image;
};

// Premultiplied RGBA8 pixels.
struct Image {
  int size = 0;
  std::vector<uint8_t> rgba;
};

Image Solid(int size, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  Image image{size, std::vector<uint8_t>(static_cast<size_t>(size) * size * 4)};
  for (size_t i = 0; i < image.rgba.size(); i += 4) {
    image.rgba[i + 0] = r;
    image.rgba[i + 1] = g;
    image.rgba[i + 2] = b;
    image.rgba[i + 3] = a;
  }
  return image;
}

// A white circle inscribed in the square, anti-aliased by 4x4 supersampling. Outside is RGBA (0, 0, 0, 0).
Image Circle(int size) {
  Image image{size, std::vector<uint8_t>(static_cast<size_t>(size) * size * 4)};
  const float radius = size * 0.5f - 1.0f;
  const float center = size * 0.5f;
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      int inside = 0;
      for (int sy = 0; sy < 4; ++sy) {
        for (int sx = 0; sx < 4; ++sx) {
          const float dx = x + (sx + 0.5f) / 4.0f - center;
          const float dy = y + (sy + 0.5f) / 4.0f - center;
          if (dx * dx + dy * dy <= radius * radius) ++inside;
        }
      }
      const auto value = static_cast<uint8_t>(std::lround(255.0f * inside / 16.0f));
      uint8_t* p = &image.rgba[(static_cast<size_t>(y) * size + x) * 4];
      p[0] = p[1] = p[2] = p[3] = value;  // premultiplied white
    }
  }
  return image;
}

ID3D11Texture2D* MakeTexture(ID3D11Device* device, const Image& image) {
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = static_cast<UINT>(image.size);
  desc.Height = static_cast<UINT>(image.size);
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
  D3D11_SUBRESOURCE_DATA data{};
  data.pSysMem = image.rgba.data();
  data.SysMemPitch = static_cast<UINT>(image.size * 4);
  ID3D11Texture2D* texture = nullptr;
  if (FAILED(device->CreateTexture2D(&desc, &data, &texture))) return nullptr;
  return texture;
}

bool Ok(vr::EVROverlayError e, const char* what) {
  if (e == vr::VROverlayError_None) return true;
  std::printf("%s failed: %s\n", what, vr::VROverlay()->GetOverlayErrorNameFromEnum(e));
  return false;
}

bool Show(vr::VROverlayHandle_t handle, const Plane& plane, ID3D11Texture2D* texture, ID3D11DeviceContext* context) {
  vr::IVROverlay* overlay = vr::VROverlay();
  vr::HmdMatrix34_t transform{};
  transform.m[0][0] = transform.m[1][1] = transform.m[2][2] = 1.0f;
  transform.m[0][3] = plane.x;
  transform.m[1][3] = plane.y;
  transform.m[2][3] = plane.z;
  vr::Texture_t t{};
  t.handle = texture;
  t.eType = vr::TextureType_DirectX;
  t.eColorSpace = vr::ColorSpace_Auto;
  const bool ok =
      Ok(overlay->SetOverlayTransformTrackedDeviceRelative(handle, vr::k_unTrackedDeviceIndex_Hmd, &transform),
         "SetOverlayTransformTrackedDeviceRelative") &&
      Ok(overlay->SetOverlayWidthInMeters(handle, plane.width), "SetOverlayWidthInMeters") &&
      Ok(overlay->SetOverlayAlpha(handle, plane.alpha), "SetOverlayAlpha") &&
      Ok(overlay->SetOverlaySortOrder(handle, plane.sortOrder), "SetOverlaySortOrder") &&
      Ok(overlay->SetOverlayFlag(handle, vr::VROverlayFlags_IsPremultiplied, true), "SetOverlayFlag") &&
      Ok(overlay->SetOverlayTexture(handle, &t), "SetOverlayTexture");
  context->Flush();
  return ok && Ok(overlay->ShowOverlay(handle), "ShowOverlay");
}

bool RunStage(const Stage& stage, ID3D11Texture2D* backTexture, ID3D11Texture2D* frontTexture,
              ID3D11DeviceContext* context) {
  vr::IVROverlay* overlay = vr::VROverlay();
  vr::VROverlayHandle_t back = vr::k_ulOverlayHandleInvalid;
  vr::VROverlayHandle_t front = vr::k_ulOverlayHandleInvalid;
  bool ok = Ok(overlay->CreateOverlay("repro.depth.back", "repro back", &back), "CreateOverlay") &&
            Ok(overlay->CreateOverlay("repro.depth.front", "repro front", &front), "CreateOverlay") &&
            Show(back, stage.back, backTexture, context) && Show(front, stage.front, frontTexture, context);
  if (ok) {
    std::fflush(stdout);
    std::this_thread::sleep_for(std::chrono::seconds(kSecondsPerStage));
  }
  if (front != vr::k_ulOverlayHandleInvalid) overlay->DestroyOverlay(front);
  if (back != vr::k_ulOverlayHandleInvalid) overlay->DestroyOverlay(back);
  return ok;
}

}  // namespace

int main() {
  vr::EVRInitError initError = vr::VRInitError_None;
  vr::VR_Init(&initError, vr::VRApplication_Overlay);
  if (initError != vr::VRInitError_None) {
    std::printf("VR_Init failed: %s\n", vr::VR_GetVRInitErrorAsEnglishDescription(initError));
    return 1;
  }

  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* context = nullptr;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device,
                               nullptr, &context))) {
    std::printf("D3D11CreateDevice failed\n");
    vr::VR_Shutdown();
    return 1;
  }

  ID3D11Texture2D* blue = MakeTexture(device, Solid(192, 0, 64, 255, 255));
  ID3D11Texture2D* circle = MakeTexture(device, Circle(128));
  ID3D11Texture2D* whiteOpaque = MakeTexture(device, Solid(128, 255, 255, 255, 255));
  ID3D11Texture2D* whiteHalf = MakeTexture(device, Solid(128, 128, 128, 128, 128));  // 50 %, premultiplied
  if (!blue || !circle || !whiteOpaque || !whiteHalf) {
    std::printf("CreateTexture2D failed\n");
    vr::VR_Shutdown();
    return 1;
  }

  // The front overlay is always 3 cm closer than the back overlay.
  // "edge": in front of the right edge of a large back overlay. The distance from the HMD to the front overlay's
  // center (about 1.07 m) is larger than to the back overlay's center (1.00 m).
  const auto back = [](float width, uint32_t order) { return Plane{0, 0, -1.0f, width, order, 1.0f}; };
  const auto center = [](uint32_t order, float alpha) { return Plane{0, 0, -0.97f, 0.3f, order, alpha}; };
  const auto edge = [](uint32_t order, float alpha) { return Plane{0.45f, 0, -0.97f, 0.3f, order, alpha}; };
  const Stage stages[] = {
      // A. White circle with fully transparent corners.
      {"A1 circle, centered, same sort order", back(0.5f, 0), center(0, 1), FrontImage::Circle},
      {"A2 circle, centered, front drawn first", back(0.5f, 1), center(0, 1),
       FrontImage::Circle},
      {"A3 circle, at the edge, same sort order", back(1.2f, 0), edge(0, 1),
       FrontImage::Circle},
      {"A4 circle, at the edge, back drawn first", back(1.2f, 0), edge(1, 1),
       FrontImage::Circle},
      // B. White square, SetOverlayAlpha 0.5.
      {"B1 overlay alpha 50 %, centered, same sort order", back(0.5f, 0), center(0, 0.5f),
       FrontImage::WhiteOpaque},
      {"B2 overlay alpha 50 %, centered, back drawn first", back(0.5f, 0), center(1, 0.5f),
       FrontImage::WhiteOpaque},
      {"B3 overlay alpha 50 %, centered, front drawn first", back(0.5f, 1),
       center(0, 0.5f), FrontImage::WhiteOpaque},
      // C. White square, texel alpha 0.5.
      {"C1 texel alpha 50 %, centered, same sort order", back(0.5f, 0), center(0, 1),
       FrontImage::WhiteHalf},
      {"C2 texel alpha 50 %, centered, back drawn first", back(0.5f, 0), center(1, 1),
       FrontImage::WhiteHalf},
      {"C3 texel alpha 50 %, centered, front drawn first", back(0.5f, 1), center(0, 1),
       FrontImage::WhiteHalf},
      // D. White square, SetOverlayAlpha 0.5, at the edge of a large overlay.
      {"D1 overlay alpha 50 %, at the edge, same sort order", back(1.2f, 0),
       edge(0, 0.5f), FrontImage::WhiteOpaque},
      {"D2 overlay alpha 50 %, at the edge, back drawn first", back(1.2f, 0),
       edge(1, 0.5f), FrontImage::WhiteOpaque},
      {"D3 overlay alpha 50 %, at the edge, front drawn first", back(1.2f, 1),
       edge(0, 0.5f), FrontImage::WhiteOpaque},
  };

  std::printf("Each stage is shown for %d seconds. Correct behavior: the blue overlay is visible everywhere behind\n"
              "the white overlay. Note the stages where the game shows through instead.\n",
              kSecondsPerStage);
  int result = 0;
  for (const Stage& stage : stages) {
    std::printf("%s\n    look at: %s\n", stage.title,
                stage.image == FrontImage::Circle ? "the transparent corners around the circle"
                                                  : "the area inside the white square");
    ID3D11Texture2D* front = stage.image == FrontImage::Circle        ? circle
                             : stage.image == FrontImage::WhiteOpaque ? whiteOpaque
                                                                      : whiteHalf;
    if (!RunStage(stage, blue, front, context)) {
      result = 1;
      break;
    }
  }
  std::printf("Done.\n");

  whiteHalf->Release();
  whiteOpaque->Release();
  circle->Release();
  blue->Release();
  context->Release();
  device->Release();
  vr::VR_Shutdown();
  return result;
}
