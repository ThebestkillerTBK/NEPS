#include "GrenadePrediction.h"

#include "../SDK/Cvar.h"
#include "../SDK/ConVar.h"
#include "../SDK/Engine.h"
#include "../SDK/Entity.h"
#include "../SDK/GlobalVars.h"
#include "../SDK/Surface.h"

#include "../Config.h"
#include "../GameData.h"
#include "../Memory.h"
#include "../Interfaces.h"

#include "../lib/ImguiCustom.hpp"

#include "shared_lib/imgui/imgui.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "shared_lib/imgui/imgui_internal.h"

#include <mutex>


std::vector<std::pair<ImVec2, ImVec2>> screenPoints;
std::vector<std::pair<ImVec2, ImVec2>> endPoints;
std::vector<std::pair<ImVec2, ImVec2>> savedPoints;

std::mutex renderMutex;

int grenade_act{ 1 };

static bool worldToScreen(const Vector& in, ImVec2& out, bool floor = true) noexcept
{
	const auto& matrix = GameData::toScreenMatrix();

	const auto w = matrix._41 * in.x + matrix._42 * in.y + matrix._43 * in.z + matrix._44;
	if (w < 0.001f)
		return false;

	out = ImGui::GetIO().DisplaySize / 2.0f;
	out.x *= 1.0f + (matrix._11 * in.x + matrix._12 * in.y + matrix._13 * in.z + matrix._14) / w;
	out.y *= 1.0f - (matrix._21 * in.x + matrix._22 * in.y + matrix._23 * in.z + matrix._24) / w;
	if (floor)
		out = ImFloor(out);
	return true;
}

void TraceHull(Vector& src, Vector& end, Trace& tr)
{
	if (!config->misc.nadePredict2)
		return;

	if (!localPlayer)
		return;

	interfaces->engineTrace->traceRay({ src, end, Vector{-2.0f, -2.0f, -2.0f}, Vector{2.0f, 2.0f, 2.0f} }, 0x200400B, { localPlayer.get() }, tr);
}

void Setup(Vector& vecSrc, Vector& vecThrow, Vector viewangles)
{
	auto AngleVectors = [](const Vector & angles, Vector * forward, Vector * right, Vector * up)
	{
		float sr, sp, sy, cr, cp, cy;

		sp = static_cast<float>(sin(double(angles.x) * 0.01745329251f));
		cp = static_cast<float>(cos(double(angles.x) * 0.01745329251f));
		sy = static_cast<float>(sin(double(angles.y) * 0.01745329251f));
		cy = static_cast<float>(cos(double(angles.y) * 0.01745329251f));
		sr = static_cast<float>(sin(double(angles.z) * 0.01745329251f));
		cr = static_cast<float>(cos(double(angles.z) * 0.01745329251f));

		if (forward)
		{
			forward->x = cp * cy;
			forward->y = cp * sy;
			forward->z = -sp;
		}

		if (right)
		{
			right->x = (-1 * sr * sp * cy + -1 * cr * -sy);
			right->y = (-1 * sr * sp * sy + -1 * cr * cy);
			right->z = -1 * sr * cp;
		}

		if (up)
		{
			up->x = (cr * sp * cy + -sr * -sy);
			up->y = (cr * sp * sy + -sr * cy);
			up->z = cr * cp;
		}
	};
	Vector angThrow = viewangles;
	float pitch = angThrow.x;

	if (pitch <= 90.0f)
	{
		if (pitch < -90.0f)
		{
			pitch += 360.0f;
		}
	}
	else
	{
		pitch -= 360.0f;
	}

	float a = pitch - (90.0f - fabs(pitch)) * 10.0f / 90.0f;
	angThrow.x = a;

	float flVel = 750.0f * 0.9f;

	static const float power[] = { 1.0f, 1.0f, 0.5f, 0.0f };
	float b = power[grenade_act];
	b = b * 0.7f;
	b = b + 0.3f;
	flVel *= b;

	Vector vForward, vRight, vUp;
	AngleVectors(angThrow, &vForward, &vRight, &vUp);

	vecSrc = localPlayer->getEyePosition();
	float off = (power[grenade_act] * 12.0f) - 12.0f;
	vecSrc.z += off;

	Trace tr;
	Vector vecDest = vecSrc;
	vecDest += vForward * 22.0f;

	TraceHull(vecSrc, vecDest, tr);

	Vector vecBack = vForward; vecBack *= 6.0f;
	vecSrc = tr.endPos;
	vecSrc -= vecBack;

	vecThrow = localPlayer->velocity(); vecThrow *= 1.25f;
	vecThrow += vForward * flVel;
}

