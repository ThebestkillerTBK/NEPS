﻿#include "Misc.h"

#include "Aimbot.h"
#include "Animations.h"
#include "Backtrack.h"
#include "EnginePrediction.h"

#include "../SDK/Client.h"
#include "../SDK/ClientMode.h"
#include "../SDK/ClassID.h"
#include "../SDK/ClientClass.h"
#include "../SDK/ConVar.h"
#include "../SDK/Cvar.h"
#include "../SDK/Entity.h"
#include "../SDK/EngineTrace.h"
#include "../SDK/FrameStage.h"
#include "../SDK/GameEvent.h"
#include "../lib/Helpers.hpp"
#include "../SDK/Input.h"
#include "../SDK/ItemSchema.h"
#include "../SDK/Localize.h"
#include "../SDK/NetworkChannel.h"
#include "../SDK/NetworkStringTable.h"
#include "../SDK/PlayerResource.h"
#include "../SDK/Panorama.h"
#include "../SDK/ProtobufReader.h"
#include "../SDK/Surface.h"
#include "../SDK/UserMessage.h"
#include "../SDK/VarMapping.h"
#include "../SDK/WeaponSystem.h"

#include "../Config.h"
#include "../GUI.h"
#include "../GameData.h"
#include "../Interfaces.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <shared_lib/imgui/imgui_internal.h>
#include "../lib/ImguiCustom.hpp"

#include <numeric>

void Misc::edgeJump(UserCmd *cmd) noexcept
{
	if (static Helpers::KeyBindState flag; !flag[config->movement.edgeJump])
		return;

	if (!localPlayer || !localPlayer->isAlive())
		return;

	if (localPlayer->moveType() == MoveType::Noclip || localPlayer->moveType() == MoveType::Ladder)
		return;

	if ((EnginePrediction::getFlags() & PlayerFlag_OnGround) && !(localPlayer->flags() & PlayerFlag_OnGround))
		cmd->buttons |= UserCmd::Button_Jump;
}

void Misc::slowwalk(UserCmd *cmd) noexcept
{
	if (!localPlayer || !localPlayer->isAlive() || ~localPlayer->flags() & PlayerFlag_OnGround || localPlayer->moveType() == MoveType::Noclip || localPlayer->moveType() == MoveType::Ladder)
		return;

	const auto activeWeapon = localPlayer->getActiveWeapon();
	if (!activeWeapon)
		return;

	const auto weaponData = activeWeapon->getWeaponData();
	if (!weaponData)
		return;

	const float maxSpeed = (localPlayer->isScoped() ? weaponData->maxSpeedAlt : weaponData->maxSpeed) / 3;
	const auto velocity = localPlayer->velocity();

	if (const auto speed = velocity.length2D(); speed > maxSpeed + 15.0f)
	{
		float direction = velocity.toAngle2D();
		direction = cmd->viewangles.y - direction;

		const auto negatedDirection = Vector::fromAngle2D(direction) * -450;
		cmd->forwardmove = negatedDirection.x;
		cmd->sidemove = negatedDirection.y;
	}
	else if (cmd->forwardmove && cmd->sidemove)
	{
		const float maxSpeedRoot = maxSpeed * static_cast<float>(M_SQRT1_2);
		cmd->forwardmove = cmd->forwardmove < 0.0f ? -maxSpeedRoot : maxSpeedRoot;
		cmd->sidemove = cmd->sidemove < 0.0f ? -maxSpeedRoot : maxSpeedRoot;
	} else if (cmd->forwardmove)
	{
		cmd->forwardmove = cmd->forwardmove < 0.0f ? -maxSpeed : maxSpeed;
	} else if (cmd->sidemove)
	{
		cmd->sidemove = cmd->sidemove < 0.0f ? -maxSpeed : maxSpeed;
	}
}

void Misc::fastwalk(UserCmd* cmd) noexcept
{
	if (static Helpers::KeyBindState flag; flag[config->exploits.fastwalk])
		return;
}

void Misc::updateClanTag() noexcept
{
	static std::string clanTag;
	static std::string clanTagBuffer;

	if (clanTag != config->griefing.clanTag)
	{
		clanTagBuffer = clanTag = config->griefing.clanTag;
		if (!clanTagBuffer.empty() && clanTagBuffer.front() != ' ' && clanTagBuffer.back() != ' ')
			clanTagBuffer.push_back(' ');
		return;
	}

	static auto lastTime = 0.0f;

	if (config->griefing.clocktag)
	{
		if (memory->globalVars->realTime - lastTime < 1.0f)
			return;

		const auto time = std::time(nullptr);
		const auto localTime = std::localtime(&time);
		char s[11];
		s[0] = '\0';
		sprintf_s(s, "[%02d:%02d:%02d]", localTime->tm_hour, localTime->tm_min, localTime->tm_sec);

		lastTime = memory->globalVars->realTime;
		memory->setClanTag(s, s);
	} else if (config->griefing.customClanTag)
	{
		if (memory->globalVars->realTime - lastTime < 0.6f)
			return;

		if (!clanTag.empty())
		{
			static int lastMode = 0;

			if (lastMode != config->griefing.animatedClanTag)
			{
				clanTagBuffer = clanTag;
				if (clanTagBuffer.front() != ' ' && clanTagBuffer.back() != ' ')
					clanTagBuffer.push_back(' ');
			}

			switch (config->griefing.animatedClanTag)
			{
			case 1:
			{
				const auto offset = Helpers::utf8SeqLen(clanTagBuffer[0]);
				if (offset != -1)
					std::rotate(clanTagBuffer.begin(), clanTagBuffer.begin() + offset, clanTagBuffer.end());
				break;
			}
			default:
				break;
			}

			lastMode = config->griefing.animatedClanTag;
		}

		lastTime = memory->globalVars->realTime;
		memory->setClanTag(clanTagBuffer.c_str(), clanTagBuffer.c_str());
	} else
	{
		if (memory->globalVars->realTime - lastTime < 0.6f)
			return;

		lastTime = memory->globalVars->realTime;
		memory->setClanTag("", "");
	}
}

struct customCmd
{
	float forwardmove;
	float sidemove;
	float upmove;
};

static Vector peekPosition{};

void Misc::drawAutoPeek(ImDrawList* drawList) noexcept
{
	static Helpers::KeyBindState flag;
	auto keyBind = flag[config->movement.quickPeekKey];
	if (!keyBind || !config->movement.quickPeekColor.enabled)
		return;

	if (peekPosition.notNull())
	{
		constexpr float step = M_PI * 2.0f / 20.0f;
		std::vector<ImVec2> points;
		for (float lat = 0.f; lat <= M_PI * 2.0f; lat += step)
		{
			const auto& point3d = Vector{ std::sin(lat), std::cos(lat), 0.f } *15.f;
			ImVec2 point2d;
			if (Helpers::worldToScreen(peekPosition + point3d, point2d))
				points.push_back(point2d);
		}

		const ImU32 color = (Helpers::calculateColor({ config->movement. quickPeekColor}));
		auto flags_backup = drawList->Flags;
		drawList->Flags |= ImDrawListFlags_AntiAliasedFill;
		drawList->AddConvexPolyFilled(points.data(), points.size(), color);
		drawList->AddPolyline(points.data(), points.size(), color, true, 2.f);
		drawList->Flags = flags_backup;
	}
}

void Misc::quickPeek(UserCmd* cmd) noexcept
{
	static bool hasShot = false;

	static Helpers::KeyBindState flag;
	auto keyBind = flag[config->movement.quickPeekKey];
	static auto reset = false;

	if (!keyBind || reset)
	{
		hasShot = false;
		peekPosition = Vector{};
		reset = false;
		return;
	}

	if (!localPlayer)
		return;

	if (!localPlayer->isAlive())
	{
		hasShot = false;
		peekPosition = Vector{};
		return;
	}

	if (const auto mt = localPlayer->moveType(); mt == MoveType::Ladder || mt == MoveType::Noclip || !(localPlayer->flags() & 1))
		return;

	if (keyBind)
	{
		if (!peekPosition.notNull())
			peekPosition = localPlayer->getRenderOrigin();

		if (cmd->buttons & UserCmd::Button_Attack)
			hasShot = true;

		if (hasShot)
		{
			const float yaw = cmd->viewangles.y;
			const auto difference = localPlayer->getRenderOrigin() - peekPosition;

			if (difference.length2D() > 5.0f)
			{
				const auto velocity = Vector{
					difference.x * std::cos(yaw / 180.0f * 3.141592654f) + difference.y * std::sin(yaw / 180.0f * 3.141592654f),
					difference.y * std::cos(yaw / 180.0f * 3.141592654f) - difference.x * std::sin(yaw / 180.0f * 3.141592654f),
					difference.z };

				cmd->forwardmove = -velocity.x * 20.f;
				cmd->sidemove = velocity.y * 20.f;
			}
			else
			{
				hasShot = false;
				peekPosition = Vector{};
				reset = true;
			}
		}
	}
	else
	{
		hasShot = false;
		peekPosition = Vector{};
		reset = true;
	}
}


static void drawCrosshair(ImDrawList *drawList, ImVec2 pos, ImU32 color, int type)
{
	bool aa = false;
	if (drawList->Flags & ImDrawListFlags_AntiAliasedFill)
	{
		drawList->Flags &= ~ImDrawListFlags_AntiAliasedFill;
		aa = true;
	}

	switch (type)
	{
	case 1:
		drawList->AddCircle(pos, 15.0f, color, 0, 2.0f);
		drawList->AddRectFilled(pos - ImVec2{1.0f, 1.0f}, pos + ImVec2{1.0f, 1.0f}, color);
		break;
	case 2:
		drawList->AddRectFilled(pos - ImVec2{1.0f, 1.0f}, pos + ImVec2{1.0f, 1.0f}, color);
		break;
	case 3:
		drawList->AddRectFilled(pos - ImVec2{1.0f, 15.0f}, pos + ImVec2{1.0f, 15.0f}, color);
		drawList->AddRectFilled(pos - ImVec2{15.0f, 1.0f}, pos + ImVec2{15.0f, 1.0f}, color);
		break;
	case 4:
		drawList->AddRectFilled(pos - ImVec2{1.0f, 15.0f}, pos - ImVec2{-1.0f, 5.0f}, color);
		drawList->AddRectFilled(pos + ImVec2{1.0f, 15.0f}, pos + ImVec2{-1.0f, 5.0f}, color);
		drawList->AddRectFilled(pos - ImVec2{15.0f, 1.0f}, pos - ImVec2{5.0f, -1.0f}, color);
		drawList->AddRectFilled(pos + ImVec2{15.0f, 1.0f}, pos + ImVec2{5.0f, -1.0f}, color);
		break;
	default:
		break;
	}

	if (aa)
		drawList->Flags |= ImDrawListFlags_AntiAliasedFill;
}

void Misc::overlayCrosshair(ImDrawList *drawList) noexcept
{
	if (!config->visuals.overlayCrosshairType)
		return;

	GameData::Lock lock;
	const auto &local = GameData::local();

	if (!local.exists || local.drawingCrosshair || local.drawingScope)
		return;

	drawCrosshair(drawList, ImGui::GetIO().DisplaySize / 2, Helpers::calculateColor(config->visuals.overlayCrosshair), config->visuals.overlayCrosshairType);
}

void Misc::recoilCrosshair(ImDrawList *drawList) noexcept
{
	if (!config->visuals.recoilCrosshairType)
		return;

	GameData::Lock lock;
	const auto &local = GameData::local();
	
	if (!local.exists || !local.alive)
		return;

	if (ImVec2 recoil; Helpers::worldToScreen(local.aimPunch, recoil, false))
	{
		const auto &displaySize = ImGui::GetIO().DisplaySize;
		Helpers::setAlphaFactor(std::sqrtf(ImLengthSqr((recoil - displaySize / 2) / displaySize)) * 100);
		drawCrosshair(drawList, recoil, Helpers::calculateColor(config->visuals.recoilCrosshair), config->visuals.recoilCrosshairType);
		Helpers::setAlphaFactor(1);
	}
}

