#pragma once

#include "../Module.hpp"
#include <atomic>

// Freecam: kamera dilepas dari karakter. Gerak kamera memakai joystick bawaan
// game (dibaca lewat MoveInput), naik = tombol lompat, turun = tombol sneak.
// Aktif/nonaktif lewat tombol di layar (pola sama dengan ZoomModule).
class FreecamModule : public Module {
public:
    float m_speed = 12.0f;          // blok per detik
    float m_boost = 2.0f;           // pengali kecepatan saat sprint/boost (cadangan)
    bool  m_freezeBody = true;      // coba nol-kan input gerak karakter
    bool  m_overlayToggle = true;

    // dibaca di thread render, ditulis di thread tick
    std::atomic<float> m_inFwd{0.f}, m_inLeft{0.f}, m_inUp{0.f};
    std::atomic<float> m_yaw{0.f}, m_pitch{0.f};

    // posisi kamera virtual
    float m_camX = 0, m_camY = 0, m_camZ = 0;
    bool  m_needInit = true;

    bool m_buttonActive = false;

    FreecamModule();
    ~FreecamModule() override;

    void onInit() override;
    void onEnable() override;
    void onDisable() override;
    void loadConfig(const nlohmann::json& j) override;
    void saveConfig(nlohmann::json& j) override;

    bool isActive();
    void registerButtons();

private:
    bool m_renderHooked = false;
};
