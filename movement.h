#pragma once
// movement.h — movement speed hook interface
// Implementation delegates to features.cpp / ShadowHook on
// VRCPlayer movement methods. Exposed to menu_renderer.cpp for the
// Movement tab UI.

namespace Movement
{
    // Apply ShadowHook to VRCPlayer loco method to intercept speed
    void ApplyHook();
    void Unhook();

    bool  IsActive();
    float GetSpeed();
    void  SetSpeed(float multiplier);  // 1.0 = default
}