void Misc::visualizeInaccuracy(ImDrawList *drawList) noexcept
{
	if (!config->visuals.inaccuracyCircle.enabled)
		return;

	GameData::Lock lock;
	const auto &local = GameData::local();

	if (!local.exists || !local.alive || !local.inaccuracy.notNull())
		return;

	if (ImVec2 edge; Helpers::worldToScreen(local.inaccuracy, edge))
	{
		const auto &displaySize = ImGui::GetIO().DisplaySize;
		const auto radius = std::sqrtf(ImLengthSqr(edge - displaySize / 2));

		if (radius > displaySize.x || radius > displaySize.y)
			return;

		const auto color = Helpers::calculateColor(config->visuals.inaccuracyCircle);
		drawList->AddCircleFilled(displaySize / 2, radius, color);
		if (config->visuals.inaccuracyCircle.outline)
			drawList->AddCircle(displaySize / 2, radius, color | IM_COL32_A_MASK);
	}
}

void Misc::prepareRevolver(UserCmd *cmd) noexcept
{
	if (static Helpers::KeyBindState flag; !flag[config->misc.prepareRevolver])
		return;

	if (!localPlayer) return;
	
	if (cmd->buttons & UserCmd::Button_Attack)
		return;

	constexpr float revolverPrepareTime = 0.234375f;
	
	if (auto activeWeapon = localPlayer->getActiveWeapon(); activeWeapon && activeWeapon->itemDefinitionIndex2() == WeaponId::Revolver)
	{
		auto time = memory->globalVars->serverTime();

		if (localPlayer->nextAttack() > time)
			return;

		cmd->buttons &= ~UserCmd::Button_Attack2;

		static auto readyTime = time + revolverPrepareTime;
		if (activeWeapon->nextPrimaryAttack() <= time)
		{
			if (readyTime <= time)
			{
				if (activeWeapon->nextSecondaryAttack() <= time)
					readyTime = time + revolverPrepareTime;
				else
					cmd->buttons |= UserCmd::Button_Attack2;
			} else
				cmd->buttons |= UserCmd::Button_Attack;
		}
		else
		{
			readyTime = time + revolverPrepareTime;
		}
	}
	
}

void Misc::fastPlant(UserCmd *cmd) noexcept
{
	if (!config->misc.fastPlant)
		return;

	static auto plantAnywhere = interfaces->cvar->findVar("mp_plant_c4_anywhere");

	if (plantAnywhere->getInt())
		return;

	if (!localPlayer || !localPlayer->isAlive() || (localPlayer->inBombZone() && localPlayer->flags() & PlayerFlag_OnGround))
		return;

	const auto activeWeapon = localPlayer->getActiveWeapon();
	if (!activeWeapon || !activeWeapon->isC4())
		return;

	cmd->buttons &= ~UserCmd::Button_Attack;

	constexpr auto doorRange = 200.0f;

	Trace trace;
	const auto startPos = localPlayer->getEyePosition();
	const auto endPos = startPos + Vector::fromAngle(cmd->viewangles) * doorRange;
	interfaces->engineTrace->traceRay({startPos, endPos}, 0x46004009, localPlayer.get(), trace);

	if (!trace.entity || trace.entity->getClientClass()->classId != ClassId::PropDoorRotating)
		cmd->buttons &= ~UserCmd::Button_Use;
}

void Misc::AutoDefuse(UserCmd* cmd) noexcept
{

	if (!config->misc.autoDefuse.enabled)
		return;

	if (!localPlayer || !localPlayer->isAlive())
		return;

	if (localPlayer->team() != Team::CT)
		return;

	const auto& bomb = GameData::plantedC4();
	Entity* bomb_ = nullptr;

	

	if (memory->plantedC4s->size > 0 && (!*memory->gameRules || (*memory->gameRules)->mapHasBombTarget()))
		bomb_ = (*memory->plantedC4s)[0];

	if (!bomb_ || !bomb_->c4Ticking()) return;

	float bombTimer = bomb.blowTime - memory->globalVars->currentTime;
	float distance = localPlayer->origin().distTo(bomb_->origin());
	bool distanceok = distance <= 75.f;
	bool cannotDefuse = (bomb.blowTime < bomb.defuseCountDown);

	if (cannotDefuse || !distanceok) return;

	if (config->misc.autoDefuse.silent)
	{
		if (cmd->buttons & UserCmd::Button_Use)
		{
			Vector pVecTarget = localPlayer->getEyePosition();
			Vector pTargetBomb = bomb_->origin();
			Vector angle = Vector::calcAngle(pVecTarget, pTargetBomb);
			angle.clamp();
			if (angle.notNull())
				cmd->viewangles = angle;
		}
	}
	else 
		cmd->buttons |= UserCmd::Button_Use;
		
}

void Misc::fastStop(UserCmd *cmd) noexcept
{
	if (!config->movement.fastStop)
		return;

	if (!localPlayer || !localPlayer->isAlive())
		return;

	if (localPlayer->moveType() == MoveType::Noclip || localPlayer->moveType() == MoveType::Ladder || !(localPlayer->flags() & 1) || cmd->buttons & UserCmd::Button_Jump)
		return;

	if (cmd->buttons & (UserCmd::Button_MoveLeft | UserCmd::Button_MoveRight | UserCmd::Button_Forward | UserCmd::Button_Back))
		return;

	const auto velocity = localPlayer->velocity();
	const auto speed = velocity.length2D();
	if (speed < 15.0f)
		return;

	float direction = velocity.toAngle2D();
	direction = cmd->viewangles.y - direction;

	const auto negatedDirection = Vector::fromAngle2D(direction) * -450;
	cmd->forwardmove = negatedDirection.x;
	cmd->sidemove = negatedDirection.y;
}

void Misc::allCvar()noexcept
{
	if (config->misc.allCvar)
	{
		auto iterator = interfaces->cvar->factoryInternalIterator();
		for (iterator->setFirst(); iterator->isValid(); iterator->next()) {
			auto cmdBase = iterator->get();
			if (cmdBase->isFlagSet(FCVAR_DEVELOPMENTONLY | FCVAR_HIDDEN))
				cmdBase->removeFlags(FCVAR_DEVELOPMENTONLY | FCVAR_HIDDEN);
		}
	}
}
void Misc::stealNames() noexcept
{
	if (!config->griefing.nameStealer)
		return;

	if (!localPlayer)
		return;

	static std::vector<int> stolenIds;

	for (int i = 1; i <= memory->globalVars->maxClients; ++i)
	{
		const auto entity = interfaces->entityList->getEntity(i);

		if (!entity || entity == localPlayer.get())
			continue;

		PlayerInfo playerInfo;
		if (!interfaces->engine->getPlayerInfo(entity->index(), playerInfo))
			continue;

		if (playerInfo.fakeplayer || std::find(stolenIds.cbegin(), stolenIds.cend(), playerInfo.userId) != stolenIds.cend())
			continue;

		if (changeName(false, (std::string{playerInfo.name} + '\x1').c_str(), 1.0f))
			stolenIds.emplace_back(playerInfo.userId);

		return;
	}
	stolenIds.clear();
	
}

void Misc::quickReload(UserCmd *cmd) noexcept
{
	if (config->misc.quickReload)
	{
		static Entity *reloadedWeapon{nullptr};

		if (reloadedWeapon)
		{
			for (auto weaponHandle : localPlayer->weapons())
			{
				if (weaponHandle == -1)
					break;

				if (interfaces->entityList->getEntityFromHandle(weaponHandle) == reloadedWeapon)
				{
					cmd->weaponSelect = reloadedWeapon->index();
					cmd->weaponSubtype = reloadedWeapon->getWeaponSubType();
					break;
				}
			}
			reloadedWeapon = nullptr;
		}

		if (auto activeWeapon = localPlayer->getActiveWeapon(); activeWeapon && activeWeapon->isInReload() && activeWeapon->clip() == activeWeapon->getWeaponData()->maxClip)
		{
			reloadedWeapon = activeWeapon;

			for (auto weaponHandle : localPlayer->weapons())
			{
				if (weaponHandle == -1)
					break;

				if (auto weapon = interfaces->entityList->getEntityFromHandle(weaponHandle); weapon && weapon != reloadedWeapon)
				{
					cmd->weaponSelect = weapon->index();
					cmd->weaponSubtype = weapon->getWeaponSubType();
					break;
				}
			}
		}
	}
}

bool Misc::changeName(bool reconnect, const char *newName, float delay) noexcept
{
	static auto exploitInitialized = false;

	static auto name = interfaces->cvar->findVar("name");

	if (reconnect)
	{
		exploitInitialized = false;
		return false;
	}

	if (!exploitInitialized && interfaces->engine->isInGame())
	{
		if (PlayerInfo playerInfo; localPlayer && interfaces->engine->getPlayerInfo(localPlayer->index(), playerInfo) && (!strcmp(playerInfo.name, "?empty") || !strcmp(playerInfo.name, "\n\xAD\xAD\xAD")))
		{
			exploitInitialized = true;
		} else
		{
			name->onChangeCallbacks.size = 0;
			name->setValue("\n\xAD\xAD\xAD");
			return false;
		}
	}

	static auto nextChangeTime{0.0f};
	if (nextChangeTime <= memory->globalVars->realTime)
	{
		name->setValue(newName);
		nextChangeTime = memory->globalVars->realTime + delay;
		return true;
	}
	return false;
}

void Misc::edgeBug(UserCmd* cmd) noexcept
{
	if (static Helpers::KeyBindState flag; !flag[config->movement.edgeBug])
		return;

	if (!localPlayer || !localPlayer->isAlive())
		return;

	if (const auto mt = localPlayer->moveType(); mt == MoveType::Noclip || mt == MoveType::Ladder)
		return;

	const auto localPlayer2 = localPlayer.get();
	float max_radias = M_PI * 2;
	float step = max_radias / 128;
	float xThick = 23;

	if (localPlayer->flags() & 1)
	{
		Vector pos = localPlayer->origin();
		for (float a = 0.f; a < max_radias; a += step)
		{
			Vector pt;
			pt.x = (xThick * cos(a)) + pos.x;
			pt.y = (xThick * sin(a)) + pos.y;
			pt.z = pos.z;

			Vector pt2 = pt;
			pt2.z -= 6;

			Trace trace;

			TraceFilter flt = localPlayer2;

			interfaces->engineTrace->traceRay({ pt, pt2 }, 0x1400B, flt, trace);

			if (trace.fraction != 1.0f && trace.fraction != 0.0f)
			{
				cmd->buttons |= UserCmd::Button_Duck;
			}
		}
		for (float a = 0.f; a < max_radias; a += step)
		{
			Vector pt;
			pt.x = ((xThick - 2.f) * cos(a)) + pos.x;
			pt.y = ((xThick - 2.f) * sin(a)) + pos.y;
			pt.z = pos.z;

			Vector pt2 = pt;
			pt2.z -= 6;

			Trace trace;

			TraceFilter flt = localPlayer2;
			interfaces->engineTrace->traceRay({ pt, pt2 }, 0x1400B, flt, trace);

			if (trace.fraction != 1.f && trace.fraction != 0.f)
			{
				if (config->exploits.fastDuck)
					cmd->buttons &= ~UserCmd::Button_Bullrush;
				cmd->buttons |= UserCmd::Button_Duck;
			}
		}
		for (float a = 0.f; a < max_radias; a += step)
		{
			Vector pt;
			pt.x = ((xThick - 20.f) * cos(a)) + pos.x;
			pt.y = ((xThick - 20.f) * sin(a)) + pos.y;
			pt.z = pos.z;

			Vector pt2 = pt;
			pt2.z -= 6;

			Trace trace;

			TraceFilter flt = localPlayer2;
			interfaces->engineTrace->traceRay({ pt, pt2 }, 0x1400B, flt, trace);

			if (trace.fraction != 1.f && trace.fraction != 0.f)
			{
				if (config->exploits.fastDuck)
					cmd->buttons &= ~UserCmd::Button_Bullrush;
				cmd->buttons |= UserCmd::Button_Duck;
			}
		}
	}
}

