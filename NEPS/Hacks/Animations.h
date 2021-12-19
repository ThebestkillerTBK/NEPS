#pragma once

class Entity;
class Matrix3x4;
struct UserCmd;
enum class FrameStage;

namespace Animations
{
    void releaseState() noexcept;
    void getDesyncedBones(Matrix3x4 *out) noexcept;
    void desyncedAnimations(const UserCmd &cmd, bool sendPacket) noexcept;
    void fixAnimation(const UserCmd &cmd, bool sendPacket) noexcept;
    void resolve(Entity *animatable) noexcept;
}
