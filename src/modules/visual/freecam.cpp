#include "freecam.hpp"
#include "modules/ModuleRegistry.hpp"
#include "core/memory/Hooks.hpp"
#include <bedrocktools/memory/Signatures.hpp>
#include <bedrocktools/events/EventBus.hpp>
#include <bedrocktools/sdk/Memory.hpp>
#include <bedrocktools/sdk/Offsets.hpp>
#include <bedrocktools/sdk/world/Actor.hpp>
#include <bedrocktools/sdk/input/MoveInput.hpp>
#include <pl/ModMenu.hpp>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace off = bedrocktools::sdk::offsets;

static FreecamModule* g_fc = nullptr;

// ---------------------------------------------------------------------------
// Tick pemain lokal (event yang sama dipakai Compass & Keystrokes):
//  - simpan yaw/pitch (rotation(): x = pitch, y = yaw, seperti di Compass)
//  - baca joystick: analog.y > 0 = maju, analog.x > 0 = kiri (seperti Keystrokes)
// ---------------------------------------------------------------------------
static void onPlayerTick(bedrocktools::sdk::Player* player) {
    if (!g_fc || !g_fc->enabled || !player) return;

    const auto rot = player->rotation();
    g_fc->m_pitch = rot.x;
    g_fc->m_yaw   = rot.y;

    if (!g_fc->isActive()) {
        g_fc->m_inFwd = g_fc->m_inLeft = g_fc->m_inUp = 0.f;
        return;
    }

    auto* mi = bedrocktools::sdk::moveInputComponent((void*)player);
    if (!mi) return;
    auto& raw = mi->mRawInputState;

    float fwd = raw.mAnalogMoveVector.y;
    float left = raw.mAnalogMoveVector.x;
    // cadangan kalau game memakai flag arah, bukan analog
    if (raw.test(MoveInputState::Flag::Up)   || raw.test(MoveInputState::Flag::UpLeft)   || raw.test(MoveInputState::Flag::UpRight))   fwd  =  1.f;
    if (raw.test(MoveInputState::Flag::Down) || raw.test(MoveInputState::Flag::DownLeft) || raw.test(MoveInputState::Flag::DownRight)) fwd  = -1.f;
    if (raw.test(MoveInputState::Flag::Left) || raw.test(MoveInputState::Flag::UpLeft)   || raw.test(MoveInputState::Flag::DownLeft))  left =  1.f;
    if (raw.test(MoveInputState::Flag::Right)|| raw.test(MoveInputState::Flag::UpRight)  || raw.test(MoveInputState::Flag::DownRight)) left = -1.f;

    float up = 0.f;
    if (raw.test(MoveInputState::Flag::JumpDown))  up += 1.f;
    if (raw.test(MoveInputState::Flag::SneakDown)) up -= 1.f;

    g_fc->m_inFwd = fwd;
    g_fc->m_inLeft = left;
    g_fc->m_inUp = up;

    // Best-effort: diamkan karakter. Perlu dites apakah cukup (lihat catatan).
    if (g_fc->m_freezeBody) {
        raw.mAnalogMoveVector.x = 0.f;
        raw.mAnalogMoveVector.y = 0.f;
    }
}

// ---------------------------------------------------------------------------
// Hook RenderLevel. Sama seperti ChunkBorder: _this = LevelRenderer,
// LevelRendererPlayer = *(LevelRenderer + mLevelRendererPlayer) (pointer).
// Kamera = LevelRendererPlayer + mCamPos (float x,y,z).
// ---------------------------------------------------------------------------
static void (*_renderLevel_orig)(void*, void*, void*) = nullptr;