int PhysicsClipVelocity(const Vector& in, const Vector& normal, Vector& out, float overbounce)
{
	static const float STOP_EPSILON = 0.1f;

	float    backoff;
	float    change;
	float    angle;
	int      blocked;

	blocked = 0;

	angle = normal.z;

	if (angle > 0)
	{
		blocked |= 1;        // floor
	}
	if (!angle)
	{
		blocked |= 2;        // step
	}

	backoff = in.dotProduct(normal) * overbounce;

	change = normal.x * backoff;
	out.x = in.x - change;
	if (out.x > -STOP_EPSILON && out.x < STOP_EPSILON)
	{
		out.x = 0;
	}
	change = normal.y * backoff;
	out.y = in.y - change;
	if (out.y > -STOP_EPSILON && out.y < STOP_EPSILON)
	{
		out.y = 0;
	}
	change = normal.z * backoff;
	out.z = in.z - change;
	if (out.z > -STOP_EPSILON && out.z < STOP_EPSILON)
	{
		out.z = 0;
	}

	return blocked;
}

void PushEntity(Vector& src, const Vector& move, Trace& tr)
{
	if (!config->misc.nadePredict2)
		return;

	Vector vecAbsEnd = src;
	vecAbsEnd += move;
	TraceHull(src, vecAbsEnd, tr);
}

void ResolveFlyCollisionCustom(Trace& tr, Vector& vecVelocity, float interval)
{
	if (!config->misc.nadePredict2)
		return;

	// Calculate elasticity
	float flSurfaceElasticity = 1.0;
	float flGrenadeElasticity = 0.45f;
	float flTotalElasticity = flGrenadeElasticity * flSurfaceElasticity;
	if (flTotalElasticity > 0.9f) flTotalElasticity = 0.9f;
	if (flTotalElasticity < 0.0f) flTotalElasticity = 0.0f;

	// Calculate bounce
	Vector vecAbsVelocity;
	PhysicsClipVelocity(vecVelocity, tr.planeNormal, vecAbsVelocity, 2.0f);
	vecAbsVelocity *= flTotalElasticity;

	float flSpeedSqr = vecAbsVelocity.lengthSquared();
	static const float flMinSpeedSqr = 20.0f * 20.0f;

	if (flSpeedSqr < flMinSpeedSqr)
	{
		vecAbsVelocity.x = 0.0f;
		vecAbsVelocity.y = 0.0f;
		vecAbsVelocity.z = 0.0f;
	}

	if (tr.planeNormal.z > 0.7f)
	{
		vecVelocity = vecAbsVelocity;
		vecAbsVelocity *= ((1.0f - tr.fraction) * interval);
		PushEntity(tr.endPos, vecAbsVelocity, tr);
	}
	else
	{
		vecVelocity = vecAbsVelocity;
	}
}

void AddGravityMove(Vector& move, Vector& vel, float frametime, bool onground)
{
	if (!config->misc.nadePredict2)
		return;

	Vector basevel{ 0.0f, 0.0f, 0.0f };

	move.x = (vel.x + basevel.x) * frametime;
	move.y = (vel.y + basevel.y) * frametime;

	if (onground)
	{
		move.z = (vel.z + basevel.z) * frametime;
	}
	else
	{
		float gravity = 800.0f * 0.4f;
		float newZ = vel.z - (gravity * frametime);
		move.z = ((vel.z + newZ) / 2.0f + basevel.z) * frametime;
		vel.z = newZ;
	}
}

enum ACT
{
	ACT_NONE,
	ACT_THROW,
	ACT_LOB,
	ACT_DROP,
};

