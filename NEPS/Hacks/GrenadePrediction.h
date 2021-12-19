#pragma once

#include "shared_lib/imgui/imgui.h"

#include "../SDK/EngineTrace.h"
#include "../SDK/UserCmd.h"
#include "../SDK/Vector.h"

#include <array>

namespace NadePrediction {
	void run(UserCmd* cmd) noexcept;
	void draw() noexcept;
};