#pragma once

class Entity;
class Matrix3x4;
struct UserCmd;
enum class FrameStage;

namespace Animations
{
    void releaseState() noexcept;
    void getDesyncedBoneMatrices(Matrix3x4 *out) noexcept;
    void computeDesync(const UserCmd &cmd, bool sendPacket) noexcept;
    void syncLocal(const UserCmd &cmd, bool sendPacket) noexcept;
    void resolveDesync(Entity *animatable) noexcept;
}
