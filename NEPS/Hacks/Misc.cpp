﻿#include "Misc.h"

#include "Animations.h"
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
		if (memory->globalVars->realtime - lastTime < 1.0f)
			return;

		const auto time = std::time(nullptr);
		const auto localTime = std::localtime(&time);
		char s[11];
		s[0] = '\0';
		sprintf_s(s, "[%02d:%02d:%02d]", localTime->tm_hour, localTime->tm_min, localTime->tm_sec);

		lastTime = memory->globalVars->realtime;
		memory->setClanTag(s, s);
	} else if (config->griefing.customClanTag)
	{
		if (memory->globalVars->realtime - lastTime < 0.6f)
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

		lastTime = memory->globalVars->realtime;
		memory->setClanTag(clanTagBuffer.c_str(), clanTagBuffer.c_str());
	} else
	{
		if (memory->globalVars->realtime - lastTime < 0.6f)
			return;

		lastTime = memory->globalVars->realtime;
		memory->setClanTag("", "");
	}
}

struct customCmd
{
	float forwardmove;
	float sidemove;
	float upmove;
};

bool hasShot;
Vector quickPeekStartPos;
ImVec2 drawPos;
std::vector<customCmd>usercmdQuickpeek;
int qpCount;

void Misc::drawQuickPeekStartPos() noexcept
{
	if (!Helpers::worldToScreen(quickPeekStartPos, drawPos))
		return;

	if (quickPeekStartPos != Vector{ 0, 0, 0 }) {
		interfaces->surface->setDrawColor(255, 255, 255);
		interfaces->surface->drawCircle(drawPos.x, drawPos.y, 0, 10);
	}
}

void gotoStart(UserCmd* cmd) {
	if (usercmdQuickpeek.empty()) return;
	if (hasShot)
	{
		if (qpCount > 0)
		{
			cmd->upmove = -usercmdQuickpeek.at(qpCount).upmove;
			cmd->sidemove = -usercmdQuickpeek.at(qpCount).sidemove;
			cmd->forwardmove = -usercmdQuickpeek.at(qpCount).forwardmove;
			qpCount--;
		}
	}
	else
	{
		qpCount = usercmdQuickpeek.size();
	}
}

void Misc::quickPeek(UserCmd* cmd) noexcept
{
	if (!localPlayer || !localPlayer->isAlive()) return;
	if (GetAsyncKeyState(config->movement.quickPeekKey)) {
		if (quickPeekStartPos == Vector{ 0, 0, 0 }) {
			quickPeekStartPos = localPlayer->getAbsOrigin();
		}
		else {
			customCmd tempCmd = {};
			tempCmd.forwardmove = cmd->forwardmove;
			tempCmd.sidemove = cmd->sidemove;
			tempCmd.upmove = cmd->upmove;

			if (cmd->buttons & UserCmd::Button_Attack) hasShot = true;
			gotoStart(cmd);

			if (!hasShot)
				usercmdQuickpeek.push_back(tempCmd);
		}
	}
	else {
		hasShot = false;
		quickPeekStartPos = Vector{ 0, 0, 0 };
		usercmdQuickpeek.clear();
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

		const auto inaccuracy = std::sqrtf(ImLengthSqr((edge - displaySize / 2) / displaySize)) * 200;
		const auto color = Helpers::calculateColor(config->visuals.inaccuracyCircle);
		char text[0xF];
		std::sprintf(text, "%.6f%%", inaccuracy);
		drawList->AddText(edge, color, text);
		drawList->AddCircleFilled(displaySize / 2, radius, color);
		drawList->AddCircle(displaySize / 2, radius, color | IM_COL32_A_MASK);
	}
}

