#include "KnifeBot.h"

#include "../Config.h"
#include "../GUI.h"
#include "../GameData.h"
#include "../Interfaces.h"
#include "../SDK/Entity.h"
#include "../SDK/EngineTrace.h"

static auto timeToTicks(float time) noexcept
{ 
    return static_cast<int>(0.5f + time / memory->globalVars->intervalPerTick); 
}

void knifeBotRage(UserCmd* cmd) noexcept
{
    if (static Helpers::KeyBindState flag; !flag[config->misc.knifeBot.enabled])
        return;

    if (!localPlayer || !localPlayer->isAlive())
        return;

    const auto activeWeapon = localPlayer->getActiveWeapon();
    if (!activeWeapon || !activeWeapon->isKnife())
        return;

    const auto weaponData = activeWeapon->getWeaponData();
    if (!weaponData)
        return;

    if (activeWeapon->nextPrimaryAttack() > memory->globalVars->serverTime() && activeWeapon->nextSecondaryAttack() > memory->globalVars->serverTime())
        return;

    const auto localPlayerOrigin = localPlayer->origin();
    const auto localPlayerEyePosition = localPlayer->getEyePosition();
    const auto aimPunch = localPlayer->getAimPunch();

    auto bestDistance{ FLT_MAX };
    Entity* bestTarget{ };
    float bestSimulationTime = 0;
    Vector absAngle{ };
    Vector origin{ };
    Vector bestTargetPosition{ };

    for (int i = 1; i <= interfaces->engine->getMaxClients(); i++)
    {
        auto entity = interfaces->entityList->getEntity(i);
        if (!entity || entity == localPlayer.get() || entity->isDormant() || !entity->isAlive()
            || (!entity->isOtherEnemy(localPlayer.get()) && !config->misc.knifeBot.friendly) || entity->gunGameImmunity())
            continue;

        std::array<Matrix3x4, MAX_STUDIO_BONES> bones;
        {
            const auto& boneMatrices = entity->boneCache();
            std::copy(boneMatrices.memory, boneMatrices.memory + boneMatrices.size, bones.begin());
        }

        auto distance{ localPlayerOrigin.distTo(entity->origin()) };

        if (distance < bestDistance) {
            bestDistance = distance;
            bestTarget = entity;
            absAngle = entity->getAbsAngle();
            origin = entity->origin();
            bestSimulationTime = entity->simulationTime();
            bestTargetPosition = bones[7].origin();
        }
    }

    if (!bestTarget || bestDistance > 75.0f)
        return;

    const auto angle{ Helpers::calculateRelativeAngle(localPlayerEyePosition, bestTarget->getBonePosition(7), cmd->viewangles + aimPunch) };
    const bool firstSwing = (localPlayer->nextPrimaryAttack() + 0.4) < memory->globalVars->serverTime();

    bool backStab = false;

    Vector targetForward = Vector::fromAngle(absAngle);
    targetForward.z = 0;

    Vector vecLOS = (origin - localPlayer->origin());
    vecLOS.z = 0;
    vecLOS.normalize();

    float dot = vecLOS.dotProduct(targetForward);

    if (dot > 0.475f)
        backStab = true;

    auto hp = bestTarget->health();
    auto armor = bestTarget->armor() > 1;

    int minDmgSol = 40;
    int minDmgSag = 65;

    if (backStab)
    {
        minDmgSag = 180;
        minDmgSol = 90;
    }
    else if (!firstSwing)
        minDmgSol = 25;

    if (armor)
    {
        minDmgSag = static_cast<int>((float)minDmgSag * 0.85f);
        minDmgSol = static_cast<int>((float)minDmgSol * 0.85f);
    }

    if (hp <= minDmgSag && bestDistance <= 60)
        cmd->buttons |= UserCmd::Button_Attack2;
    else if (hp <= minDmgSol)
        cmd->buttons |= UserCmd::Button_Attack;
    else
        cmd->buttons |= UserCmd::Button_Attack;

    cmd->viewangles += angle;
    cmd->tickCount = timeToTicks(bestSimulationTime);
}

