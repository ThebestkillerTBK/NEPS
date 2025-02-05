#include <cassert>

#include "EventListener.h"
#include "lib/fnv.hpp"
#include "GameData.h"
#include "Hacks/Aimbot.h"
#include "Hacks/Misc.h"
#include "Hacks/SkinChanger.h"
#include "Hacks/Visuals.h"
#include "Hacks/Aimbot.h"
#include "Interfaces.h"
#include "SDK/Engine.h"
#include "Config.h"

EventListener::EventListener() noexcept
{
	assert(interfaces);

	interfaces->gameEventManager->addListener(this, "item_purchase");
	interfaces->gameEventManager->addListener(this, "round_start");
	interfaces->gameEventManager->addListener(this, "round_freeze_end");
	interfaces->gameEventManager->addListener(this, "player_hurt");
	interfaces->gameEventManager->addListener(this, "bullet_impact");
	interfaces->gameEventManager->addListener(this, "player_death");
	interfaces->gameEventManager->addListener(this, "vote_cast");
	interfaces->gameEventManager->addListener(this, "weapon_fire");
	interfaces->gameEventManager->addListener(this, "cs_match_end_restart");
	interfaces->gameEventManager->addListener(this, "cs_win_panel_match");

	if (const auto desc = memory->getEventDescriptor(interfaces->gameEventManager, "player_death", nullptr))
		std::swap(desc->listeners[0], desc->listeners[desc->listeners.size - 1]);
	else
		assert(false);
}

void EventListener::remove() noexcept
{
	assert(interfaces);

	interfaces->gameEventManager->removeListener(this);
}

void EventListener::fireGameEvent(GameEvent *event)
{
	switch (fnv::hashRuntime(event->getName()))
	{
	case fnv::hash("round_start"):
		GameData::clearProjectileList();
		GameData::clearPlayersLastLocation();
		Misc::preserveKillfeed(true);
		Misc::damageList(event);
		Aimbot::resetMissCounter();
		[[fallthrough]];
	case fnv::hash("item_purchase"):
	case fnv::hash("round_freeze_end"):
		Misc::purchaseList(event);
		break;
	case fnv::hash("player_death"):
		SkinChanger::updateStatTrak(*event);
		SkinChanger::overrideHudIcon(*event);
		Visuals::killEffect(event);
		Misc::teamDamageList(event);
		Misc::killMessage(*event);
		Misc::playKillSound(*event);
		Misc::playDeathSound(*event);
		Aimbot::handleKill(*event);
		break;
	case fnv::hash("player_hurt"):
		Misc::damageList(event);
		Misc::teamDamageList(event);
		Misc::playHitSound(*event);
		Visuals::hitEffect(event);
		Visuals::hitMarker(event);
		Visuals::damageIndicator(event);
		[[fallthrough]];
	case fnv::hash("cs_win_panel_match"):
		if (config->griefing.autoDisconnect)
			interfaces->engine->clientCmdUnrestricted("disconnect");
		break;
	case fnv::hash("weapon_fire"):
		Aimbot::missCounter(event);
		break;
	case fnv::hash("bullet_impact"):
		Visuals::bulletBeams(event);
		Visuals::bulletImpacts(event);
		break;
	case fnv::hash("vote_cast"):
		Misc::onPlayerVote(*event);
		break;
	case fnv::hash("cs_match_end_restart"):
		Misc::teamDamageList(event);
		break;
	}
}