void Misc::jumpBug(UserCmd* cmd) noexcept
{
	if (static Helpers::KeyBindState flag; !flag[config->movement.jumpBug])
		return;

	if (!localPlayer || !localPlayer->isAlive())
		return;

	if (const auto mt = localPlayer->moveType(); mt == MoveType::Noclip || mt == MoveType::Ladder)
		return;

	if (!(EnginePrediction::getFlags() & 1) && (localPlayer->flags() & 1))
	{
		if (config->exploits.fastDuck)
			cmd->buttons &= ~UserCmd::Button_Bullrush;
		cmd->buttons |= UserCmd::Button_Duck;
	}

	if (localPlayer->flags() & 1)
		cmd->buttons &= ~UserCmd::Button_Jump;
}

void Misc::bunnyHop(UserCmd* cmd) noexcept
{

	static bool hasLanded = true;
	static int bhopInSeries = 1;
	static float lastTimeInAir{};
	static int chanceToHit = config->movement.bunnyChance;
	static auto wasLastTimeOnGround{ localPlayer->flags() & PlayerFlag_OnGround };

	chanceToHit = config->movement.bunnyChance;

	if (bhopInSeries <= 1) {
		chanceToHit = (int)(chanceToHit * 1.5f);
	}

	if (!localPlayer || !localPlayer->isAlive())
		return;

	if (localPlayer->moveType() == MoveType::Noclip || localPlayer->moveType() == MoveType::Ladder
		|| localPlayer->flags() & PlayerFlag_InWater || localPlayer->flags() & PlayerFlag_WaterJump)
		return;

	if (static Helpers::KeyBindState flag; flag[config->movement.jumpBug])
		return;

	static float previousTime = memory->globalVars->realTime;
	static bool fakeSlow = false;

	if (static Helpers::KeyBindState flag; flag[config->movement.bunnyHop] && !(localPlayer->flags() & PlayerFlag_OnGround) && !wasLastTimeOnGround)
	{
		if (rand() % 100 <= chanceToHit && !fakeSlow) {
			cmd->buttons &= ~UserCmd::Button_Jump;
		}
		else {
			fakeSlow = true;
		}
	}
		

	if (config->movement.humanize && fakeSlow && memory->globalVars->realTime > previousTime + 0.05f) {
		cmd->buttons &= ~UserCmd::Button_Jump;
		fakeSlow = false;
		previousTime = memory->globalVars->realTime;
	}

	if (!wasLastTimeOnGround && hasLanded) {
		bhopInSeries++;
		lastTimeInAir = memory->globalVars->realTime;
		hasLanded = false;
	}
	if (wasLastTimeOnGround) {
		hasLanded = true;
		if (memory->globalVars->realTime - lastTimeInAir >= 3) {
			bhopInSeries = 0;
		}
	}

	wasLastTimeOnGround = localPlayer->flags() & PlayerFlag_OnGround;
}

void Misc::fakeBan() noexcept
{
	if (interfaces->engine->isInGame())
		interfaces->engine->clientCmdUnrestricted(("playerchatwheel . \"Cheer! \xe2\x80\xa8" + std::string{static_cast<char>(config->griefing.banColor + 1)} + config->griefing.banText + "\"").c_str());
}

void Misc::changeConVarsTick() noexcept
{
	static auto tracerVar = interfaces->cvar->findVar("cl_weapon_debug_show_accuracy");
	tracerVar->onChangeCallbacks.size = 0;
	tracerVar->setValue(config->visuals.accuracyTracers);

	static auto impactsVar = interfaces->cvar->findVar("sv_showimpacts");
	impactsVar->onChangeCallbacks.size = 0;
	impactsVar->setValue(config->visuals.bulletImpacts);

	static auto nadeVar = interfaces->cvar->findVar("cl_grenadepreview");
	nadeVar->onChangeCallbacks.size = 0;

	if (!config->misc.nadePredict2 || config->misc.mixedNade)
		nadeVar->setValue(config->misc.nadePredict);
	else
		nadeVar->setValue(false);

	static auto fullBright = interfaces->cvar->findVar("mat_fullbright");
	fullBright->onChangeCallbacks.size = 0;
	fullBright->setValue(config->misc.fullBright);

	static auto shadowVar = interfaces->cvar->findVar("cl_csm_enabled");
	shadowVar->setValue(!config->visuals.noShadows);

	static auto lerpVar = interfaces->cvar->findVar("cl_interpolate");
	lerpVar->setValue(true);

	static auto exrpVar = interfaces->cvar->findVar("cl_extrapolate");
	exrpVar->setValue(!config->misc.noExtrapolate);
	static auto ragdollGravity = interfaces->cvar->findVar("cl_ragdoll_gravity");
	ragdollGravity->setValue(config->visuals.inverseRagdollGravity ? -600 : 600);
}

static void oppositeHandKnife(FrameStage stage) noexcept
{
	static const auto rightHandVar = interfaces->cvar->findVar("cl_righthand");
	static bool original = rightHandVar->getInt();

	if (!localPlayer)
		return;

	if (stage != FrameStage::RenderStart && stage != FrameStage::RenderEnd)
		return;

	if (!config->visuals.oppositeHandKnife)
	{
		rightHandVar->setValue(original);
		return;
	}

	if (stage == FrameStage::RenderStart)
	{
		if (const auto activeWeapon = localPlayer->getActiveWeapon())
		{
			if (const auto classId = activeWeapon->getClientClass()->classId; classId == ClassId::Knife || classId == ClassId::KnifeGG)
				rightHandVar->setValue(!original);
		}
	} else
	{
		rightHandVar->setValue(original);
	}
}

static void camDist(FrameStage stage)
{
	if (stage == FrameStage::RenderStart)
	{
		static auto distVar = interfaces->cvar->findVar("cam_idealdist");
		static auto curDist = 0.0f;
		if (memory->input->isCameraInThirdPerson)
			curDist = Helpers::approachValSmooth(static_cast<float>(config->visuals.thirdpersonDistance), curDist, memory->globalVars->frameTime * 7.0f);
		else
			curDist = 0.0f;

		distVar->setValue(curDist);
	}
}

void Misc::changeConVarsFrame(FrameStage stage)
{
	switch (stage)
	{
	case FrameStage::Undefined:
		break;
	case FrameStage::Start:
		break;
	case FrameStage::NetUpdateStart:
		break;
	case FrameStage::NetUpdatePostUpdateStart:
		break;
	case FrameStage::NetUpdatePostUpdateEnd:
		break;
	case FrameStage::NetUpdateEnd:
		break;
	case FrameStage::RenderStart:
		static auto jiggleBonesVar = interfaces->cvar->findVar("r_jiggle_bones");
		jiggleBonesVar->setValue(false);
		static auto threadedBoneSetup = interfaces->cvar->findVar("cl_threaded_bone_setup");
		threadedBoneSetup->setValue(true);
		static auto contactShadowsVar = interfaces->cvar->findVar("cl_foot_contact_shadows");
		contactShadowsVar->setValue(false);
		static auto blurVar = interfaces->cvar->findVar("@panorama_disable_blur");
		blurVar->setValue(config->misc.disablePanoramablur);
		static auto lagVar = interfaces->cvar->findVar("cam_ideallag");
		lagVar->setValue(0);
		static auto camVar = interfaces->cvar->findVar("cam_collision");
		camVar->setValue(config->visuals.thirdpersonCollision);
		static auto minVar = interfaces->cvar->findVar("c_mindistance");
		minVar->setValue(-FLT_MAX);
		static auto maxVar = interfaces->cvar->findVar("c_maxdistance");
		maxVar->setValue(FLT_MAX);
		static auto propsVar = interfaces->cvar->findVar("r_DrawSpecificStaticProp");
		propsVar->setValue(config->visuals.props.enabled ? 0 : -1);
		static auto fbrightVar = interfaces->cvar->findVar("r_flashlightbrightness");
		fbrightVar->setValue(config->visuals.flashlightBrightness);
		static auto fdistVar = interfaces->cvar->findVar("r_flashlightfar");
		fdistVar->setValue(config->visuals.flashlightDistance);
		static auto ffovVar = interfaces->cvar->findVar("r_flashlightfov");
		ffovVar->setValue(config->visuals.flashlightFov);
		static auto hairVar = interfaces->cvar->findVar("crosshair");
		hairVar->setValue(config->visuals.forceCrosshair != 2);
		{
			GameData::Lock lock;
			const auto &local = GameData::local();

			static auto shairVar = interfaces->cvar->findVar("weapon_debug_spread_show");
			shairVar->setValue(config->visuals.forceCrosshair == 1 && !local.drawingScope ? 3 : 0);
		}
		break;
	case FrameStage::RenderEnd:
		static auto skyVar = interfaces->cvar->findVar("r_3dsky");
		skyVar->setValue(!config->visuals.no3dSky);
		static auto brightVar = interfaces->cvar->findVar("mat_force_tonemap_scale");
		brightVar->setValue(config->visuals.brightness);
		break;
	}

	camDist(stage);
	oppositeHandKnife(stage);
}

void Misc::quickHealthshot(UserCmd *cmd) noexcept
{
	if (!localPlayer)
		return;

	static bool inProgress{false};

	if (GetAsyncKeyState(config->misc.quickHealthshotKey) & 1)
		inProgress = true;

	if (auto activeWeapon{localPlayer->getActiveWeapon()}; activeWeapon && inProgress)
	{
		if (activeWeapon->getClientClass()->classId == ClassId::Healthshot && localPlayer->nextAttack() <= memory->globalVars->serverTime() && activeWeapon->nextPrimaryAttack() <= memory->globalVars->serverTime())
			cmd->buttons |= UserCmd::Button_Attack;
		else
		{
			for (auto weaponHandle : localPlayer->weapons())
			{
				if (weaponHandle == -1)
					break;

				if (const auto weapon{interfaces->entityList->getEntityFromHandle(weaponHandle)}; weapon && weapon->getClientClass()->classId == ClassId::Healthshot)
				{
					cmd->weaponSelect = weapon->index();
					cmd->weaponSubtype = weapon->getWeaponSubType();
					return;
				}
			}
		}
		inProgress = false;
	}
}

void Misc::fixTabletSignal() noexcept
{
	if (config->misc.fixTabletSignal && localPlayer)
	{
		if (auto activeWeapon{localPlayer->getActiveWeapon()}; activeWeapon && activeWeapon->getClientClass()->classId == ClassId::Tablet)
			activeWeapon->tabletReceptionIsBlocked() = false;
	}
}

void Misc::killMessage(GameEvent &event) noexcept
{
	if (!config->griefing.killMessage)
		return;

	if (!localPlayer || !localPlayer->isAlive())
		return;

	if (const auto localUserId = localPlayer->getUserId(); event.getInt("attacker") != localUserId || event.getInt("userid") == localUserId)
		return;

	std::string cmd = "say \"";
	cmd += config->griefing.killMessageString;
	cmd += '"';
	interfaces->engine->clientCmdUnrestricted(cmd.c_str());
}

void Misc::fixMovement(UserCmd *cmd, float yaw) noexcept
{
	if (config->misc.fixMovement)
	{
		float oldYaw = yaw + (yaw < 0.0f ? 360.0f : 0.0f);
		float newYaw = cmd->viewangles.y + (cmd->viewangles.y < 0.0f ? 360.0f : 0.0f);
		float yawDelta = newYaw < oldYaw ? std::fabsf(newYaw - oldYaw) : 360.0f - std::fabsf(newYaw - oldYaw);
		yawDelta = 360.0f - yawDelta;

		const float forwardmove = cmd->forwardmove;
		const float sidemove = cmd->sidemove;
		cmd->forwardmove = std::cos(Helpers::degreesToRadians(yawDelta)) * forwardmove + std::cos(Helpers::degreesToRadians(yawDelta + 90.0f)) * sidemove;
		cmd->sidemove = std::sin(Helpers::degreesToRadians(yawDelta)) * forwardmove + std::sin(Helpers::degreesToRadians(yawDelta + 90.0f)) * sidemove;
	}
}

