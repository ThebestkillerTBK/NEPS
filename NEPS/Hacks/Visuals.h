#pragma once

struct UserCmd;
enum class FrameStage;
class GameEvent;
struct ViewSetup;
struct ImDrawList;
struct Vector;

namespace Visuals
{
    void runFreeCam(UserCmd* cmd, Vector viewAngles) noexcept;
    void freeCam(ViewSetup* setup) noexcept;
    void musicKit(FrameStage stage) noexcept;
    void playerModel(FrameStage stage) noexcept;
    void colorWorld() noexcept;
    void modifySmoke(FrameStage stage) noexcept;
    void modifyFire(FrameStage stage) noexcept;
    void thirdperson() noexcept;
    void removeVisualRecoil(FrameStage stage) noexcept;
    void removeBlur(FrameStage stage) noexcept;
    void removeGrass(FrameStage stage) noexcept;
    void applyScreenEffects() noexcept;
    void hitEffect(GameEvent* event = nullptr) noexcept;
    void killEffect(GameEvent* event = nullptr) noexcept;
    void hitMarker(GameEvent* event = nullptr, ImDrawList* drawList = nullptr) noexcept;
    void drawReloadProgress(ImDrawList* drawList = nullptr) noexcept;
    void damageIndicator(GameEvent* event = nullptr, ImDrawList* drawList = nullptr) noexcept;
    void disablePostProcessing(FrameStage stage) noexcept;
    void reduceFlashEffect() noexcept;
    bool removeHands(const char* modelName) noexcept;
    bool removeSleeves(const char* modelName) noexcept;
    bool removeWeapons(const char* modelName) noexcept;
    void skybox(FrameStage stage) noexcept;
    void bulletBeams(GameEvent *event);
    void bulletImpacts(GameEvent* event) noexcept;
    void drawMolotovHull(ImDrawList *drawList) noexcept;
    void drawSmokeTimer(ImDrawList* drawList) noexcept;
    void drawNadeBlast(ImDrawList* drawList) noexcept;
	void drawSmokeHull(ImDrawList *drawList) noexcept;
	void flashlight(FrameStage stage) noexcept;
	void playerBounds(ImDrawList *drawList) noexcept;
	void playerVelocity(ImDrawList *drawList) noexcept;
    void penetrationCrosshair(ImDrawList* drawList) noexcept;
    void drawAimbotFov(ImDrawList* drawList) noexcept;
}
