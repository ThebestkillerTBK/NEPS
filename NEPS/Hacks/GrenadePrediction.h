#pragma once

#include "shared_lib/imgui/imgui.h"

#include "../SDK/EngineTrace.h"
#include "../SDK/UserCmd.h"
#include "../SDK/Vector.h"

#include <array>

enum : int
{
	Flash = 0,
	Smoke,
	Molotov,
	HE
};

enum : int
{
	Stand = 0,
	Run,
	Jump,
	Walk,
	RunJump,
	WalkJump
};

struct GrenadeInfo
{
	int gType = 4;
	Vector pos;
	Vector angle;
	int tType = 6;
	std::string name = "[Empty]";
	std::string actMapName = "unknown";
	bool RClick = false;
};

namespace NadeHelper {
	std::string getNadeName(int type) noexcept;
	std::string getThrowName(int type) noexcept;
	bool isTargetNade(Entity* activeWeapon, int nadeType, bool onlyMatchingInfos) noexcept;
	void draw(ImDrawList* drawList) noexcept;
	void run(UserCmd* cmd) noexcept;
};
namespace NadePrediction {
	void run(UserCmd* cmd) noexcept;
	void draw() noexcept;
};