void Misc::soundESP() noexcept
{
	if (Helpers::KeyBindState flag; !flag[config->sound.soundESP.keybind])
		return;

	if (!localPlayer || !localPlayer->isAlive())
		return;

	auto viewAngle = interfaces->engine->getViewAngles();
	auto target = Helpers::getTargetNoWall(viewAngle, config->sound.soundESP.teammates, config->sound.soundESP.fov, config->sound.soundESP.distance);

	if (target)
	{
		static float previousTime = memory->globalVars->realTime;
		if (memory->globalVars->realTime < previousTime + 0.276f)
			return;
		previousTime = memory->globalVars->realTime;
		
		if (const auto soundprecache = interfaces->networkStringTableContainer->findTable("soundprecache"))
			soundprecache->addString(false, "buttons/blip2.wav");

		interfaces->surface->playSound("buttons/blip2.wav");
	}
}

void Misc::antiAfkKick(UserCmd *cmd) noexcept
{
	if (config->exploits.antiAfkKick && cmd->commandNumber % 2)
		cmd->buttons |= 1 << 27;
}

void Misc::fixMouseDelta(UserCmd* cmd) noexcept
{
	if (!config->misc.fixMouseDelta)
		return;

	if (!cmd)
		return;

	static Vector delta_viewangles{ };
	Vector delta = cmd->viewangles - delta_viewangles;

	delta.x = std::clamp(delta.x, -89.0f, 89.0f);
	delta.y = std::clamp(delta.y, -180.0f, 180.0f);
	delta.z = 0.0f;
	static ConVar* sensitivity;
	if (!sensitivity)
		sensitivity = interfaces->cvar->findVar("sensitivity");
	if (delta.x != 0.f) {
		static ConVar* m_pitch;

		if (!m_pitch)
			m_pitch = interfaces->cvar->findVar("m_pitch");

		int final_dy = static_cast<int>((delta.x / m_pitch->getFloat()) / sensitivity->getFloat());
		if (final_dy <= 32767) {
			if (final_dy >= -32768) {
				if (final_dy >= 1 || final_dy < 0) {
					if (final_dy <= -1 || final_dy > 0)
						final_dy = final_dy;
					else
						final_dy = -1;
				}
				else {
					final_dy = 1;
				}
			}
			else {
				final_dy = 32768;
			}
		}
		else {
			final_dy = 32767;
		}

		cmd->mousedy = static_cast<short>(final_dy);
	}

	if (delta.y != 0.f) {
		static ConVar* m_yaw;

		if (!m_yaw)
			m_yaw = interfaces->cvar->findVar("m_yaw");

		int final_dx = static_cast<int>((delta.y / m_yaw->getFloat()) / sensitivity->getFloat());
		if (final_dx <= 32767) {
			if (final_dx >= -32768) {
				if (final_dx >= 1 || final_dx < 0) {
					if (final_dx <= -1 || final_dx > 0)
						final_dx = final_dx;
					else
						final_dx = -1;
				}
				else {
					final_dx = 1;
				}
			}
			else {
				final_dx = 32768;
			}
		}
		else {
			final_dx = 32767;
		}

		cmd->mousedx = static_cast<short>(final_dx);
	}

	delta_viewangles = cmd->viewangles;
}

void Misc::tweakPlayerAnimations() noexcept
{
	if (!config->misc.fixAnimationLOD && !config->misc.resolveLby)
		return;

	for (int i = 1; i <= interfaces->engine->getMaxClients(); i++)
	{
		Entity* entity = interfaces->entityList->getEntity(i);

		if (!entity || entity == localPlayer.get() || entity->isDormant() || !entity->isAlive()) continue;

		if (config->misc.fixAnimationLOD)
		{
			*reinterpret_cast<int*>(entity + 0xA28) = 0;
			*reinterpret_cast<int*>(entity + 0xA30) = memory->globalVars->frameCount;
		}

		if (config->misc.resolveLby)
			Animations::resolveDesync(entity);
	}
}

void Misc::autoPistol(UserCmd *cmd) noexcept
{
	if (config->misc.autoPistol && localPlayer->isAlive() && localPlayer->activeWeapon())
	{
		const auto activeWeapon = localPlayer->getActiveWeapon();
		if (activeWeapon && !activeWeapon->isC4() && localPlayer->shotsFired() > 0 && activeWeapon->nextPrimaryAttack() <= memory->globalVars->serverTime() + memory->globalVars->intervalPerTick && !activeWeapon->isGrenade())
		{
			if (activeWeapon->itemDefinitionIndex2() == WeaponId::Revolver)
				cmd->buttons &= ~UserCmd::Button_Attack2;
			else if (!activeWeapon->isFullAuto())
				cmd->buttons &= ~UserCmd::Button_Attack;
		}
	}
}

void Misc::autoReload(UserCmd *cmd) noexcept
{
	if (config->misc.autoReload && localPlayer)
	{
		const auto activeWeapon = localPlayer->getActiveWeapon();
		if (activeWeapon && getWeaponIndex(activeWeapon->itemDefinitionIndex2()) && !activeWeapon->clip())
			cmd->buttons &= ~(UserCmd::Button_Attack | UserCmd::Button_Attack2);
	}
}

void Misc::revealRanks(UserCmd *cmd) noexcept
{
	if (config->misc.revealRanks && cmd->buttons & UserCmd::Button_Score)
		interfaces->client->dispatchUserMessage(50, 0, 0, nullptr);
}

void Misc::autoStrafe(UserCmd *cmd) noexcept
{
	if (!config->movement.autoStrafe)
		return;

	if (!localPlayer || localPlayer->moveType() == MoveType::Noclip || localPlayer->moveType() == MoveType::Ladder || localPlayer->flags() & PlayerFlag_InWater || localPlayer->flags() & PlayerFlag_WaterJump)
		return;

	static bool lastHeldJump = cmd->buttons & UserCmd::Button_Jump;
	if (~cmd->buttons & UserCmd::Button_Jump && !lastHeldJump)
		return;

	const float speed = localPlayer->velocity().length2D();
	if (speed < 5.0f)
		return;

	constexpr auto perfectDelta = [](float speed) noexcept
	{
		static auto speedVar = interfaces->cvar->findVar("sv_maxspeed");
		static auto airVar = interfaces->cvar->findVar("sv_airaccelerate");
		static auto wishVar = interfaces->cvar->findVar("sv_air_max_wishspeed");

		const auto term = wishVar->getFloat() / airVar->getFloat() / speedVar->getFloat() * 100.0f / speed;

		if (term < 1.0f && term > -1.0f)
			return std::acosf(term);

		return 0.0f;
	};

	const float pDelta = perfectDelta(speed);
	if (pDelta)
	{
		const float yaw = Helpers::degreesToRadians(cmd->viewangles.y);
		const float velDir = std::atan2f(localPlayer->velocity().y, localPlayer->velocity().x) - yaw;
		const float wishAng = std::atan2f(-cmd->sidemove, cmd->forwardmove);
		const float delta = Helpers::angleDiffRad(velDir, wishAng);

		float moveDir = delta < 0.0f ? velDir + pDelta : velDir - pDelta;

		cmd->forwardmove = std::cosf(moveDir) * 450.0f;
		cmd->sidemove = -std::sinf(moveDir) * 450.0f;
	}

	lastHeldJump = cmd->buttons & UserCmd::Button_Jump;
}

void Misc::removeCrouchCooldown(UserCmd *cmd) noexcept
{
	if (config->exploits.fastDuck)
		cmd->buttons |= UserCmd::Button_Bullrush;
}

void Misc::moonwalk(UserCmd *cmd) noexcept
{
	if (!localPlayer || localPlayer->moveType() == MoveType::Ladder)
		return;

	cmd->buttons &= ~(UserCmd::Button_Forward | UserCmd::Button_Back | UserCmd::Button_MoveLeft | UserCmd::Button_MoveRight);
	if (cmd->forwardmove > 0.0f)
		cmd->buttons |= UserCmd::Button_Forward;
	else if (cmd->forwardmove < 0.0f)
		cmd->buttons |= UserCmd::Button_Back;
	if (cmd->sidemove > 0.0f)
		cmd->buttons |= UserCmd::Button_MoveRight;
	else if (cmd->sidemove < 0.0f)
		cmd->buttons |= UserCmd::Button_MoveLeft;

	if (config->exploits.moonwalk)
	{
		cmd->buttons ^= UserCmd::Button_Forward | UserCmd::Button_Back | UserCmd::Button_MoveLeft | UserCmd::Button_MoveRight;
	}
}

void Misc::playHitSound(GameEvent &event) noexcept
{
	if (!config->sound.hitSound)
		return;

	if (!localPlayer)
		return;

	if (const auto localUserId = localPlayer->getUserId(); event.getInt("attacker") != localUserId || event.getInt("userid") == localUserId)
		return;

	constexpr std::array hitSounds = {
		"physics/metal/metal_solid_impact_bullet2.wav",
		"buttons/arena_switch_press_02.wav",
		"training/timer_bell.wav",
		"physics/glass/glass_impact_bullet1.wav"
	};

	if (static_cast<std::size_t>(config->sound.hitSound - 1) < hitSounds.size())
	{
		if (const auto soundprecache = interfaces->networkStringTableContainer->findTable("soundprecache"))
			soundprecache->addString(false, hitSounds[config->sound.hitSound - 1]);

		interfaces->surface->playSound(hitSounds[config->sound.hitSound - 1]);
	} else if (config->sound.hitSound == 5)
	{
		if (const auto soundprecache = interfaces->networkStringTableContainer->findTable("soundprecache"))
			soundprecache->addString(false, config->sound.customHitSound.c_str());

		interfaces->surface->playSound(config->sound.customHitSound.c_str());
	}
}

void Misc::playKillSound(GameEvent &event) noexcept
{
	if (!config->sound.killSound)
		return;

	if (!localPlayer || !localPlayer->isAlive())
		return;

	if (const auto localUserId = localPlayer->getUserId(); event.getInt("attacker") != localUserId || event.getInt("userid") == localUserId)
		return;

	constexpr std::array killSounds = {
		"physics/metal/metal_solid_impact_bullet2.wav",
		"buttons/arena_switch_press_02.wav",
		"training/timer_bell.wav",
		"physics/glass/glass_impact_bullet1.wav"
	};

	if (static_cast<std::size_t>(config->sound.killSound - 1) < killSounds.size())
	{
		if (const auto soundprecache = interfaces->networkStringTableContainer->findTable("soundprecache"))
			soundprecache->addString(false, killSounds[config->sound.killSound - 1]);

		interfaces->surface->playSound(killSounds[config->sound.killSound - 1]);
	} else if (config->sound.killSound == 5)
	{
		if (const auto soundprecache = interfaces->networkStringTableContainer->findTable("soundprecache"))
			soundprecache->addString(false, config->sound.customKillSound.c_str());

		interfaces->surface->playSound(config->sound.customKillSound.c_str());
	}
}

void Misc::playDeathSound(GameEvent &event) noexcept
{
	if (!config->sound.deathSound)
		return;

	if (!localPlayer || !localPlayer->isAlive())
		return;

	if (const auto localUserId = localPlayer->getUserId(); event.getInt("userid") != localUserId)
		return;

	constexpr std::array killSounds = {
		"physics/metal/metal_solid_impact_bullet2.wav",
		"buttons/arena_switch_press_02.wav",
		"training/timer_bell.wav",
		"physics/glass/glass_impact_bullet1.wav"
	};

	if (static_cast<std::size_t>(config->sound.deathSound - 1) < killSounds.size())
	{
		if (const auto soundprecache = interfaces->networkStringTableContainer->findTable("soundprecache"))
			soundprecache->addString(false, killSounds[config->sound.deathSound - 1]);

		interfaces->surface->playSound(killSounds[config->sound.deathSound - 1]);
	} else if (config->sound.deathSound == 5)
	{
		if (const auto soundprecache = interfaces->networkStringTableContainer->findTable("soundprecache"))
			soundprecache->addString(false, config->sound.customDeathSound.c_str());

		interfaces->surface->playSound(config->sound.customDeathSound.c_str());
	}
}