void Tick(int buttons)
{
	bool in_attack = buttons & UserCmd::Button_Attack;
	bool in_attack2 = buttons & UserCmd::Button_Attack2;

	grenade_act = (in_attack && in_attack2) ? ACT_LOB :
		(in_attack2) ? ACT_DROP :
		(in_attack) ? ACT_THROW :
		ACT_NONE;
}



bool checkDetonate(const Vector& vecThrow, const Trace& tr, int tick, float interval, Entity* activeWeapon)
{
	switch (activeWeapon->itemDefinitionIndex2())
	{
	case WeaponId::SmokeGrenade:
	case WeaponId::Decoy:
		if (vecThrow.length2D() < 0.1f)
		{
			int det_tick_mod = (int)(0.2f / interval);
			return !(tick % det_tick_mod);
		}
		return false;
	case WeaponId::Molotov:
	case WeaponId::IncGrenade:
		if (tr.fraction != 1.0f && tr.planeNormal.z > 0.7f)
			return true;
	case WeaponId::Flashbang:
	case WeaponId::HeGrenade:
		return (float)tick * interval > 1.5f && !(tick % (int)(0.2f / interval));
	default:
		return false;
	}
}

void drawCircle(Vector position, float points, float radius)
{
	float step = 3.141592654f * 2.0f / points;
	ImVec2 end2d{}, start2d{};
	Vector lastPos{};
	for (float a = -step; a < 3.141592654f * 2.0f; a += step) {
		Vector start{ radius * cosf(a) + position.x, radius * sinf(a) + position.y, position.z };

		Trace tr;
		TraceHull(position, start, tr);
		if (!tr.endPos.notNull())
			continue;

		if (worldToScreen(tr.endPos, start2d) && worldToScreen(lastPos, end2d) && lastPos != Vector{ })
		{
			if (start2d.x != 0.f && end2d.x != 0.f && start2d.y != 0.f && end2d.y != 0.f)
				endPoints.emplace_back(std::pair<ImVec2, ImVec2>{ end2d, start2d });
		}
		lastPos = tr.endPos;
	}
}

void NadePrediction::run(UserCmd* cmd) noexcept
{
	renderMutex.lock();

	screenPoints.clear();
	endPoints.clear();

	if (!config->misc.nadePredict2)
	{
		renderMutex.unlock();
		return;
	}

	if (!localPlayer || !localPlayer->isAlive())
	{
		renderMutex.unlock();
		return;
	}

	Tick(cmd->buttons);
	if (localPlayer->moveType() == MoveType::Noclip)
	{
		renderMutex.unlock();
		return;
	}

	auto activeWeapon = localPlayer->getActiveWeapon();
	if (!activeWeapon || !activeWeapon->isGrenade())
	{
		renderMutex.unlock();
		return;
	}

	Vector vecSrc, vecThrow;
	Setup(vecSrc, vecThrow, cmd->viewangles);

	float interval = memory->globalVars->intervalPerTick;
	int logstep = static_cast<int>(0.05f / interval);
	int logtimer = 0;

	std::vector<Vector> path;

	for (unsigned int i = 0; i < path.max_size() - 1; ++i)
	{
		if (!logtimer)
			path.emplace_back(vecSrc);

		Vector move;
		AddGravityMove(move, vecThrow, interval, false);

		// Push entity
		Trace tr;
		PushEntity(vecSrc, move, tr);

		int result = 0;
		if (checkDetonate(vecThrow, tr, i, interval, activeWeapon))
			result |= 1;

		if (tr.fraction != 1.0f)
		{
			result |= 2; // Collision!
			ResolveFlyCollisionCustom(tr, vecThrow, interval);
		}

		vecSrc = tr.endPos;

		if (result & 1)
			break;

		if ((result & 2) || logtimer >= logstep)
			logtimer = 0;
		else
			++logtimer;
	}

	path.emplace_back(vecSrc);

	Vector prev = path[0];
	ImVec2 nadeStart, nadeEnd;
	Vector lastPos{ };
	for (auto& nade : path)
	{
		if (worldToScreen(prev, nadeStart) && worldToScreen(nade, nadeEnd))
		{
			screenPoints.emplace_back(std::pair<ImVec2, ImVec2>{ nadeStart, nadeEnd });
			prev = nade;
			lastPos = nade;
		}
	}

	if(lastPos.notNull())
		drawCircle(lastPos, 120, 150);

	renderMutex.unlock();
}