static void _renderLevel_hook(void* _this, void* screenContext, void* a3) {
    if (g_fc && g_fc->isActive() && _this) {
        uintptr_t lrp = *(uintptr_t*)((uintptr_t)_this + off::LevelRenderer::mLevelRendererPlayer);
        if (lrp > 0x1000) {
            float* cam = (float*)(lrp + off::LevelRendererPlayer::mCamPos);

            static auto last = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            float dt = std::chrono::duration<float>(now - last).count();
            last = now;
            if (dt > 0.1f) dt = 0.1f;

            if (g_fc->m_needInit) {               // mulai dari posisi kamera sekarang
                g_fc->m_camX = cam[0]; g_fc->m_camY = cam[1]; g_fc->m_camZ = cam[2];
                g_fc->m_needInit = false;
            }

            const float d2r = 3.14159265f / 180.0f;
            float yaw = g_fc->m_yaw.load() * d2r;
            float pitch = g_fc->m_pitch.load() * d2r;

            // yaw 0 = menghadap +Z; maju = (-sin yaw, +cos yaw); kiri = (cos yaw, sin yaw)
            float fx = -std::sin(yaw) * std::cos(pitch);
            float fy = -std::sin(pitch);
            float fz =  std::cos(yaw) * std::cos(pitch);
            float lx =  std::cos(yaw), lz = std::sin(yaw);

            float f = g_fc->m_inFwd, l = g_fc->m_inLeft, u = g_fc->m_inUp;
            float s = g_fc->m_speed * dt;
            g_fc->m_camX += (fx * f + lx * l) * s;
            g_fc->m_camY += (fy * f + u) * s;
            g_fc->m_camZ += (fz * f + lz * l) * s;

            cam[0] = g_fc->m_camX; cam[1] = g_fc->m_camY; cam[2] = g_fc->m_camZ;
        }
    }
    if (_renderLevel_orig) _renderLevel_orig(_this, screenContext, a3);
}

FreecamModule::FreecamModule()
    : Module("Freecam", "Fly the camera freely, like Java freecam.") {
    this->keybind = 0;
    g_fc = this;
}

FreecamModule::~FreecamModule() { if (g_fc == this) g_fc = nullptr; }

bool FreecamModule::isActive() { return enabled && m_buttonActive; }

void FreecamModule::onInit() {
    if (!m_renderHooked) {
        uintptr_t a = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::RenderLevel);
        if (a) {
            bedrocktools::hooks::install((void*)a, (void*)_renderLevel_hook, (void**)&_renderLevel_orig);
            m_renderHooked = true;
        }
    }
    bedrocktools::events::bus().subscribe<bedrocktools::events::LocalPlayerTickEvent>(
        [](auto& event) { onPlayerTick(event.player); });
    registerButtons();
}

void FreecamModule::onEnable()  { m_needInit = true; }
void FreecamModule::onDisable() { m_buttonActive = false; m_inFwd = m_inLeft = m_inUp = 0.f; }

void FreecamModule::loadConfig(const nlohmann::json& j) {
    Module::loadConfig(j);
    if (j.contains("m_speed")) m_speed = j["m_speed"].get<float>();
    if (j.contains("m_freezeBody")) m_freezeBody = j["m_freezeBody"].get<bool>();
    if (j.contains("m_overlayToggle")) m_overlayToggle = j["m_overlayToggle"].get<bool>();
    registerButtons();
}

void FreecamModule::saveConfig(nlohmann::json& j) {
    Module::saveConfig(j);
    j["m_speed"] = m_speed;
    j["m_freezeBody"] = m_freezeBody;
    j["m_overlayToggle"] = m_overlayToggle;
}

void FreecamModule::registerButtons() {
    if (!m_overlayToggle) {
        pl::modmenu::unregisterButton("bedrocktools.Freecam.Button");
        return;
    }
    pl::modmenu::ButtonBuilder("bedrocktools.Freecam.Button", "Freecam")
        .moduleId("bedrocktools.Freecam")
        .behavior(pl::modmenu::ButtonBehavior::Toggle)
        .stylePreset(pl::modmenu::ButtonStylePreset::Accent)
        .onEvent([this](std::string_view, pl::modmenu::ButtonEvent ev, float v) {
            if (ev == pl::modmenu::ButtonEvent::StateChanged) {
                bool on = v > 0.5f;
                if (on && !m_buttonActive) m_needInit = true;
                m_buttonActive = on;
            }
        })
        .registerButton();
}