static std::vector<std::uint64_t> reportedPlayers;
static int reportbotRound;

void Misc::runReportbot() noexcept
{
	if (!config->griefing.reportbot.enabled)
		return;

	if (!localPlayer)
		return;

	static auto lastReportTime = 0.0f;

	if (lastReportTime + config->griefing.reportbot.delay > memory->globalVars->realTime)
		return;

	if (reportbotRound >= config->griefing.reportbot.rounds)
		return;

	for (int i = 1; i <= interfaces->engine->getMaxClients(); ++i)
	{
		const auto entity = interfaces->entityList->getEntity(i);

		if (!entity || entity == localPlayer.get())
			continue;

		if (config->griefing.reportbot.target != 2 && (localPlayer->isOtherEnemy(entity) ? config->griefing.reportbot.target != 0 : config->griefing.reportbot.target != 1))
			continue;

		PlayerInfo playerInfo;
		if (!interfaces->engine->getPlayerInfo(i, playerInfo))
			continue;

		if (playerInfo.fakeplayer || std::find(reportedPlayers.cbegin(), reportedPlayers.cend(), playerInfo.xuid) != reportedPlayers.cend())
			continue;

		std::string report;

		if (config->griefing.reportbot.textAbuse)
			report += "textabuse,";
		if (config->griefing.reportbot.griefing)
			report += "grief,";
		if (config->griefing.reportbot.wallhack)
			report += "wallhack,";
		if (config->griefing.reportbot.aimbot)
			report += "aimbot,";
		if (config->griefing.reportbot.other)
			report += "speedhack,";

		if (!report.empty())
		{
			memory->submitReport(std::to_string(playerInfo.xuid).c_str(), report.c_str());
			lastReportTime = memory->globalVars->realTime;
			reportedPlayers.emplace_back(playerInfo.xuid);
		}
		return;
	}

	reportedPlayers.clear();
	++reportbotRound;
}

void Misc::resetReportbot() noexcept
{
	reportbotRound = 0;
	reportedPlayers.clear();
}

void Misc::preserveKillfeed(bool roundStart) noexcept
{
	if (!config->misc.preserveKillfeed.enabled)
		return;

	static auto nextUpdate = 0.0f;

	if (roundStart)
	{
		nextUpdate = memory->globalVars->realTime + 10.0f;
		return;
	}

	if (nextUpdate > memory->globalVars->realTime)
		return;

	nextUpdate = memory->globalVars->realTime + 2.0f;

	const auto deathNotice = memory->findHudElement(memory->hud, "CCSGO_HudDeathNotice");
	if (!deathNotice)
		return;

	const auto deathNoticePanel = (*(UIPanel **)(*(deathNotice - 5 + 22) + 4));
	const auto childPanelCount = deathNoticePanel->getChildCount();

	for (int i = 0; i < childPanelCount; ++i)
	{
		const auto child = deathNoticePanel->getChild(i);
		if (!child)
			continue;

		if (child->hasClass("DeathNotice_Killer") && (!config->misc.preserveKillfeed.onlyHeadshots || child->hasClass("DeathNoticeHeadShot")))
			child->setAttributeFloat("SpawnTime", memory->globalVars->currentTime);
	}
}

static int blockTargetHandle = 0;

void Misc::blockBot(UserCmd *cmd, const Vector &currentViewAngles) noexcept
{
	if (!localPlayer || !localPlayer->isAlive())
		return;

	float best = 255.0f;
	if (static Helpers::KeyBindState flag; flag[config->griefing.blockbot.target])
	{
		for (int i = 1; i <= interfaces->engine->getMaxClients(); i++)
		{
			Entity *entity = interfaces->entityList->getEntity(i);

			if (!entity || !entity->isPlayer() || entity == localPlayer.get() || entity->isDormant() || !entity->isAlive())
				continue;

			const auto angle = Helpers::calculateRelativeAngle(localPlayer->getEyePosition(), entity->getEyePosition(), currentViewAngles);
			const auto fov = std::hypot(angle.x, angle.y);

			if (fov < best)
			{
				best = fov;
				blockTargetHandle = entity->handle();
			}
		}
	}

	if (static Helpers::KeyBindState flag; !flag[config->griefing.blockbot.bind]) return;

	const auto target = interfaces->entityList->getEntityFromHandle(blockTargetHandle);
	if (target && target->isPlayer() && target != localPlayer.get() && !target->isDormant() && target->isAlive())
	{
		const auto targetVec = (target->getAbsOrigin() + target->velocity() * memory->globalVars->intervalPerTick * config->griefing.blockbot.trajectoryFac - localPlayer->getAbsOrigin()) * config->griefing.blockbot.distanceFac;
		const auto z1 = target->getAbsOrigin().z - localPlayer->getEyePosition().z;
		const auto z2 = target->getEyePosition().z - localPlayer->getAbsOrigin().z;
		if (z1 >= 0.0f || z2 <= 0.0f)
		{
			Vector fwd = Vector::fromAngle2D(cmd->viewangles.y);
			Vector side = fwd.crossProduct(Vector::up());
			Vector move = Vector{fwd.dotProduct2D(targetVec), side.dotProduct2D(targetVec), 0.0f};
			move *= 45.0f;

			const float l = move.length2D();
			if (l > 450.0f)
				move *= 450.0f / l;

			cmd->forwardmove = move.x;
			cmd->sidemove = move.y;
		} else
		{
			Vector fwd = Vector::fromAngle2D(cmd->viewangles.y);
			Vector side = fwd.crossProduct(Vector::up());
			Vector tar = (targetVec / targetVec.length2D()).crossProduct(Vector::up());
			tar = tar.snapTo4();
			tar *= tar.dotProduct2D(targetVec);
			Vector move = Vector{fwd.dotProduct2D(tar), side.dotProduct2D(tar), 0.0f};
			move *= 45.0f;

			const float l = move.length2D();
			if (l > 450.0f)
				move *= 450.0f / l;

			cmd->forwardmove = move.x;
			cmd->sidemove = move.y;
		}
	}
}

void Misc::visualizeBlockBot(ImDrawList *drawList) noexcept
{
	if (!config->griefing.blockbot.visualize.enabled)
		return;

	GameData::Lock lock;
	const auto &local = GameData::local();

	if (!local.exists || !local.alive)
		return;

	auto target = GameData::playerByHandle(blockTargetHandle);
	if (!target || target->dormant || !target->alive)
		return;

	Vector max = target->obbMaxs + target->origin;
	Vector min = target->obbMins + target->origin;
	const auto z = target->origin.z;

	ImVec2 points[4];
	const auto color = Helpers::calculateColor(config->griefing.blockbot.visualize);

	bool draw = Helpers::worldToScreen(Vector{max.x, max.y, z}, points[0]);
	draw = draw && Helpers::worldToScreen(Vector{max.x, min.y, z}, points[1]);
	draw = draw && Helpers::worldToScreen(Vector{min.x, min.y, z}, points[2]);
	draw = draw && Helpers::worldToScreen(Vector{min.x, max.y, z}, points[3]);

	if (draw)
	{
		drawList->AddLine(points[0], points[1], color, config->griefing.blockbot.visualize.thickness);
		drawList->AddLine(points[1], points[2], color, config->griefing.blockbot.visualize.thickness);
		drawList->AddLine(points[2], points[3], color, config->griefing.blockbot.visualize.thickness);
		drawList->AddLine(points[3], points[0], color, config->griefing.blockbot.visualize.thickness);
	}
}

void Misc::useSpam(UserCmd *cmd) noexcept
{
	if (!localPlayer || !localPlayer->isAlive())
		return;

	if (static Helpers::KeyBindState flag; !flag[config->griefing.spamUse])
		return;

	static auto plantAnywhere = interfaces->cvar->findVar("mp_plant_c4_anywhere");

	if (plantAnywhere->getInt())
		return;

	if (localPlayer->inBombZone() && localPlayer->flags() & PlayerFlag_OnGround)
		return;

	if (cmd->buttons & UserCmd::Button_Use)
	{
		static bool flag = false;
		if (flag)
			cmd->buttons |= UserCmd::Button_Use;
		else
			cmd->buttons &= ~UserCmd::Button_Use;
		flag = !flag;
	}
}

void Misc::indicators() noexcept
{
	if (!config->misc.indicators.enabled)
		return;

	GameData::Lock lock;
	const auto &local = GameData::local();

	if ((!local.exists || !local.alive) && !gui->open)
		return;

	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
	if (!gui->open)
		windowFlags |= ImGuiWindowFlags_NoInputs;

	ImGui::SetNextWindowPos({0, 50}, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2{200.0f, 0.0f}, ImVec2{200.0f, FLT_MAX});
	ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, {0.5f, 0.5f});
	ImGui::Begin("Indicators", nullptr, windowFlags);
	ImGui::PopStyleVar();
	
	if (!local.exists)
	{
		ImGui::TextWrapped("Shows things like choked packets, height, speed and shot statistics");
		ImGui::End();
		return;
	}

	const auto networkChannel = interfaces->engine->getNetworkChannel();
	if (networkChannel)
	{
		static auto maxChokeVar = interfaces->cvar->findVar("sv_maxusrcmdprocessticks");
		ImGui::TextUnformatted("Choked packets");
		ImGuiCustom::progressBarFullWidth(static_cast<float>(networkChannel->chokedPackets) / maxChokeVar->getInt());
	}

	ImGui::TextUnformatted("Height");
	ImGuiCustom::progressBarFullWidth((local.eyePosition.z - local.origin.z - PLAYER_EYE_HEIGHT_CROUCH) / (PLAYER_EYE_HEIGHT - PLAYER_EYE_HEIGHT_CROUCH));

	ImGui::Text("Speed %.0fu", local.velocity.length2D());

	ImGui::Text("In %s person", memory->input->isCameraInThirdPerson ? "third" : "first");

	ImGui::Text("Last shot: %s", Backtrack::lastShotLagRecord() ? "lag record" : "modern record");
	ImGui::Text("Target misses: %d", Aimbot::getMisses());

	ImGui::End();
}

void Misc::drawBombTimer() noexcept
{
	if (!config->misc.bombTimer.enabled)
		return;

	if (!interfaces->engine->isConnected())
		return;

	GameData::Lock lock;
	const auto &plantedC4 = GameData::plantedC4();
	if (!plantedC4.blowTime && !gui->open)
		return;

	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav;
	if (!gui->open)
		windowFlags |= ImGuiWindowFlags_NoInputs;

	static float windowWidth = 500.0f;
	ImGui::SetNextWindowPos({(ImGui::GetIO().DisplaySize.x - 500) / 2, 160}, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize({500, 0}, ImGuiCond_FirstUseEver);
	if (!gui->open) ImGui::SetNextWindowSize({windowWidth, 0}, ImGuiCond_Always);
	ImGui::SetNextWindowSizeConstraints({200, -1}, {FLT_MAX, -1});
	ImGui::Begin("Bomb timer", nullptr, windowFlags | (gui->open ? 0 : ImGuiWindowFlags_NoInputs));

	constexpr auto bombsite = [](int i)
	{
		switch (i)
		{
		case 0:
			return 'A';
		case 1:
			return 'B';
		default:
			return '?';
		}
	};

	std::ostringstream ss;
	ss << "Bomb on " << bombsite(plantedC4.bombsite) << " " << std::fixed << std::showpoint << std::setprecision(3) << (std::max)(plantedC4.blowTime - memory->globalVars->currentTime, 0.0f) << "s";

	ImGuiCustom::textUnformattedCentered(ss.str().c_str());

	ImGuiCustom::progressBarFullWidth((plantedC4.blowTime - memory->globalVars->currentTime) / plantedC4.timerLength);

	if (plantedC4.defuserHandle != -1)
	{
		const bool canDefuse = plantedC4.blowTime >= plantedC4.defuseCountDown;

		if (plantedC4.defuserHandle == GameData::local().handle)
		{
			std::ostringstream ss; ss << "Defusing... " << std::fixed << std::showpoint << std::setprecision(3) << (std::max)(plantedC4.defuseCountDown - memory->globalVars->currentTime, 0.0f) << "s";

			ImGuiCustom::textUnformattedCentered(ss.str().c_str());
		} else if (auto playerData = GameData::playerByHandle(plantedC4.defuserHandle))
		{
			std::ostringstream ss; ss << playerData->name << " is defusing... " << std::fixed << std::showpoint << std::setprecision(3) << (std::max)(plantedC4.defuseCountDown - memory->globalVars->currentTime, 0.0f) << "s";

			ImGuiCustom::textUnformattedCentered(ss.str().c_str());
		}

		ImGuiCustom::progressBarFullWidth((plantedC4.defuseCountDown - memory->globalVars->currentTime) / plantedC4.defuseLength);

		if (canDefuse)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 0, 255));
			ImGuiCustom::textUnformattedCentered("CAN DEFUSE");
			ImGui::PopStyleColor();
		} else
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
			ImGuiCustom::textUnformattedCentered("CANNOT DEFUSE");
			ImGui::PopStyleColor();
		}
	}

	windowWidth = ImGui::GetCurrentWindow()->SizeFull.x;
	ImGui::End();
}