void NadePrediction::draw() noexcept
{
	if (!config->misc.nadePredict || !config->misc.nadePredict2)
		return;

	if (!localPlayer || !localPlayer->isAlive())
		return;

	if (renderMutex.try_lock())
	{
		savedPoints = screenPoints;

		renderMutex.unlock();
	}

	if (interfaces->engine->isHLTV())
		return;
	

	if (savedPoints.empty())
		return;

	static const auto redColor = ImGui::GetColorU32(ImVec4(1.f, 0.f, 0.f, 1.f));
	static const auto yellowColor = ImGui::GetColorU32(ImVec4(1.f, 1.f, 0.f, 1.f));
	auto greenColor = ImGui::GetColorU32(ImVec4(0.f, 1.f, 0.5f, 1.f));

	auto drawList = ImGui::GetBackgroundDrawList();
	// draw end nade path
	for (auto& point : endPoints) 
		drawList->AddLine(ImVec2(point.first.x, point.first.y), ImVec2(point.second.x, point.second.y), greenColor, 2.f);

	//	draw nade path
	for (auto& point : savedPoints)
		drawList->AddLine(ImVec2(point.first.x, point.first.y), ImVec2(point.second.x, point.second.y), yellowColor, 1.5f);
}

std::string NadeHelper::getNadeName(int type) noexcept
{
	switch (type)
	{
	case Flash:
		return "Flash";
	case Smoke:
		return "Smoke";
	case HE:
		return "HE";
	case Molotov:
		return "Molotov";
	default:
		return "Empty";
	}
}

std::string NadeHelper::getThrowName(int type) noexcept
{
	switch (type)
	{
	case Stand:
		return "Stand";
	case Run:
		return "Run";
	case Walk:
		return "Walk";
	case Jump:
		return "Jump";
	case RunJump:
		return "Run&Jump";
	case WalkJump :
		return "Walk&Jump";
	default:
		return "Empty";
	}
}

bool NadeHelper::isTargetNade(Entity* activeWeapon, int nadeType, bool onlyMatchingInfos) noexcept
{

	switch (activeWeapon->itemDefinitionIndex2())
	{
	case WeaponId::Flashbang:
		if ((nadeType != Flash) && onlyMatchingInfos)
			return false;
		else return true;
	case WeaponId::SmokeGrenade:
		if ((nadeType != Smoke) && onlyMatchingInfos)
			return false;
		else return true;
	case WeaponId::HeGrenade:
		if ((nadeType != HE) && onlyMatchingInfos)
			return false;
		else return true;
	case WeaponId::Molotov:
		if ((nadeType != Molotov) && onlyMatchingInfos)
			return false;
		else return true;
	case WeaponId::IncGrenade:
		if ((nadeType != Molotov) && onlyMatchingInfos)
			return false;
		else return true;
	default:
		return false;
	}
}

bool canNadeHelp() noexcept
{
	if (!interfaces->engine->isInGame() || !interfaces->engine->isConnected() || !localPlayer)
		return false;

	if (const auto mt = localPlayer->moveType(); mt == MoveType::Ladder || mt == MoveType::Noclip)
		return false;

	if (!config->nadeHelper.bind)
		return false;

	return true;
}