void Misc::prepareRevolver(UserCmd *cmd) noexcept
{
	constexpr float revolverPrepareTime = 0.234375f;

	static float readyTime;
	if (static Helpers::KeyBindState flag; flag[config->misc.prepareRevolver])
	{
		if (auto activeWeapon = localPlayer->getActiveWeapon(); activeWeapon && activeWeapon->itemDefinitionIndex2() == WeaponId::Revolver)
		{
			if (!readyTime) readyTime = memory->globalVars->serverTime() + revolverPrepareTime;
			auto ticksToReady = Helpers::timeToTicks(readyTime - memory->globalVars->serverTime() - interfaces->engine->getNetworkChannel()->getLatency(0));
			if (ticksToReady > 0 && ticksToReady <= Helpers::timeToTicks(revolverPrepareTime))
				cmd->buttons |= UserCmd::Button_Attack;
			else
				readyTime = 0.0f;
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

	float bombTimer = bomb.blowTime - memory->globalVars->currenttime;
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

	auto duration = std::chrono::system_clock::now().time_since_epoch();
	long currentTime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
	static long timeStamp = currentTime_ms;

	if (currentTime_ms - timeStamp < 350)
		return;

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
	if (nextChangeTime <= memory->globalVars->realtime)
	{
		name->setValue(newName);
		nextChangeTime = memory->globalVars->realtime + delay;
		return true;
	}
	return false;
}

void Misc::bunnyHop(UserCmd* cmd) noexcept
{

	static bool hasLanded = true;
	static int bhopInSeries = 1;
	static float lastTimeInAir{};
	static int chanceToHit = config->movement.bunnyChance;
	static auto wasLastTimeOnGround{ localPlayer->flags() & 1 };

	chanceToHit = config->movement.bunnyChance;

	if (bhopInSeries <= 1) {
		chanceToHit = chanceToHit * 1.5;
	}

	//config->misc.DEBUG = bhopInSeries;


	if (static Helpers::KeyBindState flag; flag[config->movement.bunnyHop] && !(localPlayer->flags() & PlayerFlag_OnGround) && localPlayer->moveType() != MoveType::Ladder && !wasLastTimeOnGround)
		if (rand() % 100 <= chanceToHit) {
			cmd->buttons &= ~UserCmd::Button_Jump;
		}
	//memory->globalVars->realtime - lastTimeInAir <= 2 &&
	if (!wasLastTimeOnGround && hasLanded) {
		bhopInSeries++;
		lastTimeInAir = memory->globalVars->realtime;
		hasLanded = false;
	}
	if (wasLastTimeOnGround) {
		hasLanded = true;
		if (memory->globalVars->realtime - lastTimeInAir >= 3) {
			bhopInSeries = 0;
		}
	}

	wasLastTimeOnGround = localPlayer->flags() & 1;
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
	nadeVar->setValue(config->misc.nadePredict);
	static auto fullBright = interfaces->cvar->findVar("mat_fullbright");
	fullBright->onChangeCallbacks.size = 0;
	fullBright->setValue(config->misc.fullBright);
	static auto trajectoryVar{ interfaces->cvar->findVar("sv_grenade_trajectory") };
	static auto trajectoryTimeVar{ interfaces->cvar->findVar("sv_grenade_trajectory_time") };
	static auto timeBackup = trajectoryTimeVar->getFloat();
	trajectoryVar->onChangeCallbacks.size = 0;
	trajectoryVar->setValue(config->misc.nadeTrajectory);
	trajectoryTimeVar->onChangeCallbacks.size = 0;
	trajectoryTimeVar->setValue(config->misc.nadeTrajectory ? 12 : timeBackup);
	static auto shadowVar = interfaces->cvar->findVar("cl_csm_enabled");
	shadowVar->setValue(!config->visuals.noShadows);
	static auto lerpVar = interfaces->cvar->findVar("cl_interpolate");
	lerpVar->setValue(true);
	static auto exrpVar = interfaces->cvar->findVar("cl_extrapolate");
	exrpVar->setValue(false);
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
			curDist = Helpers::approachValSmooth(static_cast<float>(config->visuals.thirdpersonDistance), curDist, memory->globalVars->frametime * 7.0f);
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

void Misc::antiAfkKick(UserCmd *cmd) noexcept
{
	if (config->exploits.antiAfkKick && cmd->commandNumber % 2)
		cmd->buttons |= 1 << 26;
}

void Misc::tweakPlayerAnimations(FrameStage stage) noexcept
{
	if (stage == FrameStage::RenderStart)
	{
		if (!config->misc.fixAnimationLOD && !config->misc.disableInterp && !config->misc.resolveLby)
			return;

		for (int i = 1; i <= interfaces->engine->getMaxClients(); i++)
		{
			Entity *entity = interfaces->entityList->getEntity(i);

			if (!entity || entity == localPlayer.get() || entity->isDormant() || !entity->isAlive()) continue;

			if (config->misc.fixAnimationLOD)
			{
				*reinterpret_cast<int *>(entity + 0xA28) = 0;
				*reinterpret_cast<int *>(entity + 0xA30) = memory->globalVars->framecount;
			}

			if (config->misc.resolveLby)
				Animations::resolve(entity);

			if (auto varMap = entity->getVarMap(); varMap && config->misc.disableInterp)
				for (int j = 0; j < varMap->entries.size; j++)
					varMap->entries[j].needsToInterpolate = 0;
		}
	}
}

void Misc::autoPistol(UserCmd *cmd) noexcept
{
	if (config->misc.autoPistol && localPlayer->isAlive() && localPlayer->activeWeapon())
	{
		const auto activeWeapon = localPlayer->getActiveWeapon();
		if (activeWeapon && !activeWeapon->isC4() && activeWeapon->nextPrimaryAttack() > memory->globalVars->serverTime() && !activeWeapon->isGrenade())
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

	if (!localPlayer || localPlayer->moveType() == MoveType::Noclip || localPlayer->moveType() == MoveType::Ladder)
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

	if (lastReportTime + config->griefing.reportbot.delay > memory->globalVars->realtime)
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
			lastReportTime = memory->globalVars->realtime;
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
		nextUpdate = memory->globalVars->realtime + 10.0f;
		return;
	}

	if (nextUpdate > memory->globalVars->realtime)
		return;

	nextUpdate = memory->globalVars->realtime + 2.0f;

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
			child->setAttributeFloat("SpawnTime", memory->globalVars->currenttime);
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

	Vector curDir = target->velocity * 0.12f;
	curDir.z = 0.0f;
	Vector max = target->colMaxs + target->origin;
	Vector min = target->colMins + target->origin;
	const auto z = target->origin.z;

	ImVec2 pos, dir;
	ImVec2 points[4];

	const auto color = Helpers::calculateColor(config->griefing.blockbot.visualize);

	bool draw = Helpers::worldToScreen(target->origin, pos);
	draw = draw && Helpers::worldToScreen(curDir + target->origin, dir);

	if (draw)
	{
		drawList->AddLine(pos, dir, color);
		ImGuiCustom::drawText(drawList, 0.0f, 0.0f, color, color & IM_COL32_A_MASK, std::to_string(static_cast<int>(target->velocity.length())).c_str(), dir);
	}

	draw = Helpers::worldToScreen(Vector{max.x, max.y, z}, points[0]);
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
	GameData::Lock lock;
	const auto &local = GameData::local();

	if (!local.exists || !local.alive)
		return;

	if (!config->misc.indicators.enabled)
		return;

	ImGui::SetNextWindowPos({0, 50}, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2{200.0f, 0.0f}, ImVec2{200.0f, FLT_MAX});
	ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, {0.5f, 0.5f});
	ImGui::Begin("Indicators", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | (gui->open ? 0 : ImGuiWindowFlags_NoInputs));
	ImGui::PopStyleVar();
	
	const auto networkChannel = interfaces->engine->getNetworkChannel();
	if (networkChannel)
	{
		ImGui::TextUnformatted("Choked packets");
		ImGuiCustom::progressBarFullWidth(static_cast<float>(networkChannel->chokedPackets) / 16);
	}

	ImGui::TextUnformatted("Height");
	ImGuiCustom::progressBarFullWidth((local.eyePosition.z - local.origin.z - PLAYER_EYE_HEIGHT_CROUCH) / (PLAYER_EYE_HEIGHT - PLAYER_EYE_HEIGHT_CROUCH));

	ImGui::Text("Speed %.0fu", local.velocity.length2D());

	if (memory->input->isCameraInThirdPerson)
		ImGui::TextUnformatted("In thirdperson");

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

	static float windowWidth = 500.0f;
	ImGui::SetNextWindowPos({(ImGui::GetIO().DisplaySize.x - 500.0f) / 2.0f, 160.0f}, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize({windowWidth, 0}, ImGuiCond_FirstUseEver);

	if (!gui->open)
		ImGui::SetNextWindowSize({windowWidth, 0});

	ImGui::SetNextWindowSizeConstraints({200, -1}, {FLT_MAX, -1});
	ImGui::Begin("Bomb timer", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav | (gui->open ? 0 : ImGuiWindowFlags_NoInputs));

	std::ostringstream ss; ss << "Bomb on " << (!plantedC4.bombsite ? 'A' : 'B') << " " << std::fixed << std::showpoint << std::setprecision(3) << (std::max)(plantedC4.blowTime - memory->globalVars->currenttime, 0.0f) << " s";

	ImGuiCustom::textUnformattedCentered(ss.str().c_str());

	ImGuiCustom::progressBarFullWidth((plantedC4.blowTime - memory->globalVars->currenttime) / plantedC4.timerLength);

	if (plantedC4.defuserHandle != -1)
	{
		const bool canDefuse = plantedC4.blowTime >= plantedC4.defuseCountDown;

		if (plantedC4.defuserHandle == GameData::local().handle)
		{
			std::ostringstream ss; ss << "Defusing... " << std::fixed << std::showpoint << std::setprecision(3) << (std::max)(plantedC4.defuseCountDown - memory->globalVars->currenttime, 0.0f) << " s";

			ImGuiCustom::textUnformattedCentered(ss.str().c_str());
		} else if (auto playerData = GameData::playerByHandle(plantedC4.defuserHandle))
		{
			std::ostringstream ss; ss << playerData->name << " is defusing... " << std::fixed << std::showpoint << std::setprecision(3) << (std::max)(plantedC4.defuseCountDown - memory->globalVars->currenttime, 0.0f) << " s";

			ImGuiCustom::textUnformattedCentered(ss.str().c_str());
		}

		ImGuiCustom::progressBarFullWidth((plantedC4.defuseCountDown - memory->globalVars->currenttime) / plantedC4.defuseLength);

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
			freezeEnd = memory->globalVars->realtime;
			break;
		}
	} else
	{
		if (!config->misc.purchaseList.enabled)
			return;

		static const auto buyTimeVar = interfaces->cvar->findVar("mp_buytime");

		if ((!interfaces->engine->isInGame() || freezeEnd && memory->globalVars->realtime > freezeEnd + (!config->misc.purchaseList.onlyDuringFreezeTime ? buyTimeVar->getFloat() : 0.0f) || playerPurchases.empty() || purchaseTotal.empty()) && !gui->open)
			return;

		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
		if (!gui->open)
			windowFlags |= ImGuiWindowFlags_NoInputs;
		if (config->misc.purchaseList.noTitleBar)
			windowFlags |= ImGuiWindowFlags_NoTitleBar;

		ImGui::SetNextWindowSize({200, 200}, ImGuiCond_FirstUseEver);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, {0.5f, 0.5f});
		ImGui::Begin("Purchases", nullptr, windowFlags);
		ImGui::PopStyleVar();

		if (config->misc.purchaseList.mode == Config::Misc::PurchaseList::Details)
		{
			GameData::Lock lock;

			for (const auto &[handle, purchases] : playerPurchases)
			{
				std::string s;
				s.reserve(std::accumulate(purchases.items.begin(), purchases.items.end(), 0, [](int length, const auto &p) { return length + p.first.length() + 2; }));
				for (const auto &purchasedItem : purchases.items)
				{
					if (purchasedItem.second > 1)
						s += std::to_string(purchasedItem.second) + "x ";
					s += purchasedItem.first + ", ";
				}

				if (s.length() >= 2)
					s.erase(s.length() - 2);

				if (const auto player = GameData::playerByHandle(handle))
				{
					if (config->misc.purchaseList.showPrices)
						ImGui::TextWrapped("%s $%d: %s", player->name.c_str(), purchases.totalCost, s.c_str());
					else
						ImGui::TextWrapped("%s: %s", player->name.c_str(), s.c_str());
				}
			}
		} else if (config->misc.purchaseList.mode == Config::Misc::PurchaseList::Summary)
		{
			for (const auto &purchase : purchaseTotal)
				ImGui::TextWrapped("%d x %s", purchase.second, purchase.first.c_str());

			if (config->misc.purchaseList.showPrices && totalCost > 0)
			{
				ImGui::Separator();
				ImGui::TextWrapped("Total: $%d", totalCost);
			}
		}
		ImGui::End();
	}
}

void Misc::teamDamageList(GameEvent *event)
{
	static std::mutex mtx;
	std::scoped_lock _{mtx};

	static std::unordered_map<int, int> damageList;

	if (event)
	{
		switch (fnv::hashRuntime(event->getName()))
		{
		case fnv::hash("player_hurt"):
		{
			const auto attacker = interfaces->entityList->getEntity(interfaces->engine->getPlayerFromUserID(event->getInt("attacker")));
			const auto player = interfaces->entityList->getEntity(interfaces->engine->getPlayerFromUserID(event->getInt("userid")));

			if (attacker && player && localPlayer && !localPlayer->isOtherEnemy(attacker) && !player->isOtherEnemy(attacker))
				damageList[attacker->handle()] += event->getInt("dmg_health");
			break;
		}
		case fnv::hash("cs_match_end_restart"):
			damageList.clear();
			break;
		}
	} else
	{
		if (!interfaces->engine->isConnected())
		{
			damageList.clear();
			return;
		}

		if (!config->griefing.teamDamageList.enabled)
			return;

		if (!interfaces->engine->isInGame())
			return;

		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
		if (config->griefing.teamDamageList.noTitleBar)
			windowFlags |= ImGuiWindowFlags_NoTitleBar;

		ImGui::SetNextWindowSize({200, 200}, ImGuiCond_FirstUseEver);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, {0.5f, 0.5f});
		ImGui::Begin("Team damage list", nullptr, windowFlags);
		ImGui::PopStyleVar();

		GameData::Lock lock;

		for (const auto &[handle, damage] : damageList)
		{
			if (const auto player = GameData::playerByHandle(handle))
				ImGui::Text("%s -> %idp", player->name.c_str(), damage);
			else if (GameData::local().handle == handle)
				ImGui::TextColored({1.0f, 0.7f, 0.2f, 1.0f}, "YOU -> %idp", damage);
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
//			interfaces->surface->setDrawColor(Helpers::rainbowColor(config->misc.watermark.rainbowSpeed));
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
		ImGui::SetNextWindowPos(ImVec2{ImGui::GetIO().DisplaySize.x - 200.0f, ImGui::GetIO().DisplaySize.y / 2 - 20.0f}, ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSizeConstraints(ImVec2{200.0f, 0.0f}, ImVec2{FLT_MAX, FLT_MAX});
		ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, {0.5f, 0.5f});
		ImGui::Begin("Spectators", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | (gui->open ? 0 : ImGuiWindowFlags_NoInputs));
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

	ImGui::SetNextWindowSizeConstraints({160, 0}, {FLT_MAX, FLT_MAX});
	ImGui::SetNextWindowBgAlpha(0.4f);
	ImGui::Begin("Watermark", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);

	int &pos = config->misc.watermark.position;

	switch (pos)
	{
	case 0:
		ImGui::SetWindowPos(ImVec2{10.0f, 10.0f}, ImGuiCond_Always);
		break;
	case 1:
		ImGui::SetWindowPos(ImVec2{ImGui::GetIO().DisplaySize.x - ImGui::GetWindowSize().x - 10.0f, 10.0f}, ImGuiCond_Always);
		break;
	case 2:
		ImGui::SetWindowPos(ImGui::GetIO().DisplaySize - ImGui::GetWindowSize() - ImVec2{10.0f, 10.0f}, ImGuiCond_Always);
		break;
	case 3:
		ImGui::SetWindowPos(ImVec2{10.0f, ImGui::GetIO().DisplaySize.y - ImGui::GetWindowSize().y - 10.0f}, ImGuiCond_Always);
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

	constexpr std::array otherOnes = {"gamesense", "neverlose", "aimware", "onetap", "advancedaim", "flowhooks", "ratpoison", "osiris", "rifk7", "novoline", "novihacks", "ev0lve", "ezfrags", "pandora", "luckycharms", "weave", "legendware", "spirthack", "mutinty"};

	std::ostringstream watermark;
	watermark << "NEPS > ";
	watermark << otherOnes[static_cast<int>(memory->globalVars->realtime) % otherOnes.size()];
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
	}
	ImGui::End();

}

void Misc::velocityGraph() noexcept
{

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

	memory->clientMode->getHudChat()->printf(0, " \x1[NEPS]\x8 %s %s voted %c%s\x1", localPlayer->isOtherEnemy(entity) ? "Enemy" : "Teammate", isLocal ? "\x10YOU\x8" : entity->getPlayerName().c_str(), color, votedYes ? "YES" : "NO");
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
			default: return "";
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
		memory->clientMode->getHudChat()->printf(0, " \x1[NEPS]\x8 vote\x4 PASSED\x1");
		break;
	case UserMessageType::VoteFailed:
		memory->clientMode->getHudChat()->printf(0, " \x1[NEPS]\x8 vote\x2 FAILED\x1");
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
	if (interfaces->engine->isConnected()) return;

	constexpr auto nuke = "say \xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9\xE2\x80\xA9";
	constexpr auto basmala = "say \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD \uFDFD";

	if (static Helpers::KeyBindState flag; !flag[config->griefing.chatNuke])
		interfaces->engine->clientCmdUnrestricted(nuke);

	if (static Helpers::KeyBindState flag; !flag[config->griefing.chatBasmala])
		interfaces->engine->clientCmdUnrestricted(basmala);
}