void Misc::purchaseList(GameEvent *event) noexcept
{
	static std::mutex mtx;
	std::scoped_lock _{mtx};

	struct PlayerPurchases
	{
		int totalCost;
		std::unordered_map<std::string, int> items;
	};

	static std::unordered_map<int, PlayerPurchases> playerPurchases;
	static std::unordered_map<std::string, int> purchaseTotal;
	static int totalCost;

	static auto freezeEnd = 0.0f;

	if (event)
	{
		switch (fnv::hashRuntime(event->getName()))
		{
		case fnv::hash("item_purchase"):
		{
			const auto player = interfaces->entityList->getEntity(interfaces->engine->getPlayerFromUserID(event->getInt("userid")));

			if (player && localPlayer && localPlayer->isOtherEnemy(player))
			{
				if (const auto definition = memory->itemSystem()->getItemSchema()->getItemDefinitionByName(event->getString("weapon")))
				{
					auto &purchase = playerPurchases[player->handle()];
					if (const auto weaponInfo = memory->weaponSystem->getWeaponInfo(definition->getWeaponId()))
					{
						purchase.totalCost += weaponInfo->price;
						totalCost += weaponInfo->price;
					}
					const std::string weapon = interfaces->localize->findAsUTF8(definition->getItemBaseName());
					++purchaseTotal[weapon];
					++purchase.items[weapon];
				}
			}
			break;
		}
		case fnv::hash("round_start"):
			freezeEnd = 0.0f;
			playerPurchases.clear();
			purchaseTotal.clear();
			totalCost = 0;
			break;
		case fnv::hash("round_freeze_end"):
			freezeEnd = memory->globalVars->realTime;
			break;
		}
	} else
	{
		if (!config->misc.purchaseList.enabled)
			return;

		static const auto buyTimeVar = interfaces->cvar->findVar("mp_buytime");

		if ((!interfaces->engine->isInGame() || freezeEnd && memory->globalVars->realTime > freezeEnd + (!config->misc.purchaseList.onlyDuringFreezeTime ? buyTimeVar->getFloat() : 0.0f) || playerPurchases.empty() || purchaseTotal.empty()) && !gui->open)
			return;

		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
		if (config->misc.purchaseList.noTitleBar)
			windowFlags |= ImGuiWindowFlags_NoTitleBar;
		if (!gui->open)
			windowFlags |= ImGuiWindowFlags_NoInputs;

		ImGui::SetNextWindowSize({200, 200}, ImGuiCond_FirstUseEver);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, {0.5f, 0.5f});
		ImGui::Begin("Purchases", nullptr, windowFlags);
		ImGui::PopStyleVar();

		if (config->misc.purchaseList.mode == Config::Misc::PurchaseList::Details) {
			if (ImGui::BeginTable("table", 3, ImGuiTableFlags_Hideable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide, 100.0f);
				ImGui::TableSetupColumn("Price", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
				ImGui::TableSetupColumn("Purchases", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetColumnEnabled(1, config->misc.purchaseList.showPrices);

				GameData::Lock lock;

				for (const auto& [handle, purchases] : playerPurchases) {
					std::string s;
					s.reserve(std::accumulate(purchases.items.begin(), purchases.items.end(), 0, [](int length, const auto& p) { return length + p.first.length() + 2; }));
					for (const auto& purchasedItem : purchases.items) {
						if (purchasedItem.second > 1)
							s += std::to_string(purchasedItem.second) + "x ";
						s += purchasedItem.first + ", ";
					}

					if (s.length() >= 2)
						s.erase(s.length() - 2);

					ImGui::TableNextRow();

					if (const auto it = std::ranges::find(GameData::players(), handle, &PlayerData::handle); it != GameData::players().cend()) {
						if (ImGui::TableNextColumn())
							ImGuiCustom::textEllipsisInTableCell(it->name.c_str());
						if (ImGui::TableNextColumn())
							ImGui::TextColored({ 0.0f, 0.5f, 0.0f, 1.0f }, "$%d", purchases.totalCost);
						if (ImGui::TableNextColumn())
							ImGui::TextWrapped("%s", s.c_str());
					}
				}

				ImGui::EndTable();
			}
		}
		else if (config->misc.purchaseList.mode == Config::Misc::PurchaseList::Summary) {
			for (const auto& purchase : purchaseTotal)
				ImGui::TextWrapped("%dx %s", purchase.second, purchase.first.c_str());

			if (config->misc.purchaseList.showPrices && totalCost > 0) {
				ImGui::Separator();
				ImGui::TextWrapped("Total: $%d", totalCost);
			}
		}
		ImGui::End();
	}
}

void Misc::statusBar() noexcept
{
	auto& cfg = config->misc.Sbar;
	if (cfg.enabled == false)
		return;

	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse;

	if (!localPlayer && !gui->open)
		return;

	ImGui::SetNextWindowSize(ImVec2(200.0f, 200.0f), ImGuiCond_Once);
	ImGui::Begin("Status Bar", nullptr, windowFlags);
	if (localPlayer && localPlayer->isAlive()) {
		if (cfg.showPlayerRealViewAngles) {
			ImGui::Text("Pitch: %.1f", interfaces->engine->getViewAngles().x);
			ImGui::Text("Yaw: %.1f", interfaces->engine->getViewAngles().y);
		}

		if (cfg.showPlayerStatus)
		{
			std::string message = "Local Player: ";
			auto local = GameData::local();

			if (localPlayer->flags() & PlayerFlag_OnGround)
				message += "On Gound\n";
			if (!(localPlayer->flags() & PlayerFlag_OnGround))
				message += "In Air\n";
			if (localPlayer->flags() & PlayerFlag_Crouched)
				message += "Crouched\n";
			if (localPlayer->flags() & PlayerFlag_GodMode)
				message += "God Mode\n";
			if (localPlayer->flags() & PlayerFlag_OnFire)
				message += "On Fire\n";
			if (localPlayer->flags() & PlayerFlag_Swim)
				message += "Swimming\n";
			if (localPlayer->isDefusing())
				message += "Defusing\n";
			if (localPlayer->isFlashed())
				message += "Flashed\n";
			if (localPlayer->inBombZone())
				message += "In Bomb Zone\n";
			if (local.shooting)
				message += "Shooting\n";

			ImGui::Text(message.c_str());

		}

		if (cfg.showGameGlobalVars) {
			ImGui::Text("CurTime: %.1f", memory->globalVars->currentTime);
			ImGui::Text("RealTime: %.1f", memory->globalVars->realTime);
		}
	}
	ImGui::End();
}

void Misc::playerList() noexcept
{
	if (!config->misc.playerList)
		return;

	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse || ImGuiWindowFlags_NoTitleBar || ImGuiWindowFlags_NoResize;

	if (!interfaces->engine->isConnected() && !gui->open)
		return;

	auto playerResource = *memory->playerResource;

	if (localPlayer && playerResource)
	{
		ImGui::SetNextWindowSize(ImClamp(ImVec2{}, {}, ImGui::GetIO().DisplaySize));
		ImGui::Begin("Player List", nullptr, windowFlags);
		if (ImGui::BeginTable("playerList", 11, ImGuiTableFlags_Borders | ImGuiTableFlags_Hideable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable))
		{
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide, 120.0f);
			ImGui::TableSetupColumn("Wins", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
			ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
			ImGui::TableSetupColumn("Ranking", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
			ImGui::TableSetupColumn("SteamID", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
			ImGui::TableSetupColumn("UserID", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
			ImGui::TableSetupColumn("Money", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
			ImGui::TableSetupColumn("Health", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
			ImGui::TableSetupColumn("Armor", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
			ImGui::TableSetupColumn("Last Place", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
			ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
			ImGui::TableHeadersRow();

			ImGui::TableNextRow();
			ImGui::PushID(ImGui::TableGetRowIndex());

			GameData::Lock lock;

			std::vector<std::reference_wrapper<const PlayerData>> playersOrdered{ GameData::players().begin(), GameData::players().end() };
			std::ranges::sort(playersOrdered, [](const PlayerData& a, const PlayerData& b) 
			{
			// enemies first
			if (a.enemy != b.enemy)
				return a.enemy && !b.enemy;

			return a.handle < b.handle;
			});

			for (auto& player : playersOrdered)
			{
				ImGui::TableNextRow();
				ImGui::PushID(ImGui::TableGetRowIndex());

				auto* entity = interfaces->entityList->getEntityFromHandle(player.get().handle);
				if (!entity) continue;

				if (ImGui::TableNextColumn())
					entity->team() == Team::CT ? ImGui::TextColored({ 0.0f, 0.2f, 1.0f, 1.0f }, entity->getPlayerName().c_str())
					: entity->team() == Team::TT ? ImGui::TextColored({ 0.5f, 0.5f, 0.0f, 1.0f }, entity->getPlayerName().c_str())
					: ImGui::Text(entity->getPlayerName().c_str());

				if (ImGui::TableNextColumn())
				{
					if (entity->isBot())
						ImGui::Text("BOT");
					else
						ImGui::Text("%d", playerResource->competitiveWins()[entity->index()]);
				}

				if (ImGui::TableNextColumn())
				{
					if (entity->isBot())
						ImGui::Text("BOT");
					else
						ImGui::Text("%d", playerResource->level()[entity->index()]);
				}

				if (ImGui::TableNextColumn())
				{
					if (entity->isBot())
						ImGui::Text("BOT");
					else
						ImGui::Text(interfaces->localize->findAsUTF8(("RankName_" + std::to_string(playerResource->competitiveRanking()[entity->index()])).c_str()));
				}

				if (ImGui::TableNextColumn())
				{
					if (entity->isBot())
						ImGui::Text("BOT");
					else
					{
						ImGui::Text("%llu", entity->getSteamID());
						if (ImGui::SameLine(); ImGui::SmallButton("Copy"))
							ImGui::SetClipboardText(std::to_string(entity->getSteamID()).c_str());
					}
				}

				if (ImGui::TableNextColumn())
					ImGui::Text("%d", entity->getUserId());

				if (ImGui::TableNextColumn())
					ImGui::TextColored({ 0.0f, 0.5f, 0.0f, 1.0f }, "$%d", entity->account());

				if (ImGui::TableNextColumn())
				{
					if (!entity->isAlive())
						ImGui::TextColored({ 1.0f, 0.0f, 0.0f, 1.0f }, "%s", "DEAD");
					else
						ImGui::Text("%d", entity->health());
				}

				if (ImGui::TableNextColumn())
					ImGui::Text("%d", entity->armor());

				if (ImGui::TableNextColumn())
					ImGui::Text("%s", entity->isAlive() && entity->lastPlaceName() ? interfaces->localize->findAsUTF8(entity->lastPlaceName()) : "Unknown");

				if (ImGui::TableNextColumn())
					ImGui::Text("%.1f", localPlayer->origin().distTo(entity->origin()));
			}
			
			ImGui::EndTable();
		}
	}
	ImGui::End();
}

void Misc::selfNade(UserCmd* cmd)
{
	if (!GetAsyncKeyState(config->misc.selfNade))
		return;

	if (!interfaces->engine->isInGame() || !interfaces->engine->isConnected())
		return;

	if (!localPlayer || !localPlayer->isAlive())
		return;

	auto activeWeapon = localPlayer->getActiveWeapon();


	if (activeWeapon)
	{
		if (!activeWeapon->isGrenade())
			return;

		auto throwStrength = activeWeapon->throwStrength();

		if (throwStrength > 0.10f)
			cmd->buttons |= UserCmd::Button_Attack2;

		if (throwStrength < 0.11f) 
			cmd->buttons |= UserCmd::Button_Attack;

		if (throwStrength >= 0.11f || throwStrength <= 0.10f)
			return;

		cmd->viewangles.x = 89.0f;
		cmd->buttons &= ~UserCmd::Button_Attack;
		cmd->buttons &= ~UserCmd::Button_Attack2;
	}
}

void Misc::damageList(GameEvent* event) noexcept
{
	if (!config->misc.damageList.enabled)
		return;

	static std::mutex mtx;
	std::scoped_lock _{ mtx };

	static std::unordered_map<int, int> damageCount;
	static std::unordered_map<int, int> damagedBy;

	if (event) {
		switch (fnv::hashRuntime(event->getName())) {
		case fnv::hash("round_start"):
			damageCount.clear();
			damagedBy.clear();
			break;
		case fnv::hash("player_hurt"):
			if (const auto localPlayerId = localPlayer ? localPlayer->getUserId() : 0; localPlayerId && event->getInt("attacker") == localPlayerId && event->getInt("userid") != localPlayerId)
				if (const auto player = interfaces->entityList->getEntity(interfaces->engine->getPlayerFromUserID(event->getInt("userid"))); player)
					damageCount[player->handle()] += event->getInt("dmg_health");
			if (const auto localPlayerId = localPlayer ? localPlayer->getUserId() : 0; event->getInt("userid") == localPlayerId)
				if (const auto player = interfaces->entityList->getEntity(interfaces->engine->getPlayerFromUserID(event->getInt("attacker"))); player)
					damagedBy[player->handle()] += event->getInt("dmg_health");
			break;
		}
	}
	else {

		if (!interfaces->engine->isConnected())
		{
			damageCount.clear();
			damagedBy.clear();
			return;
		}

		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
		if (config->misc.damageList.noTitleBar)
			windowFlags |= ImGuiWindowFlags_NoTitleBar;

		ImGui::SetNextWindowSize({ 200, 200 }, ImGuiCond_FirstUseEver);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, { 0.5f, 0.5f });
		ImGui::Begin("Damage list", nullptr, windowFlags);
		ImGui::PopStyleVar();

		std::vector<std::pair<int, int>> damageList(damageCount.cbegin(), damageCount.cend());
		std::ranges::sort(damageList, std::ranges::greater{}, &std::pair<int, int>::second);
		std::vector<std::pair<int, int>> damageList2(damagedBy.cbegin(), damagedBy.cend());
		std::ranges::sort(damageList2, std::ranges::greater{}, &std::pair<int, int>::second);
		GameData::Lock lock;

		for (const auto & [handle, damage] : damageList) {
			if (damage == 0)
				continue;
			if (const auto playerData = GameData::playerByHandle(handle)) {
				const auto textSize = ImGui::CalcTextSize(playerData->name.c_str());
				ImGui::TextWrapped("You to %s: ", playerData->name.c_str());
				ImGui::SameLine(0.f, 1.f);
				ImGui::TextWrapped("Dmg: %d | Health: %d", damage, playerData->health);
			}
		}
		for (const auto & [handle, damage] : damageList2) {
			if (damage == 0)
				continue;
			if (const auto playerData = GameData::playerByHandle(handle)) {
				const auto textSize = ImGui::CalcTextSize(playerData->name.c_str());
				ImGui::TextWrapped("%s to you: ", playerData->name.c_str());
				ImGui::SameLine(0.f, 1.f);
				ImGui::TextWrapped("Dmg: %d", damage);
			}
		}
		ImGui::End();
	}
}

void Misc::teamDamageList(GameEvent *event)
{
	static std::mutex mtx;
	std::scoped_lock _{mtx};

	static std::unordered_map<int, std::pair<unsigned, unsigned>> damageList;

	if (event)
	{
		switch (fnv::hashRuntime(event->getName()))
		{
		case fnv::hash("player_hurt"):
		{
			const auto attacker = interfaces->entityList->getEntity(interfaces->engine->getPlayerFromUserID(event->getInt("attacker")));
			const auto player = interfaces->entityList->getEntity(interfaces->engine->getPlayerFromUserID(event->getInt("userid")));

			if (attacker && player && localPlayer && !localPlayer->isOtherEnemy(attacker) && !player->isOtherEnemy(attacker) && player->handle() != attacker->handle())
				damageList[attacker->handle()].first += event->getInt("dmg_health");

			break;
		}
		case fnv::hash("player_death"):
		{
			const auto attacker = interfaces->entityList->getEntity(interfaces->engine->getPlayerFromUserID(event->getInt("attacker")));
			const auto player = interfaces->entityList->getEntity(interfaces->engine->getPlayerFromUserID(event->getInt("userid")));

			if (attacker && player && localPlayer && !localPlayer->isOtherEnemy(attacker) && !player->isOtherEnemy(attacker) && player->handle() != attacker->handle())
				damageList[attacker->handle()].second++;

			break;
		}
		case fnv::hash("cs_match_end_restart"):
			damageList.clear();
			break;
		}
	} else
	{
		if (!config->misc.teamDamageList.enabled)
			return;

		if (!interfaces->engine->isInGame())
			damageList.clear();

		if (!gui->open && (damageList.empty() || !interfaces->engine->isInGame()))
			return;

		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
		if (config->misc.teamDamageList.noTitleBar)
			windowFlags |= ImGuiWindowFlags_NoTitleBar;
		if (!gui->open)
			windowFlags |= ImGuiWindowFlags_NoInputs;

		ImGui::SetNextWindowSize({200, 200}, ImGuiCond_FirstUseEver);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, {0.5f, 0.5f});
		ImGui::Begin("Team damage list", nullptr, windowFlags);
		ImGui::PopStyleVar();

		GameData::Lock lock;

		for (const auto &[handle, info] : damageList)
		{
			if (const auto player = GameData::playerByHandle(handle))
				ImGui::Text("%s -> %idmg %i%s", player->name.c_str(), info.first, info.second, info.second == 1 ? "kill" : "kills");
			else if (GameData::local().handle == handle)
				ImGui::TextColored({1.0f, 0.7f, 0.2f, 1.0f}, "YOU -> %idmg %i%s", info.first, info.second, info.second == 1 ? "kill" : "kills");

			if (config->misc.teamDamageList.progressBars)
			{
				ImGuiCustom::progressBarFullWidth(static_cast<float>(info.first) / 300);
				ImGuiCustom::progressBarFullWidth(static_cast<float>(info.second) / 3);
			}
		}

		ImGui::End();
	}
}

//void Misc::spectatorList() noexcept
//{
//	if (!config->misc.spectatorList.enabled)
//		return;
//
//	if (!localPlayer || !localPlayer->isAlive())
//		return;
//
//	interfaces->surface->setTextFont(Surface::font);
//
//	const auto [width, height] = interfaces->surface->getScreenSize();
//
//	auto textPositionY = static_cast<int>(0.5f * height);
//
//	for (int i = 1; i <= interfaces->engine->getMaxClients(); ++i)
//	{
//		const auto entity = interfaces->entityList->getEntity(i);
//		if (!entity || entity->isDormant() || entity->isAlive() || entity->getObserverTarget() != localPlayer.get())
//			continue;
//
//		PlayerInfo playerInfo;
//
//		if (!interfaces->engine->getPlayerInfo(i, playerInfo))
//			continue;
//
//		if (wchar_t name[128]; MultiByteToWideChar(CP_UTF8, 0, playerInfo.name, -1, name, 128))
//		{
//			const auto [textWidth, textHeight] = interfaces->surface->getTextSize(Surface::font, name);
//
//			interfaces->surface->setTextColor(0, 0, 0, 100);
//
//			interfaces->surface->setTextPosition(width - textWidth - 5, textPositionY + 2);
//			interfaces->surface->printText(name);
//			interfaces->surface->setTextPosition(width - textWidth - 6, textPositionY + 1);
//			interfaces->surface->printText(name);
//
//			if (config->misc.spectatorList.rainbow)
//				interfaces->surface->setTextColor(Helpers::rainbowColor(config->misc.spectatorList.rainbowSpeed));
//			else
//				interfaces->surface->setTextColor(config->misc.spectatorList.color);
//
//			interfaces->surface->setTextPosition(width - textWidth - 7, textPositionY);
//			interfaces->surface->printText(name);
//
//			textPositionY += textHeight;
//		}
//	}
//
//	const auto title = L"Spectators";
//
//	const auto [titleWidth, titleHeight] = interfaces->surface->getTextSize(Surface::font, title);
//	textPositionY = static_cast<int>(0.5f * height);
//
//	interfaces->surface->setDrawColor(0, 0, 0, 127);
//
//	interfaces->surface->drawFilledRect(width - titleWidth - 15, textPositionY - titleHeight - 12, width, textPositionY);
//
//	if (config->misc.spectatorList.rainbow)
//		interfaces->surface->setDrawColor(Helpers::rainbowColor(config->misc.spectatorList.rainbowSpeed), 255);
//	else
//		interfaces->surface->setDrawColor(static_cast<int>(config->misc.spectatorList.color[0] * 255), static_cast<int>(config->misc.spectatorList.color[1] * 255), static_cast<int>(config->misc.spectatorList.color[2] * 255), 255);
//
//	interfaces->surface->drawOutlinedRect(width - titleWidth - 15, textPositionY - titleHeight - 12, width, textPositionY);
//
//	interfaces->surface->setTextColor(0, 0, 0, 100);
//
//	interfaces->surface->setTextPosition(width - titleWidth - 5, textPositionY - titleHeight - 5);
//	interfaces->surface->printText(title);
//
//	interfaces->surface->setTextPosition(width - titleWidth - 6, textPositionY - titleHeight - 6);
//	interfaces->surface->printText(title);
//
//	if (config->misc.spectatorList.rainbow)
//		interfaces->surface->setTextColor(Helpers::rainbowColor(config->misc.spectatorList.rainbowSpeed));
//	else
//		interfaces->surface->setTextColor(config->misc.spectatorList.color);
//
//	interfaces->surface->setTextPosition(width - titleWidth - 7, textPositionY - titleHeight - 7);
//	interfaces->surface->printText(title);
//}
//
//void Misc::watermark() noexcept
//{
//	if (config->misc.watermark.enabled)
//	{
//		interfaces->surface->setTextFont(Surface::font);
//
//		const auto watermark = L"Welcome to NEPS.PP";
//
//		static auto frameRate = 1.0f;
//		frameRate = 0.9f * frameRate + 0.1f * memory->globalVars->absoluteFrameTime;
//		const auto [screenWidth, screenHeight] = interfaces->surface->getScreenSize();
//		std::wstring fps{std::to_wstring(static_cast<int>(1 / frameRate)) + L" fps"};
//		const auto [fpsWidth, fpsHeight] = interfaces->surface->getTextSize(Surface::font, fps.c_str());
//
//		float latency = 0.0f;
//		if (auto networkChannel = interfaces->engine->getNetworkChannel(); networkChannel && networkChannel->getLatency(0) > 0.0f)
//			latency = networkChannel->getLatency(0);
//
//		std::wstring ping{L"Ping: " + std::to_wstring(static_cast<int>(latency * 1000)) + L" ms"};
//		const auto [pingWidth, pingHeight] = interfaces->surface->getTextSize(Surface::font, ping.c_str());
//
//		const auto [waterWidth, waterHeight] = interfaces->surface->getTextSize(Surface::font, watermark);
//
//		interfaces->surface->setTextColor(0, 0, 0, 100);
//		interfaces->surface->setDrawColor(0, 0, 0, 127);
//
//		interfaces->surface->drawFilledRect(screenWidth - std::max(pingWidth, fpsWidth) - 14, 0, screenWidth, fpsHeight + pingHeight + 12);
//
//		interfaces->surface->setTextPosition(screenWidth - pingWidth - 5, fpsHeight + 6);
//		interfaces->surface->printText(ping.c_str());
//		interfaces->surface->setTextPosition(screenWidth - pingWidth - 6, fpsHeight + 7);
//		interfaces->surface->printText(ping.c_str());
//
//		interfaces->surface->setTextPosition(screenWidth - fpsWidth - 5, 6);
//		interfaces->surface->printText(fps.c_str());
//		interfaces->surface->setTextPosition(screenWidth - fpsWidth - 6, 7);
//		interfaces->surface->printText(fps.c_str());
//
//		interfaces->surface->drawFilledRect(0, 0, waterWidth + 14, waterHeight + 11);
//
//		interfaces->surface->setTextPosition(5, 6);
//		interfaces->surface->printText(watermark);
//		interfaces->surface->setTextPosition(6, 7);
//		interfaces->surface->printText(watermark);
//
//		if (config->misc.watermark.rainbow)
//			interfaces->surface->setTextColor(Helpers::rainbowColor(config->misc.watermark.rainbowSpeed));
//		else
//			interfaces->surface->setTextColor(config->misc.watermark.color);
//
//		interfaces->surface->setTextPosition(screenWidth - pingWidth - 7, fpsHeight + 5);
//		interfaces->surface->printText(ping.c_str());
//
//		interfaces->surface->setTextPosition(screenWidth - fpsWidth - 7, 5);
//		interfaces->surface->printText(fps.c_str());
//
//		interfaces->surface->setTextPosition(7, 5);
//		interfaces->surface->printText(watermark);
//
//		if (config->misc.watermark.rainbow)
//			interfaces->surface->setDrawColor(Helpers::rainbowColor(config->misc.watermarks.rainbowSpeed));
//		else
//			interfaces->surface->setDrawColor(static_cast<int>(config->misc.watermark.color[0] * 255), static_cast<int>(config->misc.watermark.color[1] * 255), static_cast<int>(config->misc.watermark.color[2] * 255), 255);
//
//		interfaces->surface->drawOutlinedRect(screenWidth - std::max(pingWidth, fpsWidth) - 14, 0, screenWidth, fpsHeight + pingHeight + 12);
//		interfaces->surface->drawOutlinedRect(0, 0, waterWidth + 14, waterHeight + 11);
//	}
//}

void Misc::spectatorList() noexcept
{
	if (!config->misc.spectatorList.enabled)
		return;

	if (!localPlayer)
		return;

	std::vector<const char *> observers;

	GameData::Lock lock;
	for (auto &observer : GameData::observers())
	{
		if (observer.targetIsObservedByLocalPlayer || observer.targetIsLocalPlayer)
			observers.emplace_back(observer.name);
	}

	if (!observers.empty() || gui->open)
	{
		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
		if (!gui->open)
			windowFlags |= ImGuiWindowFlags_NoInputs;

		ImGui::SetNextWindowPos(ImVec2{ImGui::GetIO().DisplaySize.x - 200, ImGui::GetIO().DisplaySize.y / 2 - 20}, ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSizeConstraints({200, 0}, ImVec2{FLT_MAX, FLT_MAX});
		ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, {0.5f, 0.5f});
		ImGui::Begin("Spectators", nullptr, windowFlags);
		ImGui::PopStyleVar();

		for (auto &observer : observers)
			ImGui::TextUnformatted(observer);

		ImGui::End();
	}
}

void Misc::watermark() noexcept
{
	if (!config->misc.watermark.enabled)
		return;

	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
	if (!gui->open)
		windowFlags |= ImGuiWindowFlags_NoInputs;

	ImGui::SetNextWindowSizeConstraints({160, 0}, {FLT_MAX, FLT_MAX});
	ImGui::SetNextWindowBgAlpha(0.4f);
	ImGui::Begin("Watermark", nullptr, windowFlags);

	int &pos = config->misc.watermark.position;

	switch (pos)
	{
	case 0:
		ImGui::SetWindowPos({10, 10}, ImGuiCond_Always);
		break;
	case 1:
		ImGui::SetWindowPos({ImGui::GetIO().DisplaySize.x - ImGui::GetWindowSize().x - 10, 10}, ImGuiCond_Always);
		break;
	case 2:
		ImGui::SetWindowPos(ImGui::GetIO().DisplaySize - ImGui::GetWindowSize() - ImVec2{10, 10}, ImGuiCond_Always);
		break;
	case 3:
		ImGui::SetWindowPos({10.0f, ImGui::GetIO().DisplaySize.y - ImGui::GetWindowSize().y - 10}, ImGuiCond_Always);
		break;
	}

	if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseClicked[1])
		ImGui::OpenPopup("##pos_sel");

	if (gui->open && ImGui::BeginPopup("##pos_sel", ImGuiWindowFlags_NoMove))
	{
		bool selected = pos == 0;
		if (ImGui::MenuItem("Top left", nullptr, selected)) pos = 0;
		selected = pos == 1;
		if (ImGui::MenuItem("Top right", nullptr, selected)) pos = 1;
		selected = pos == 2;
		if (ImGui::MenuItem("Bottom right", nullptr, selected)) pos = 2;
		selected = pos == 3;
		if (ImGui::MenuItem("Bottom left", nullptr, selected)) pos = 3;
		ImGui::EndPopup();
	}

	constexpr std::array otherOnes = {"gamesense", "neverlose", "aimware", "onetap", "advancedaim", "flowhooks", "ratpoison", "osiris", "rifk7", "novoline", "novihacks", "ev0lve", "ezfrags", "pandora", "luckycharms", "weave", "legendware", "spirthack", "mutinty", "monolith"};

	std::ostringstream watermark;
	watermark << "NEPS > ";
	watermark << otherOnes[static_cast<int>(memory->globalVars->realTime) % otherOnes.size()];
	ImGui::TextUnformatted(watermark.str().c_str());

	static float frameRate = 1.0f;
	frameRate = 0.9f * frameRate + 0.1f * memory->globalVars->absoluteFrameTime;
	ImGui::Text("%.0ffps", 1.0f / frameRate);

	GameData::Lock lock;
	const auto session = GameData::session();

	if (session.connected && !session.levelName.empty())
	{
		ImGui::SameLine(55.0f);
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
		ImGui::SameLine();

		ImGui::Text("%dms", session.latency);

		ImGui::SameLine(105.0f);
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
		ImGui::SameLine();

		ImGui::Text("%dtps", session.tickrate);

		ImGui::SameLine();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
		ImGui::SameLine();

		ImGui::TextUnformatted(session.levelName.c_str());

		ImGui::SameLine();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
		ImGui::SameLine();

		ImGui::TextUnformatted(session.address.c_str());

		ImGui::SameLine();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
		ImGui::SameLine();

		ImGui::TextUnformatted("Connected");
	} else
		ImGui::TextUnformatted("Not Connected");
	ImGui::End();

}

void Misc::velocityGraph() noexcept
{
	// Upcoming
}

void Misc::onPlayerVote(GameEvent &event) noexcept
{
	if (!config->griefing.revealVotes)
		return;

	const auto entity = interfaces->entityList->getEntity(event.getInt("entityid"));
	if (!entity || !entity->isPlayer())
		return;

	const auto votedYes = event.getInt("vote_option") == 0;
	const char color = votedYes ? '\x4' : '\x2';
	const auto isLocal = localPlayer && entity == localPlayer.get();

	memory->clientMode->getHudChat()->printf(0, " \x1[NEPS]\x8 %s voted %c%s\x1", isLocal ? "\x10YOU\x8" : entity->getPlayerName().c_str(), color, votedYes ? "YES" : "NO");
}

void Misc::onVoteChange(UserMessageType type, const void *data, int size) noexcept
{
	if (!config->griefing.revealVotes)
		return;

	switch (type)
	{
	case UserMessageType::VoteStart:
	{
		if (!data || !size) break;

		constexpr auto voteName = [](int index)
		{
			switch (index)
			{
			case 0: return "kicking a player";
			case 1: return "changing the level";
			case 6: return "surrendering";
			case 13: return "starting a timeout";
			default: return "unknown action";
			}
		};

		const auto reader = ProtobufReader{static_cast<const std::uint8_t *>(data), size};
		const auto entityIndex = reader.readInt32(2);

		const auto entity = interfaces->entityList->getEntity(entityIndex);
		if (!entity || !entity->isPlayer())
			return;

		const auto isLocal = localPlayer && entity == localPlayer.get();
		const auto voteType = reader.readInt32(3);

		memory->clientMode->getHudChat()->printf(0, " \x1[NEPS]\x8 %s started a vote for\x1 %s", isLocal ? "\x10YOU\x8" : entity->getPlayerName().c_str(), voteName(voteType));
	}
		break;
	case UserMessageType::VotePass:
		memory->clientMode->getHudChat()->printf(0, " \x1[NEPS]\x8 Vote\x4 PASSED\x1");
		break;
	case UserMessageType::VoteFailed:
		memory->clientMode->getHudChat()->printf(0, " \x1[NEPS]\x8 Vote\x2 FAILED\x1");
		break;
	}
}

void Misc::forceRelayCluster() noexcept
{
	std::string dataCentersList[] = {"", "syd", "vie", "gru", "scl", "dxb", "par", "fra", "hkg",
	"maa", "bom", "tyo", "lux", "ams", "limc", "man", "waw", "sgp", "jnb",
	"mad", "sto", "lhr", "atl", "eat", "ord", "lax", "mwh", "okc", "sea", "iad"};

	*memory->relayCluster = dataCentersList[config->misc.forceRelayCluster];
}

void Misc::runChatSpammer() noexcept
{
	static float previousTime = memory->globalVars->realTime;
	if (memory->globalVars->realTime < previousTime + 0.1f)
		return;
	previousTime = memory->globalVars->realTime;

	constexpr auto nuke = "say \xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9";
	constexpr auto basmala = "say \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD";

	if (static Helpers::KeyBindState flag; flag[config->griefing.chatSpammer.keyBind])
	{
		if (config->griefing.chatSpammer.chatNuke)
			interfaces->engine->clientCmdUnrestricted(nuke);
		
		if (config->griefing.chatSpammer.chatBasmala)
			interfaces->engine->clientCmdUnrestricted(basmala);

		if (config->griefing.chatSpammer.custom)
		{
			std::string cmd = "say \"";
			cmd += config->griefing.chatSpammer.text;
			cmd += '"';
			interfaces->engine->clientCmdUnrestricted(cmd.c_str());
		}
			
	}


}

void Misc::fakePrime() noexcept
{
	static bool lastState = false;

	if (config->griefing.fakePrime != lastState)
	{
		lastState = config->griefing.fakePrime;

		#ifdef _WIN32
		if (DWORD oldProtect; VirtualProtect(memory->fakePrime, 4, PAGE_EXECUTE_READWRITE, &oldProtect))
		{
			constexpr uint8_t patch[]{0x31, 0xC0, 0x40, 0xC3};
			std::memcpy(memory->fakePrime, patch, 4);
			VirtualProtect(memory->fakePrime, 4, oldProtect, nullptr);
		}
		#endif
	}
}