void NadeHelper::draw(ImDrawList* drawList) noexcept
{
	if (!canNadeHelp())
		return;

	auto activeWeapon = localPlayer->getActiveWeapon();
	if (!activeWeapon || !activeWeapon->isGrenade())
		return;

	ImVec2 infoPosScreen, crosshairScreen;

	int x, y;
	interfaces->surface->getScreenSize(x, y);
	float cy = y / 2.f;
	float cx = x / 2.f;

	auto nades = config->grenadeInfos;
	for (auto& x : nades)
	{
		if (strstr(interfaces->engine->getLevelName(), x.actMapName.c_str()))
		{
			float dist = localPlayer->origin().distTo(x.pos);
			std::string text = NadeHelper::getNadeName(x.gType) + "|" + x.name + "|" + NadeHelper::getThrowName(x.tType) + "|" + (x.RClick ? "RClick" : "LClick");
			auto size = ImGui::CalcTextSize(text.c_str());

			auto textCol = Helpers::calculateColor(config->nadeHelper.infoText);
			auto BGCol = Helpers::calculateColor(config->nadeHelper.infoBG);
			auto dotCol = Helpers::calculateColor(config->nadeHelper.aimDot);
			auto lineCol = Helpers::calculateColor(config->nadeHelper.aimLine);
			auto shortDist = config->nadeHelper.aimDistance;

			if (!NadeHelper::isTargetNade(activeWeapon, x.gType, config->nadeHelper.onlyMatchingInfos))
				continue;

			Vector crosshair = localPlayer->getEyePosition() + (x.angle.calcDir() * 250.f);
			Vector TCircleOfst = Helpers::calcHelpPos(x.pos);

			if (dist <= config->nadeHelper.renderDistance && Helpers::worldToScreen(x.pos, infoPosScreen))
			{
				drawList->AddRectFilled(ImVec2(infoPosScreen.x - 41.f, infoPosScreen.y - 75.f), ImVec2(infoPosScreen.x + size.x , infoPosScreen.y - 60.f), BGCol);
				drawList->AddText(ImVec2(infoPosScreen.x - text.length() - 15.f, infoPosScreen.y - 75.f), textCol, text.c_str());
				ImGuiCustom::AddRing3D(drawList, x.pos, 15.f, 255, IM_COL32_WHITE, 1.0f);
				ImGuiCustom::AddRing3D(drawList, x.pos, config->nadeHelper.aimDistance / 4.5f, 255, IM_COL32_WHITE, 1.0f);
			}

			if (dist <= shortDist && Helpers::worldToScreen(crosshair, crosshairScreen))
			{
				drawList->AddRectFilled(ImVec2(crosshairScreen.x - 20.f, crosshairScreen.y - 10.f), ImVec2(crosshairScreen.x + size.x + 25.f, crosshairScreen.y + 12.f), BGCol);
				drawList->AddRectFilled(ImVec2(crosshairScreen.x - 20.f, crosshairScreen.y - -10.f), ImVec2(crosshairScreen.x + size.x + 25.f, crosshairScreen.y + 22.f), BGCol);
				drawList->AddCircle(ImVec2(crosshairScreen.x, crosshairScreen.y), 9.f, dotCol);
				drawList->AddCircleFilled(ImVec2(crosshairScreen.x, crosshairScreen.y), 8.f, dotCol);
				drawList->AddCircleFilled(ImVec2(crosshairScreen.x - TCircleOfst.x, crosshairScreen.y - TCircleOfst.y), 2.f, dotCol);
				drawList->AddText(ImVec2(crosshairScreen.x + 12.f, crosshairScreen.y - 7.f), textCol,text.c_str());
				drawList->AddLine(ImVec2(cx, cy), ImVec2(crosshairScreen.x, crosshairScreen.y), lineCol, 2.f);
				drawList->AddCircle(ImVec2(cx, cy), 10.f, dotCol, 255);
			}
		}
	}
}

auto now = 0.0f;
static float nadeTime = 0.0f;

static bool throwing = false;
static bool releasing = false;
static bool stopMove = false;

void releaseNade(UserCmd* cmd, bool RClick)
{
	if (config->nadeHelper.autoThrow && throwing)
		RClick ? cmd->buttons &= ~UserCmd::Button_Attack2 : cmd->buttons &= ~UserCmd::Button_Attack;

	releasing = true;
	throwing = false;
	stopMove = false;
}
void prepareNade(UserCmd* cmd, bool RClick)
{
	if (config->nadeHelper.autoThrow && !releasing)
		RClick ? cmd->buttons |= UserCmd::Button_Attack2 : cmd->buttons |= UserCmd::Button_Attack;
}
		

