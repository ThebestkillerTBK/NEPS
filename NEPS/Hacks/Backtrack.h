#pragma once

#include <array>
#include <deque>

#include "../SDK/Matrix3x4.h"
#include "../SDK/Vector.h"
#include "../SDK/ModelInfo.h"

enum class FrameStage;
struct UserCmd;
struct NetworkCmd;
class NetworkChannel;

struct Record
{
	int ownerIdx;
	Vector origin;
	float simulationTime;
	bool hasHelmet;
	int armor;
	Matrix3x4 bones[MAX_STUDIO_BONES];
};

struct incomingSequence {
	int inreliablestate;
	int sequencenr;
	float servertime;
};

namespace Backtrack
{
	void update(FrameStage) noexcept;
	void run(UserCmd *) noexcept;

	void addLatencyToNetwork(NetworkChannel*, float) noexcept;
	void updateIncomingSequences() noexcept;

	const std::deque<Record> &getRecords(std::size_t index) noexcept;
	float getLerp() noexcept;
	bool valid(float simTime) noexcept;
	float getMaxUnlag() noexcept;
	bool lastShotLagRecord() noexcept;
}