void knifeTrigger(UserCmd* cmd) noexcept
{
    if (!localPlayer || !localPlayer->isAlive())
        return;

    const auto activeWeapon = localPlayer->getActiveWeapon();
    if (!activeWeapon || !activeWeapon->isKnife())
        return;

    const auto weaponData = activeWeapon->getWeaponData();
    if (!weaponData)
        return;

    if (activeWeapon->nextPrimaryAttack() > memory->globalVars->serverTime() && activeWeapon->nextSecondaryAttack() > memory->globalVars->serverTime())
        return;

    Vector startPos = localPlayer->getEyePosition();
    Vector endPos = startPos + Vector::fromAngle(cmd->viewangles) * 48.f;

    bool didHitShort = false;

    Trace trace;
    interfaces->engineTrace->traceRay({ startPos, endPos }, 0x200400B, { localPlayer.get() }, trace);

    if (trace.fraction >= 1.0f)
        interfaces->engineTrace->traceRay({ startPos, endPos, Vector{ -16, -16, -18 },  Vector { 16, 16, 18 } }, 0x200400B, { localPlayer.get() }, trace);


    endPos = startPos + Vector::fromAngle(cmd->viewangles) * 32.f;
    Trace tr;
    interfaces->engineTrace->traceRay({ startPos, endPos }, 0x200400B, { localPlayer.get() }, tr);

    if (tr.fraction >= 1.0f)
        interfaces->engineTrace->traceRay({ startPos, endPos, Vector{ -16, -16, -18 },  Vector { 16, 16, 18 } }, 0x200400B, { localPlayer.get() }, tr);

    didHitShort = tr.fraction < 1.0f;

    if (trace.fraction >= 1.0f)
        return;

    if (!trace.entity || !trace.entity->isPlayer())
        return;

    if (!trace.entity->isOtherEnemy(localPlayer.get()) && !config->misc.knifeBot.friendly)
        return;

    if (trace.entity->gunGameImmunity())
        return;

    std::array<Matrix3x4, MAX_STUDIO_BONES> bones;
    {
        const auto& boneMatrices = trace.entity->boneCache();
        std::copy(boneMatrices.memory, boneMatrices.memory + boneMatrices.size, bones.begin());
    }

    const bool firstSwing = (localPlayer->nextPrimaryAttack() + 0.4) < memory->globalVars->serverTime();

    bool backStab = false;

    Vector targetForward = Vector::fromAngle(trace.entity->getAbsAngle());
    targetForward.z = 0;

    Vector vecLOS = (trace.entity->origin() - localPlayer->origin());
    vecLOS.z = 0;
    vecLOS.normalize();

    float dot = vecLOS.dotProduct(targetForward);

    if (dot > 0.475f)
        backStab = true;

    auto hp = trace.entity->health();
    auto armor = trace.entity->armor() > 1;

    int minDmgSol = 40;
    int minDmgSag = 65;

    if (backStab)
    {
        minDmgSag = 180;
        minDmgSol = 90;
    }
    else if (!firstSwing)
        minDmgSol = 25;

    if (armor)
    {
        minDmgSag = static_cast<int>((float)minDmgSag * 0.85f);
        minDmgSol = static_cast<int>((float)minDmgSol * 0.85f);
    }

    if (hp <= minDmgSag && didHitShort)
        cmd->buttons |= UserCmd::Button_Attack2;
    else if (hp <= minDmgSol)
        cmd->buttons |= UserCmd::Button_Attack;
    else
        cmd->buttons |= UserCmd::Button_Attack;

    cmd->tickCount = timeToTicks(trace.entity->simulationTime() );
}

void KnifeBot::run(UserCmd* cmd) noexcept
{
    if (static Helpers::KeyBindState flag; !flag[config->misc.knifeBot.enabled])
        return;

    if (config->misc.knifeBot.aimbot)
        knifeBotRage(cmd);
    else
        knifeTrigger(cmd);
}