void autoMove(UserCmd* cmd, int nadeType, bool RClick) noexcept
{
	const float jump = 0.05f;
	const float run = 0.2f;
	const float walkSpeed = 130.f;
	const float runSpeed = 250.f;
	auto& vel = localPlayer->velocity();
	auto speed = vel.length2D();
	switch (nadeType)
	{
	case Stand:
		if (speed < 0.2f)
			releaseNade(cmd, RClick);
		else
			nadeTime = now;
		break;
	case Walk:
		cmd->forwardmove = walkSpeed;
		if (now - nadeTime > run)
			releaseNade(cmd, RClick);
		break;
	case Jump:
		if (speed < 0.2f)
			cmd->buttons |= UserCmd::Button_Jump;
		else
			nadeTime = now;
		if (now - nadeTime > jump)
			releaseNade(cmd, RClick);
		break;
	case Run:
		cmd->forwardmove = 250.f;
		if (now - nadeTime > run)
			releaseNade(cmd, RClick);
		break;
	case RunJump:
		cmd->forwardmove = 250.f;
		if (now - nadeTime > run)
			cmd->buttons |= UserCmd::Button_Jump;
		if (now - nadeTime > run + jump)
			releaseNade(cmd, RClick);
		break;
	case WalkJump:
		cmd->forwardmove = 130.f;
		if (now - nadeTime > run)
			cmd->buttons |= UserCmd::Button_Jump;
		if (now - nadeTime > run + jump)
			releaseNade(cmd, RClick);
		break;
	}
	
}

void NadeHelper::run(UserCmd* cmd) noexcept
{
	if (!canNadeHelp())
		return;

	if (static Helpers::KeyBindState flag; !flag[config->nadeHelper.aimAssist])
		return;

	auto activeWeapon = localPlayer->getActiveWeapon();
	if (!activeWeapon || !activeWeapon->isGrenade())
		return;
	
	auto nades = config->grenadeInfos;
	for (auto& x : nades)
	{
		if (strstr(interfaces->engine->getLevelName(), x.actMapName.c_str()))
		{
			float dist = localPlayer->origin().distTo(x.pos);

			now = memory->globalVars->realTime;

			if (!NadeHelper::isTargetNade(activeWeapon, x.gType, config->nadeHelper.onlyMatchingInfos))
				continue;

			if ((dist > 5.f && dist <= 250.f)|| releasing && config->nadeHelper.autoThrow)
			{
				prepareNade(cmd, x.RClick);
				releasing = false;
			}

			if (dist <= 300.f && config->nadeHelper.throwAssist && throwing)
				autoMove(cmd, x.tType, x.RClick);
			else
			{
				stopMove = false;
				nadeTime = now;
			}

			Vector toTarget = (localPlayer->getAbsOrigin() - x.pos);
			Vector playerViewAngles;
			Vector::AngleVectors(x.angle, playerViewAngles);

			if (dist > 5.f && dist <= 250.f && config->nadeHelper.moveAssist && !throwing && localPlayer->flags() & PlayerFlag_OnGround
				&& !stopMove && toTarget.dotProduct(playerViewAngles) < -0.5f)
			{

				const float yaw = cmd->viewangles.y;
				const auto difference = localPlayer->getRenderOrigin() - x.pos;

				if (difference.length2D() > 1.f)
				{
					const auto f = 180.0f / 3.141592654f;
					const auto velocity = Vector{
						difference.x * std::cos(yaw / f) + difference.y * std::sin(yaw / f),
						difference.y * std::cos(yaw / f) - difference.x * std::sin(yaw / f),
						difference.z };

					cmd->forwardmove = -velocity.x * 20.f;
					cmd->sidemove = velocity.y * 20.f;
				}
			}
			else if(throwing && (x.tType & (Stand | Jump)))
			{
				cmd->forwardmove = 0;
				cmd->sidemove = 0;
			}

			if (dist <= config->nadeHelper.aimDistance)
			{
				int keyPressed = cmd->buttons & (UserCmd::Button_Attack | UserCmd::Button_Attack2);
				throwing = keyPressed;

				Vector angle = x.angle;

				angle.normalize().clamp();

				float fov = Helpers::getFovToPlayer(cmd->viewangles, angle);

				if (fov <= config->nadeHelper.aimFov)
				{
					if (keyPressed)
					{
						if (!config->nadeHelper.silent)
						{
							if (config->nadeHelper.smoothing)
								Helpers::smooth(config->nadeHelper.aimStep, cmd->viewangles, angle, angle, false);

							interfaces->engine->setViewAngles(angle);
						}
						else
							cmd->viewangles = angle;
					}
				}
			}
				
		}
	}
}