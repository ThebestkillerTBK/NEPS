#include "Visuals.h"

#include "../GameData.h"

#include "../SDK/Beam.h"
#include "../SDK/Cvar.h"
#include "../SDK/ClientMode.h"
#include "../SDK/ConVar.h"

#include "../SDK/DebugOverlay.h"
#include "../SDK/Entity.h"
#include "../SDK/FrameStage.h"
#include "../SDK/GameEvent.h"
#include "../SDK/GlobalVars.h"
#include "../SDK/Input.h"
#include "../SDK/UserCmd.h"
#include "../SDK/Material.h"
#include "../SDK/MaterialSystem.h"
#include "../SDK/NetworkStringTable.h"
#include "../SDK/RenderContext.h"
#include "../SDK/Surface.h"
#include "../SDK/ModelInfo.h"
#include "../SDK/ViewSetup.h"
#include "../SDK/PlayerResource.h"

#include "../lib/fnv.hpp"
#include "../lib/Helpers.hpp"
#include "../lib/ImguiCustom.hpp"

#include <shared_lib/imgui/imgui.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <shared_lib/imgui/imgui_internal.h>

#include <array>
#include <deque>

static int buttons = 0;

void Visuals::runFreeCam(UserCmd* cmd, Vector viewAngles) noexcept
{
	static Vector currentViewAngles = Vector{ };
	static Vector realViewAngles = Vector{ };
	static bool wasCrouching = false;
	static bool hasSetAngles = false;

	buttons = cmd->buttons;

	if (!localPlayer || !localPlayer->isAlive())
		return;

	if (static Helpers::KeyBindState flag; !flag[config->visuals.freeCam])
	{
		if (hasSetAngles)
		{
			interfaces->engine->setViewAngles(realViewAngles);
			cmd->viewangles = currentViewAngles;
			if (wasCrouching)
				cmd->buttons |= UserCmd::Button_Duck;
			wasCrouching = false;
			hasSetAngles = false;
		}
		currentViewAngles = Vector{};
		return;
	}

	if (!currentViewAngles.notNull())
	{
		currentViewAngles = cmd->viewangles;
		realViewAngles = cmd->viewangles;
		wasCrouching = cmd->buttons & UserCmd::Button_Duck;
	}

	cmd->forwardmove = 0;
	cmd->sidemove = 0;
	if (wasCrouching)
		cmd->buttons = UserCmd::Button_Duck;
	else
		cmd->buttons = 0;
	cmd->viewangles = currentViewAngles;
	hasSetAngles = true;
}

void Visuals::freeCam(ViewSetup* setup) noexcept
{
	static Vector newOrigin = Vector{ };

	if (static Helpers::KeyBindState flag; !flag[config->visuals.freeCam])
	{
		newOrigin = Vector{ };
		return;
	}

	if (!localPlayer || !localPlayer->isAlive())
		return;

	float freeCamSpeed = fabsf(static_cast<float>(config->visuals.freeCamSpeed));

	if (!newOrigin.notNull())
		newOrigin = setup->origin;

	Vector forward{ }, right{ }, up{ };

	Vector::fromAngleAll(setup->angles, &forward, &right, &up);

	const bool backBtn = buttons & UserCmd::Button_Back;
	const bool forwardBtn = buttons & UserCmd::Button_Forward;
	const bool rightBtn = buttons & UserCmd::Button_MoveRight;
	const bool leftBtn = buttons & UserCmd::Button_MoveLeft;
	const bool shiftBtn = buttons & UserCmd::Button_Speed;
	const bool duckBtn = buttons & UserCmd::Button_Duck;
	const bool jumpBtn = buttons & UserCmd::Button_Jump;

	if (duckBtn)
		freeCamSpeed *= 0.45;

	if (shiftBtn)
		freeCamSpeed *= 1.65;

	if (forwardBtn)
		newOrigin += forward * freeCamSpeed;

	if (rightBtn)
		newOrigin += right * freeCamSpeed;

	if (leftBtn)
		newOrigin -= right * freeCamSpeed;

	if (backBtn)
		newOrigin -= forward * freeCamSpeed;

	if (jumpBtn)
		newOrigin += up * freeCamSpeed;

	setup->origin = newOrigin;
}

void Visuals::musicKit(FrameStage stage) noexcept
{
	if (!config->visuals.musicKitChanger)
		return;

	auto playerResource = *memory->playerResource;

	if (localPlayer && playerResource)
	{
		playerResource->musicId()[localPlayer->index()] = config->visuals.musicKit + 1;
	}
}
void Visuals::playerModel(FrameStage stage) noexcept
{
	if (stage != FrameStage::NetUpdateEnd && stage != FrameStage::RenderEnd)
		return;

	static int originalIdx = 0;

	if (!localPlayer) {
		originalIdx = 0;
		return;
	}

	constexpr auto getModel = [](Team team) constexpr noexcept -> const char* {
		constexpr std::array models{
		"models/player/custom_player/legacy/ctm_fbi_variantb.mdl",
		"models/player/custom_player/legacy/ctm_fbi_variantf.mdl",
		"models/player/custom_player/legacy/ctm_fbi_variantg.mdl",
		"models/player/custom_player/legacy/ctm_fbi_varianth.mdl",
		"models/player/custom_player/legacy/ctm_sas_variantf.mdl",
		"models/player/custom_player/legacy/ctm_st6_variante.mdl",
		"models/player/custom_player/legacy/ctm_st6_variantg.mdl",
		"models/player/custom_player/legacy/ctm_st6_varianti.mdl",
		"models/player/custom_player/legacy/ctm_st6_variantk.mdl",
		"models/player/custom_player/legacy/ctm_st6_variantm.mdl",
		"models/player/custom_player/legacy/tm_balkan_variantf.mdl",
		"models/player/custom_player/legacy/tm_balkan_variantg.mdl",
		"models/player/custom_player/legacy/tm_balkan_varianth.mdl",
		"models/player/custom_player/legacy/tm_balkan_varianti.mdl",
		"models/player/custom_player/legacy/tm_balkan_variantj.mdl",
		"models/player/custom_player/legacy/tm_leet_variantf.mdl",
		"models/player/custom_player/legacy/tm_leet_variantg.mdl",
		"models/player/custom_player/legacy/tm_leet_varianth.mdl",
		"models/player/custom_player/legacy/tm_leet_varianti.mdl",
		"models/player/custom_player/legacy/tm_phoenix_variantf.mdl",
		"models/player/custom_player/legacy/tm_phoenix_variantg.mdl",
		"models/player/custom_player/legacy/tm_phoenix_varianth.mdl",

		"models/player/custom_player/legacy/tm_pirate.mdl",
		"models/player/custom_player/legacy/tm_pirate_varianta.mdl",
		"models/player/custom_player/legacy/tm_pirate_variantb.mdl",
		"models/player/custom_player/legacy/tm_pirate_variantc.mdl",
		"models/player/custom_player/legacy/tm_pirate_variantd.mdl",
		"models/player/custom_player/legacy/tm_anarchist.mdl",
		"models/player/custom_player/legacy/tm_anarchist_varianta.mdl",
		"models/player/custom_player/legacy/tm_anarchist_variantb.mdl",
		"models/player/custom_player/legacy/tm_anarchist_variantc.mdl",
		"models/player/custom_player/legacy/tm_anarchist_variantd.mdl",
		"models/player/custom_player/legacy/tm_balkan_varianta.mdl",
		"models/player/custom_player/legacy/tm_balkan_variantb.mdl",
		"models/player/custom_player/legacy/tm_balkan_variantc.mdl",
		"models/player/custom_player/legacy/tm_balkan_variantd.mdl",
		"models/player/custom_player/legacy/tm_balkan_variante.mdl",
		"models/player/custom_player/legacy/tm_jumpsuit_varianta.mdl",
		"models/player/custom_player/legacy/tm_jumpsuit_variantb.mdl",
		"models/player/custom_player/legacy/tm_jumpsuit_variantc.mdl",

		"models/player/custom_player/legacy/tm_phoenix_varianti.mdl",
		"models/player/custom_player/legacy/ctm_st6_variantj.mdl",
		"models/player/custom_player/legacy/ctm_st6_variantl.mdl",
		"models/player/custom_player/legacy/tm_balkan_variantk.mdl",
		"models/player/custom_player/legacy/tm_balkan_variantl.mdl",
		"models/player/custom_player/legacy/ctm_swat_variante.mdl",
		"models/player/custom_player/legacy/ctm_swat_variantf.mdl",
		"models/player/custom_player/legacy/ctm_swat_variantg.mdl",
		"models/player/custom_player/legacy/ctm_swat_varianth.mdl",
		"models/player/custom_player/legacy/ctm_swat_varianti.mdl",
		"models/player/custom_player/legacy/ctm_swat_variantj.mdl",
		"models/player/custom_player/legacy/tm_professional_varf.mdl",
		"models/player/custom_player/legacy/tm_professional_varf1.mdl",
		"models/player/custom_player/legacy/tm_professional_varf2.mdl",
		"models/player/custom_player/legacy/tm_professional_varf3.mdl",
		"models/player/custom_player/legacy/tm_professional_varf4.mdl",
		"models/player/custom_player/legacy/tm_professional_varg.mdl",
		"models/player/custom_player/legacy/tm_professional_varh.mdl",
		"models/player/custom_player/legacy/tm_professional_vari.mdl",
		"models/player/custom_player/legacy/tm_professional_varj.mdl"
		};

		switch (team) {
		case Team::TT: return static_cast<std::size_t>(config->visuals.playerModelT - 1) < models.size() ? models[config->visuals.playerModelT - 1] : nullptr;
		case Team::CT: return static_cast<std::size_t>(config->visuals.playerModelCT - 1) < models.size() ? models[config->visuals.playerModelCT - 1] : nullptr;
		default: return nullptr;
		}
	};

	if (const auto model = getModel(localPlayer->getTeamNumber())) {
		if (stage == FrameStage::NetUpdateEnd) {
			originalIdx = localPlayer->modelIndex();
			if (const auto modelprecache = interfaces->networkStringTableContainer->findTable("modelprecache")) {
				modelprecache->addString(false, model);
				const auto viewmodelArmConfig = memory->getPlayerViewmodelArmConfigForPlayerModel(model);
				modelprecache->addString(false, viewmodelArmConfig[2]);
				modelprecache->addString(false, viewmodelArmConfig[3]);
			}
		}

		const auto idx = stage == FrameStage::RenderEnd && originalIdx ? originalIdx : interfaces->modelInfo->getModelIndex(model);

		localPlayer->setModelIndex(idx);

		if (const auto ragdoll = interfaces->entityList->getEntityFromHandle(localPlayer->ragdoll()))
			ragdoll->setModelIndex(idx);
	}
}
void Visuals::colorWorld() noexcept
{
	if (!config->visuals.world.enabled && !config->visuals.sky.enabled && !config->visuals.props.enabled)
		return;

	for (short h = interfaces->materialSystem->firstMaterial(); h != interfaces->materialSystem->invalidMaterial(); h = interfaces->materialSystem->nextMaterial(h))
	{
		const auto mat = interfaces->materialSystem->getMaterial(h);

		if (!mat || mat->isErrorMaterial() || mat->getReferenceCount() < 1)
			continue;

		if (config->visuals.world.enabled && std::strstr(mat->getTextureGroupName(), "World"))
		{
			if (config->visuals.world.rainbow)
				mat->colorModulate(Helpers::rainbowColor(config->visuals.world.rainbowSpeed));
			else
				mat->colorModulate(config->visuals.world.color[0], config->visuals.world.color[1], config->visuals.world.color[2]);
			mat->alphaModulate(config->visuals.world.color[3]);
			mat->setMaterialVarFlag(MaterialVarFlag::NO_DRAW, !config->visuals.world.color[3]);
		} else if (config->visuals.props.enabled && std::strstr(mat->getTextureGroupName(), "StaticProp"))
		{
			if (config->visuals.props.rainbow)
				mat->colorModulate(Helpers::rainbowColor(config->visuals.props.rainbowSpeed));
			else
				mat->colorModulate(config->visuals.props.color[0], config->visuals.props.color[1], config->visuals.props.color[2]);
			mat->alphaModulate(config->visuals.props.color[3]);
			mat->setMaterialVarFlag(MaterialVarFlag::NO_DRAW, !config->visuals.props.color[3]);
		} else if (config->visuals.sky.enabled && std::strstr(mat->getTextureGroupName(), "SkyBox"))
		{
			if (config->visuals.sky.rainbow)
				mat->colorModulate(Helpers::rainbowColor(config->visuals.sky.rainbowSpeed));
			else
				mat->colorModulate(config->visuals.sky.color);
		}
	}
}

void Visuals::modifySmoke(FrameStage stage) noexcept
{
	if (stage != FrameStage::RenderStart && stage != FrameStage::RenderEnd)
		return;

	constexpr std::array smokeMaterials = {
		"particle/vistasmokev1/vistasmokev1_emods",
		"particle/vistasmokev1/vistasmokev1_emods_impactdust",
		"particle/vistasmokev1/vistasmokev1_fire",
		"particle/vistasmokev1/vistasmokev1_smokegrenade"
	};

	for (const auto mat : smokeMaterials)
	{
		const auto material = interfaces->materialSystem->findMaterial(mat);
		if (material)
		{
			material->setMaterialVarFlag(MaterialVarFlag::NO_DRAW, stage == FrameStage::RenderStart && config->visuals.smoke == 1);
			material->setMaterialVarFlag(MaterialVarFlag::WIREFRAME, stage == FrameStage::RenderStart && config->visuals.smoke == 2);
		}
	}
}

void Visuals::modifyFire(FrameStage stage) noexcept
{
	if (stage != FrameStage::RenderStart && stage != FrameStage::RenderEnd)
		return;

	constexpr std::array fireMaterials = {
		"decals/molotovscorch",
		"particle/fire_explosion_1/fire_explosion_1_oriented",
		"particle/fire_burning_character/fire_env_fire_depthblend",
		"particle/fire_burning_character/fire_env_fire",
		"particle/vistasmokev1/vistasmokev1_nearcull_nodepth"
	};

	for (const auto mat : fireMaterials)
	{
		const auto material = interfaces->materialSystem->findMaterial(mat);
		if (material)
		{
			material->setMaterialVarFlag(MaterialVarFlag::NO_DRAW, stage == FrameStage::RenderStart && config->visuals.inferno == 1);
			material->setMaterialVarFlag(MaterialVarFlag::WIREFRAME, stage == FrameStage::RenderStart && config->visuals.inferno == 2);
		}
	}
}

void Visuals::thirdperson() noexcept
{
	if (!localPlayer)
		return;

	if (!config->visuals.thirdPerson.keyMode && !config->visuals.freeCam.keyMode)
	{
		if (localPlayer->isAlive())
			memory->input->isCameraInThirdPerson = false;
		return;
	}

	static Helpers::KeyBindState thirdPerson;
	static Helpers::KeyBindState freeCam;

	if (localPlayer->isAlive())
		memory->input->isCameraInThirdPerson = thirdPerson[config->visuals.thirdPerson] || freeCam[config->visuals.freeCam];
	else if (localPlayer->getObserverTarget() && (localPlayer->observerMode() == ObsMode::InEye || localPlayer->observerMode() == ObsMode::Chase))
	{ 
		memory->input->isCameraInThirdPerson = false;
		localPlayer->observerMode() = thirdPerson[config->visuals.thirdPerson] ? ObsMode::InEye : ObsMode::Chase;
	}
}

void Visuals::removeVisualRecoil(FrameStage stage) noexcept
{
    if (!localPlayer || !localPlayer->isAlive())
        return;

    static Vector aimPunch;
    static Vector viewPunch;

    if (stage == FrameStage::RenderStart) {
        aimPunch = localPlayer->aimPunchAngle();
        viewPunch = localPlayer->viewPunchAngle();

        if (config->visuals.noAimPunch)
            localPlayer->aimPunchAngle() = Vector{ };

        if (config->visuals.noViewPunch)
            localPlayer->viewPunchAngle() = Vector{ };

    } else if (stage == FrameStage::RenderEnd) {
        localPlayer->aimPunchAngle() = aimPunch;
        localPlayer->viewPunchAngle() = viewPunch;
    }
}

void Visuals::removeBlur(FrameStage stage) noexcept
{
    if (stage != FrameStage::RenderStart && stage != FrameStage::RenderEnd)
        return;

    static auto blur = interfaces->materialSystem->findMaterial("dev/scope_bluroverlay");
    blur->setMaterialVarFlag(MaterialVarFlag::NO_DRAW, stage == FrameStage::RenderStart && config->visuals.noBlur);
}

void Visuals::removeGrass(FrameStage stage) noexcept
{
    if (stage != FrameStage::RenderStart && stage != FrameStage::RenderEnd)
        return;

	constexpr auto getGrassMaterialName = []() noexcept -> const char *
	{
		switch (fnv::hashRuntime(interfaces->engine->getLevelName()))
		{
		case fnv::hash("dz_blacksite"): return "detail/detailsprites_survival";
		case fnv::hash("dz_sirocco"): return "detail/dust_massive_detail_sprites";
		case fnv::hash("dz_county"): return "detail/county/detailsprites_county";
		case fnv::hash("coop_autumn"): return "detail/autumn_detail_sprites";
		case fnv::hash("dz_frostbite"): return "ski/detail/detailsprites_overgrown_ski";
			// dz_junglety has been removed in 7/23/2020 patch
			// case fnv::hash("dz_junglety"): return "detail/tropical_grass";
		default: return nullptr;
		}
	};

    if (const auto grassMaterialName = getGrassMaterialName())
        interfaces->materialSystem->findMaterial(grassMaterialName)->setMaterialVarFlag(MaterialVarFlag::NO_DRAW, stage == FrameStage::RenderStart && config->visuals.noGrass);
}

#define DRAW_SCREEN_EFFECT(material) \
{ \
    const auto drawFunction = memory->drawScreenEffectMaterial; \
    int w, h; \
    interfaces->surface->getScreenSize(w, h); \
    __asm { \
        __asm push h \
        __asm push w \
        __asm push 0 \
        __asm xor edx, edx \
        __asm mov ecx, material \
        __asm call drawFunction \
        __asm add esp, 12 \
    } \
}

void Visuals::applyScreenEffects() noexcept
{
    if (!config->visuals.screenEffect)
        return;

    const auto material = interfaces->materialSystem->findMaterial([] {
        constexpr std::array effects{
            "effects/dronecam",
            "effects/underwater_overlay",
            "effects/healthboost",
            "effects/dangerzone_screen"
        };

        if (config->visuals.screenEffect <= 2 || static_cast<std::size_t>(config->visuals.screenEffect - 2) >= effects.size())
            return effects[0];
        return effects[config->visuals.screenEffect - 2];
    }());

    if (config->visuals.screenEffect == 1)
        material->findVar("$c0_x")->setValue(0.0f);
    else if (config->visuals.screenEffect == 2)
        material->findVar("$c0_x")->setValue(0.1f);
    else if (config->visuals.screenEffect >= 4)
        material->findVar("$c0_x")->setValue(1.0f);

    DRAW_SCREEN_EFFECT(material)
}

void Visuals::hitEffect(GameEvent* event) noexcept
{
    if (config->visuals.hitEffect && localPlayer) {
        static float lastHitTime = 0.0f;

        if (event && interfaces->engine->getPlayerFromUserID(event->getInt("attacker")) == localPlayer->index()) {
            lastHitTime = memory->globalVars->realTime;
            return;
        }

		const auto timeSinceHit = memory->globalVars->realTime - lastHitTime;

		if (timeSinceHit > config->visuals.hitEffectTime)
			return;

        constexpr auto getEffectMaterial = [] {
            static constexpr const char* effects[]{
            "effects/dronecam",
            "effects/underwater_overlay",
            "effects/healthboost",
            "effects/dangerzone_screen"
            };

            if (config->visuals.hitEffect <= 2)
                return effects[0];
            return effects[config->visuals.hitEffect - 2];
        };

           
        auto material = interfaces->materialSystem->findMaterial(getEffectMaterial());
        if (config->visuals.hitEffect == 1)
            material->findVar("$c0_x")->setValue(0.0f);
        else if (config->visuals.hitEffect == 2)
            material->findVar("$c0_x")->setValue(1.0f - timeSinceHit / config->visuals.hitEffectTime * 0.1f);
        else if (config->visuals.hitEffect >= 4)
            material->findVar("$c0_x")->setValue(1.0f - timeSinceHit / config->visuals.hitEffectTime);

        DRAW_SCREEN_EFFECT(material)
    }
}

void Visuals::killEffect(GameEvent *event) noexcept
{
	if (config->visuals.killEffect && localPlayer)
	{
		static float lastKillTime = 0.0f;

		if (event && interfaces->engine->getPlayerFromUserID(event->getInt("attacker")) == localPlayer->index())
		{
			lastKillTime = memory->globalVars->realTime;
			return;
		}

		const auto timeSinceKill = memory->globalVars->realTime - lastKillTime;

		if (timeSinceKill > config->visuals.killEffectTime)
			return;

		constexpr auto getEffectMaterial = []
		{
			static constexpr const char *effects[]{
			"effects/dronecam",
			"effects/underwater_overlay",
			"effects/healthboost",
			"effects/dangerzone_screen"
			};

			if (config->visuals.killEffect <= 2)
				return effects[0];
			return effects[config->visuals.killEffect - 2];
		};


		auto material = interfaces->materialSystem->findMaterial(getEffectMaterial());
		if (config->visuals.killEffect == 1)
			material->findVar("$c0_x")->setValue(0.0f);
		else if (config->visuals.killEffect == 2)
			material->findVar("$c0_x")->setValue((1.0f - timeSinceKill / config->visuals.killEffectTime) * 0.1f);
		else if (config->visuals.killEffect >= 4)
			material->findVar("$c0_x")->setValue(1.0f - timeSinceKill / config->visuals.killEffectTime);

		DRAW_SCREEN_EFFECT(material)
	}
}

void Visuals::hitMarker(GameEvent *event, ImDrawList *drawList) noexcept
{
	if (config->visuals.hitMarker == 0)
		return;

	static float lastHitTime = 0.0f;

	if (event)
	{
		if (localPlayer && event->getInt("attacker") == localPlayer->getUserId())
			lastHitTime = memory->globalVars->realTime;
		return;
	}

	const auto timeSinceHit = memory->globalVars->realTime - lastHitTime;

	if (timeSinceHit > config->visuals.hitMarkerTime)
		return;

	switch (config->visuals.hitMarker)
	{
	case 1:
	{
		const auto &mid = ImGui::GetIO().DisplaySize / 2.0f;
		const auto color = Helpers::calculateColor(1.0f, 1.0f, 1.0f, 1.0f - timeSinceHit / config->visuals.hitMarkerTime);
		drawList->AddLine({mid.x - 10, mid.y - 10}, {mid.x - 4, mid.y - 4}, color);
		drawList->AddLine({mid.x + 10.5f, mid.y - 10.5f}, {mid.x + 4.5f, mid.y - 4.5f}, color);
		drawList->AddLine({mid.x + 10.5f, mid.y + 10.5f}, {mid.x + 4.5f, mid.y + 4.5f}, color);
		drawList->AddLine({mid.x - 10, mid.y + 10}, {mid.x - 4, mid.y + 4}, color);
		break;
	}
	case 2:
	{
		const auto &mid = ImGui::GetIO().DisplaySize / 2.0f;
		const auto color = Helpers::calculateColor(1.0f, 1.0f, 1.0f, 1.0f - timeSinceHit / config->visuals.hitMarkerTime);
		drawList->AddCircle(mid, 17.0f, color);
		break;
	}
	}
}

void Visuals::drawReloadProgress(ImDrawList* drawList) noexcept
{

	if (!config->visuals.reloadProgress.enabled)
		return;

	GameData::Lock lock;

	if (!localPlayer || !localPlayer->isAlive())
		return;

	static float reloadLength = 0.0f;
	const auto activeWeapon = localPlayer->getActiveWeapon();

	if (activeWeapon && activeWeapon->isInReload())
	{
		if (!reloadLength)
			reloadLength = activeWeapon->nextPrimaryAttack() - memory->globalVars->currentTime;

		constexpr int segments = 40;
		constexpr float pi = std::numbers::pi_v<float>;
		const auto arc270 = (3 * pi) / 2;
		float reloadTime = activeWeapon->nextPrimaryAttack() - memory->globalVars->currentTime;
		const float fraction = std::clamp((reloadTime / reloadLength), 0.0f, 1.0f);

		drawList->PathArcTo(ImGui::GetIO().DisplaySize / 2.0f + ImVec2{ 1.0f, 1.0f }, 20.0f, arc270 - (2 * pi * fraction), arc270, segments);
		const ImU32 color = Helpers::calculateColor(config->visuals.reloadProgress);
		drawList->PathStroke(color & 0xFF000000, false, config->visuals.reloadProgress.thickness);
		drawList->PathArcTo(ImGui::GetIO().DisplaySize / 2.0f, 20.0f, arc270 - (2 * pi * fraction), arc270, segments);
		drawList->PathStroke(color, false, config->visuals.reloadProgress.thickness);
		std::ostringstream text;
		text << std::fixed << std::showpoint << std::setprecision(1) << reloadTime << " s";
		ImGuiCustom::drawText(drawList, text.str().c_str(), ImGui::GetIO().DisplaySize / 2.0f, color, false, color & IM_COL32_A_MASK);
	}
	else {
		reloadLength = 0.0f;
	}
}

void Visuals::damageIndicator(GameEvent *event, ImDrawList* drawList) noexcept
{
	if (!config->visuals.damageIndicator) return;

	static float lastHitTime = 0.0f;
	static int hitMarkerDmg = 0;
	static bool hitMessage = false;
	static int playerHandle = 0;
	static std::string playerName = "";
	static int playerHealth = 0;
	static int randX = 0;
	static int randY = 0;
	if (event)
	{
		if (localPlayer && event->getInt("attacker") == localPlayer->getUserId())
		{
			lastHitTime = memory->globalVars->realTime;
			hitMarkerDmg = event->getInt("dmg_health");
			hitMessage = true;
			if (event->getInt("userid") == localPlayer->getUserId())
			{
				playerHealth = localPlayer->health();
				playerName = "\x2You\x1";
				return;
			}
			playerHandle = interfaces->entityList->getEntity(interfaces->engine->getPlayerFromUserID(event->getInt("userid")))->handle();
			if (!playerHandle) return;
			playerName = GameData::playerByHandle(playerHandle)->name;
			playerHealth = GameData::playerByHandle(playerHandle)->health - hitMarkerDmg;
			if (playerHealth < 0) playerHealth = 0;
			randX = rand() % 80 - 40;
			randY = rand() % 40 - 20;
		}
		return;
	}

	const auto timeSinceHit = memory->globalVars->realTime - lastHitTime;

	if (timeSinceHit > config->visuals.damageIndicatorTime)
		return;

	std::string dmg = std::to_string(hitMarkerDmg);
	std::string text = "\x1[\x5NEPS\x1] ---> " + dmg + " damage to " + playerName + " (" + std::to_string(playerHealth) + " health)";

	if (hitMessage && config->visuals.damageIndicatorMessage) memory->clientMode->getHudChat()->printf(0, text.c_str());
	hitMessage = false;

	const auto& pos = ImGui::GetIO().DisplaySize / 2.0f + ImVec2{ float(randX), float(randY) };
	const auto color = Helpers::calculateColor(1.0f, 1.0f, 1.0f, 1.0f - timeSinceHit / config->visuals.damageIndicatorTime);
	ImGuiCustom::drawText(drawList, dmg.c_str(), pos, color, false, color & IM_COL32_A_MASK);
}

void Visuals::disablePostProcessing(FrameStage stage) noexcept
{
    if (stage != FrameStage::RenderStart && stage != FrameStage::RenderEnd)
        return;

    *memory->disablePostProcessing = stage == FrameStage::RenderStart && config->visuals.disablePostProcessing;
}

void Visuals::reduceFlashEffect() noexcept
{
    if (localPlayer)
        localPlayer->flashMaxAlpha() = 255.0f - config->visuals.flashReduction * 2.55f;
}

bool Visuals::removeHands(const char* modelName) noexcept
{
    return config->visuals.noHands && std::strstr(modelName, "arms") && !std::strstr(modelName, "sleeve");
}

bool Visuals::removeSleeves(const char* modelName) noexcept
{
    return config->visuals.noSleeves && std::strstr(modelName, "sleeve");
}

bool Visuals::removeWeapons(const char* modelName) noexcept
{
    return config->visuals.noWeapons && std::strstr(modelName, "models/weapons/v_")
        && !std::strstr(modelName, "arms") && !std::strstr(modelName, "tablet")
        && !std::strstr(modelName, "parachute") && !std::strstr(modelName, "fists");
}

void Visuals::skybox(FrameStage stage) noexcept
{
    if (stage != FrameStage::RenderStart && stage != FrameStage::RenderEnd)
        return;

    if (const auto& skyboxes = Helpers::skyboxList; stage == FrameStage::RenderStart && config->visuals.skybox > 0 && static_cast<std::size_t>(config->visuals.skybox) < skyboxes.size()) {
        memory->loadSky(skyboxes[config->visuals.skybox]);
    } else {
        static const auto skyNameVar = interfaces->cvar->findVar("sv_skyname");
        memory->loadSky(skyNameVar->string);
    }
}

void Visuals::bulletBeams(GameEvent *event)
{
	if (!config->visuals.selfBeams.enabled && !config->visuals.allyBeams.enabled && !config->visuals.enemyBeams.enabled)
		return;

	if (!localPlayer)
		return;

	if (!interfaces->engine->isInGame())
		return;

	auto player = interfaces->entityList->getEntity(interfaces->engine->getPlayerFromUserID(event->getInt("userid")));

	if (!player) return;

	constexpr std::array beamSprites = {
		"sprites/physbeam.vmt",
		"sprites/white.vmt",
		"sprites/purplelaser1.vmt",
		"sprites/laserbeam.vmt"
	};

	Config::Visuals::Beams *cfg = nullptr;
	if (localPlayer->isOtherEnemy(player))
		cfg = &config->visuals.enemyBeams;
	else if (player != localPlayer.get())
		cfg = &config->visuals.allyBeams;
	else
		cfg = &config->visuals.selfBeams;

	if (!cfg || !cfg->enabled || static_cast<std::size_t>(cfg->sprite) >= beamSprites.size())
		return;

	const auto activeWeapon = player->getActiveWeapon();
	if (!activeWeapon)
		return;

	if (const auto modelprecache = interfaces->networkStringTableContainer->findTable("modelprecache"))
		modelprecache->addString(false, beamSprites[cfg->sprite]);

	BeamInfo info;

	if (!player->shouldDraw())
	{
		const auto viewModel = interfaces->entityList->getEntityFromHandle(player->viewModel());
		if (!viewModel)
			return;

		if (!viewModel->getAttachment(activeWeapon->getMuzzleAttachmentIndex1stPerson(viewModel), info.start))
			return;
	} else
	{
		const auto worldModel = interfaces->entityList->getEntityFromHandle(activeWeapon->weaponWorldModel());
		if (!worldModel)
			return;

		if (!worldModel->getAttachment(activeWeapon->getMuzzleAttachmentIndex3rdPerson(), info.start))
			return;
	}

	info.end = {event->getFloat("x"), event->getFloat("y"), event->getFloat("z")};
	info.type = TE_BEAMPOINTS;
	info.modelName = beamSprites[cfg->sprite];
	info.haloScale = 0.0f;
	info.life = cfg->life;
	info.width = cfg->width;
	info.endWidth = cfg->width;
	info.fadeLength = 30.0f;
	info.speed = 0.2f;
	info.startFrame = 0;
	info.frameRate = 60.0f;
	info.red = cfg->color[0] * 255.0f;
	info.green = cfg->color[1] * 255.0f;
	info.blue = cfg->color[2] * 255.0f;
	info.brightness = cfg->color[3] * 255.0f;
	info.segments = -1;
	info.renderable = true;
	info.flags = FBEAM_SHADEIN;
	info.amplitude = 0.0f;
	switch (cfg->type)
	{
	case cfg->Type::Line:
		info.flags |= FBEAM_ONLYNOISEONCE;
		break;
	case cfg->Type::Noise:
		info.amplitude = cfg->amplitude * 200.0f / info.start.distTo(info.end);
		break;
	case cfg->Type::Spiral:
		info.flags |= FBEAM_SINENOISE;
		info.amplitude = cfg->amplitude * 0.02f;
		break;
	}
	if (cfg->noiseOnce)
		info.flags |= FBEAM_ONLYNOISEONCE;

	if (const auto beam = memory->viewRenderBeams->createBeamPoints(info))
	{
		beam->flags &= ~FBEAM_FOREVER;
		beam->die = memory->globalVars->currentTime + cfg->life;
	}
}

void Visuals::bulletImpacts(GameEvent* event) noexcept
{
	static std::deque<Vector> positions;

	if (!config->visuals.bulletBox.enabled)
		return;

	if (!localPlayer)
		return;

	if (!interfaces->debugOverlay)
		return;

	if (event->getInt("userid") != localPlayer->getUserId())
		return;

	Vector endPos = Vector{ event->getFloat("x"), event->getFloat("y"), event->getFloat("z") };
	positions.push_front(endPos);

	const int r = static_cast<int>(config->visuals.bulletBox.color[0] * 255.f);
	const int g = static_cast<int>(config->visuals.bulletBox.color[1] * 255.f);
	const int b = static_cast<int>(config->visuals.bulletBox.color[2] * 255.f);
	const int a = static_cast<int>(config->visuals.bulletBox.color[3] * 255.f);

	for (int i = 0; i < static_cast<int>(positions.size()); i++)
	{
		if (!positions.at(i).notNull())
			continue;
		interfaces->debugOverlay->boxOverlay(positions.at(i), Vector{ -2.0f, -2.0f, -2.0f }, Vector{ 2.0f, 2.0f, 2.0f }, Vector{ 0.0f, 0.0f, 0.0f }, r, g, b, a, config->visuals.bulletBoxTime);
	}
	positions.clear();
}

void Visuals::drawMolotovHull(ImDrawList *drawList) noexcept
{
	if (!config->visuals.molotovHull.enabled)
		return;

	const auto color = Helpers::calculateColor(config->visuals.molotovHull);
	const auto color2 = Helpers::calculateColor(config->visuals.molotovHull.color[0], config->visuals.molotovHull.color[1], config->visuals.molotovHull.color[2], 1.0f);

	GameData::Lock lock;

	static const auto flameCircumference = []
	{
		std::array<Vector, 72> points;
		for (std::size_t i = 0; i < points.size(); ++i)
		{
			constexpr auto flameRadius = 60.0f; // https://github.com/perilouswithadollarsign/cstrike15_src/blob/f82112a2388b841d72cb62ca48ab1846dfcc11c8/game/server/cstrike15/Effects/inferno.cpp#L889
			points[i] = Vector{flameRadius * std::cos(Helpers::degreesToRadians(i * (360.0f / points.size()))),
				flameRadius * std::sin(Helpers::degreesToRadians(i * (360.0f / points.size()))),
				0.0f};
		}
		return points;
	}();

	for (const auto &molotov : GameData::infernos())
	{
		for (const auto &pos : molotov.points)
		{
			std::array<ImVec2, flameCircumference.size()> screenPoints;
			std::size_t count = 0;

			for (const auto &point : flameCircumference)
			{
				if (Helpers::worldToScreen(pos + point, screenPoints[count]))
					++count;
			}

			if (count < 1)
				continue;

			std::swap(screenPoints[0], *std::min_element(screenPoints.begin(), screenPoints.begin() + count, [](const auto &a, const auto &b) { return a.y < b.y || (a.y == b.y && a.x < b.x); }));

			constexpr auto orientation = [](const ImVec2 &a, const ImVec2 &b, const ImVec2 &c)
			{
				return (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
			};

			std::sort(screenPoints.begin() + 1, screenPoints.begin() + count, [&](const auto &a, const auto &b) { return orientation(screenPoints[0], a, b) > 0.0f; });

			drawList->AddConvexPolyFilled(screenPoints.data(), count, color);
			if (config->visuals.molotovHull.color[3] != 1.0f)
				drawList->AddPolyline(screenPoints.data(), count, color2, true, config->visuals.molotovHull.thickness);
		}
	}
}

void Visuals::drawSmokeTimer(ImDrawList* drawList) noexcept
{
	if (!config->visuals.smokeTimer.enabled)
		return;

	if (!interfaces->engine->isInGame())
		return;

	GameData::Lock lock;
	for (const auto& smoke : GameData::smokes()) {
		const auto time = std::clamp(smoke.explosionTime + SMOKEGRENADE_LIFETIME - memory->globalVars->realTime, 0.f, SMOKEGRENADE_LIFETIME);
		std::ostringstream text; text << std::fixed << std::showpoint << std::setprecision(1) << time << " sec.";

		auto text_size = ImGui::CalcTextSize(text.str().c_str());
		ImVec2 pos;

		if (Helpers::worldToScreen(smoke.origin, pos)) {
			const auto radius = 10.f + config->visuals.smokeTimer.timerThickness;
			const auto fraction = std::clamp(time / SMOKEGRENADE_LIFETIME, 0.0f, 1.0f);

			Helpers::setAlphaFactor(smoke.fadingAlpha());
			drawList->AddCircle(pos, radius, Helpers::calculateColor(config->visuals.smokeTimer.backgroundColor), 40, 3.0f + config->visuals.smokeTimer.timerThickness);
			if (fraction == 1.0f) {
				drawList->AddCircle(pos, radius, Helpers::calculateColor(config->visuals.smokeTimer.timerColor), 40, 2.0f + config->visuals.smokeTimer.timerThickness);
			}
			else {
				constexpr float pi = std::numbers::pi_v<float>;
				const auto arc270 = (3 * pi) / 2;
				drawList->PathArcTo(pos, radius - 0.5f, arc270 - (2 * pi * fraction), arc270, 40);
				drawList->PathStroke(Helpers::calculateColor(config->visuals.smokeTimer.timerColor), false, 2.0f + config->visuals.smokeTimer.timerThickness);
			}
			drawList->AddText(ImVec2(pos.x - (text_size.x / 2), pos.y + (config->visuals.smokeTimer.timerThickness * 2.f) + (text_size.y / 2)), Helpers::calculateColor(config->visuals.smokeTimer.textColor), text.str().c_str());
			constexpr auto s = "S";
			text_size = ImGui::CalcTextSize(s);
			drawList->AddText(ImVec2(pos.x - (text_size.x / 2), pos.y - (text_size.y / 2)), Helpers::calculateColor(config->visuals.smokeTimer.textColor), s);
			Helpers::setAlphaFactor(1.f);
		}
	}
}

#define IM_NORMALIZE2F_OVER_ZERO(VX,VY)     do { float d2 = VX*VX + VY*VY; if (d2 > 0.0f) { float inv_len = 1.0f / ImSqrt(d2); VX *= inv_len; VY *= inv_len; } } while (0)
#define IM_FIXNORMAL2F(VX,VY)               do { float d2 = VX*VX + VY*VY; if (d2 < 0.5f) d2 = 0.5f; float inv_lensq = 1.0f / d2; VX *= inv_lensq; VY *= inv_lensq; } while (0)

auto generateAntialiasedDot() noexcept
{
	constexpr auto segments = 4;
	constexpr auto radius = 1.0f;

	std::array<ImVec2, segments> circleSegments;

	for (int i = 0; i < segments; ++i) {
		circleSegments[i] = ImVec2{ radius * std::cos(Helpers::degreesToRadians(i * (360.0f / segments) + 45.0f)),
									radius * std::sin(Helpers::degreesToRadians(i * (360.0f / segments) + 45.0f)) };
	}

	// based on ImDrawList::AddConvexPolyFilled()
	const float AA_SIZE = 1.0f; // _FringeScale;
	constexpr int idx_count = (segments - 2) * 3 + segments * 6;
	constexpr int vtx_count = (segments * 2);

	std::array<ImDrawIdx, idx_count> indices;
	std::size_t indexIdx = 0;

	// Add indexes for fill
	for (int i = 2; i < segments; ++i) {
		indices[indexIdx++] = 0;
		indices[indexIdx++] = (i - 1) << 1;
		indices[indexIdx++] = i << 1;
	}

	// Compute normals
	std::array<ImVec2, segments> temp_normals;
	for (int i0 = segments - 1, i1 = 0; i1 < segments; i0 = i1++) {
		const ImVec2& p0 = circleSegments[i0];
		const ImVec2& p1 = circleSegments[i1];
		float dx = p1.x - p0.x;
		float dy = p1.y - p0.y;
		IM_NORMALIZE2F_OVER_ZERO(dx, dy);
		temp_normals[i0].x = dy;
		temp_normals[i0].y = -dx;
	}

	std::array<ImVec2, vtx_count> vertices;
	std::size_t vertexIdx = 0;

	for (int i0 = segments - 1, i1 = 0; i1 < segments; i0 = i1++) {
		// Average normals
		const ImVec2& n0 = temp_normals[i0];
		const ImVec2& n1 = temp_normals[i1];
		float dm_x = (n0.x + n1.x) * 0.5f;
		float dm_y = (n0.y + n1.y) * 0.5f;
		IM_FIXNORMAL2F(dm_x, dm_y);
		dm_x *= AA_SIZE * 0.5f;
		dm_y *= AA_SIZE * 0.5f;

		vertices[vertexIdx++] = ImVec2{ circleSegments[i1].x - dm_x, circleSegments[i1].y - dm_y };
		vertices[vertexIdx++] = ImVec2{ circleSegments[i1].x + dm_x, circleSegments[i1].y + dm_y };

		indices[indexIdx++] = (i1 << 1);
		indices[indexIdx++] = (i0 << 1);
		indices[indexIdx++] = (i0 << 1) + 1;
		indices[indexIdx++] = (i0 << 1) + 1;
		indices[indexIdx++] = (i1 << 1) + 1;
		indices[indexIdx++] = (i1 << 1);
	}

	return std::make_pair(vertices, indices);
}

template <std::size_t N>
auto generateSpherePoints() noexcept
{
	constexpr auto goldenAngle = static_cast<float>(2.399963229728653);

	std::array<Vector, N> points;
	for (std::size_t i = 1; i <= points.size(); ++i) {
		const auto latitude = std::asin(2.0f * i / (points.size() + 1) - 1.0f);
		const auto longitude = goldenAngle * i;

		points[i - 1] = Vector{ std::cos(longitude) * std::cos(latitude), std::sin(longitude) * std::cos(latitude), std::sin(latitude) };
	}
	return points;
};

template <std::size_t VTX_COUNT, std::size_t IDX_COUNT>
void drawPrecomputedPrimitive(
	ImDrawList* drawList, 
	const ImVec2& pos, 
	ImU32 color, 
	const std::array<ImVec2, VTX_COUNT>& vertices,
	const std::array<ImDrawIdx, IDX_COUNT>& indices
) noexcept
{
	drawList->PrimReserve(indices.size(), vertices.size());

	const ImU32 colors[2]{ color, color & ~IM_COL32_A_MASK };
	const auto uv = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
	for (std::size_t i = 0; i < vertices.size(); ++i) {
		drawList->_VtxWritePtr[i].pos = vertices[i] + pos;
		drawList->_VtxWritePtr[i].uv = uv;
		drawList->_VtxWritePtr[i].col = colors[i & 1];
	}
	drawList->_VtxWritePtr += vertices.size();

	std::memcpy(drawList->_IdxWritePtr, indices.data(), indices.size() * sizeof(ImDrawIdx));

	const auto baseIdx = drawList->_VtxCurrentIdx;
	for (std::size_t i = 0; i < indices.size(); ++i)
		drawList->_IdxWritePtr[i] += baseIdx;

	drawList->_IdxWritePtr += indices.size();
	drawList->_VtxCurrentIdx += vertices.size();
}

void Visuals::drawNadeBlast(ImDrawList* drawList) noexcept
{
	if (!config->visuals.nadeBlast.enabled)
		return;

	const auto color = Helpers::calculateColor(config->visuals.nadeBlast);

	static const auto spherePoints = generateSpherePoints<1000>();
	static const auto [vertices, indices] = generateAntialiasedDot();

	constexpr auto blastDuration = 0.35f;

	GameData::Lock lock;
	for (const auto& projectile : GameData::projectiles()) {
		if (!projectile.exploded || projectile.explosionTime + blastDuration < memory->globalVars->realTime)
			continue;

		for (const auto& point : spherePoints) {
			const auto radius = ImLerp(10.0f, 70.0f, (memory->globalVars->realTime - projectile.explosionTime) / blastDuration);
			if (ImVec2 screenPos; Helpers::worldToScreen(projectile.coordinateFrame.origin() + point * radius, screenPos)) {
				drawPrecomputedPrimitive(drawList, screenPos, color, vertices, indices);
			}
		}
	}
}

void Visuals::drawSmokeHull(ImDrawList *drawList) noexcept
{
	if (!config->visuals.smokeHull.enabled)
		return;

	const auto color = Helpers::calculateColor(config->visuals.smokeHull);
	const auto color2 = Helpers::calculateColor(config->visuals.smokeHull.color[0], config->visuals.smokeHull.color[1], config->visuals.smokeHull.color[2], 1.0f);

	GameData::Lock lock;

	static const auto smokeCircumference = []
	{
		std::array<Vector, 72> points;
		for (std::size_t i = 0; i < points.size(); ++i)
		{
			constexpr auto smokeRadius = 150.0f; // https://github.com/perilouswithadollarsign/cstrike15_src/blob/f82112a2388b841d72cb62ca48ab1846dfcc11c8/game/server/cstrike15/Effects/inferno.cpp#L90
			points[i] = Vector{smokeRadius * std::cos(Helpers::degreesToRadians(i * (360.0f / points.size()))),
				smokeRadius * std::sin(Helpers::degreesToRadians(i * (360.0f / points.size()))),
				0.0f};
		}
		return points;
	}();

	for (const auto &smoke : GameData::smokes())
	{
		std::array<ImVec2, smokeCircumference.size()> screenPoints;
		std::size_t count = 0;

		for (const auto &point : smokeCircumference)
		{
			if (Helpers::worldToScreen(smoke.origin + point, screenPoints[count]))
				++count;
		}

		if (count < 1)
			continue;

		std::swap(screenPoints[0], *std::min_element(screenPoints.begin(), screenPoints.begin() + count, [](const auto &a, const auto &b) { return a.y < b.y || (a.y == b.y && a.x < b.x); }));

		constexpr auto orientation = [](const ImVec2 &a, const ImVec2 &b, const ImVec2 &c)
		{
			return (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
		};

		std::sort(screenPoints.begin() + 1, screenPoints.begin() + count, [&](const auto &a, const auto &b) { return orientation(screenPoints[0], a, b) > 0.0f; });

		drawList->AddConvexPolyFilled(screenPoints.data(), count, color);
		if (config->visuals.smokeHull.color[3] != 1.0f)
			drawList->AddPolyline(screenPoints.data(), count, color2, true, config->visuals.smokeHull.thickness);
	}
}

void Visuals::flashlight(FrameStage stage) noexcept
{
	if (stage != FrameStage::RenderStart && stage != FrameStage::RenderEnd)
		return;

	if (!localPlayer)
		return;

	if (static Helpers::KeyBindState flag; flag[config->visuals.flashlight] && stage == FrameStage::RenderStart)
		localPlayer->effectFlags() |= EffectFlag_Flashlight;
	else
		localPlayer->effectFlags() &= ~EffectFlag_Flashlight;
}

void Visuals::playerBounds(ImDrawList *drawList) noexcept
{
	if (!config->visuals.playerBounds.enabled)
		return;

	GameData::Lock lock;
	const auto &local = GameData::local();

	if (!local.exists || !local.alive)
		return;

	Vector max = local.obbMaxs + local.origin;
	Vector min = local.obbMins + local.origin;
	const auto z = local.origin.z;

	ImVec2 points[4];

	bool draw = Helpers::worldToScreen(Vector{max.x, max.y, z}, points[0]);
	draw = draw && Helpers::worldToScreen(Vector{max.x, min.y, z}, points[1]);
	draw = draw && Helpers::worldToScreen(Vector{min.x, min.y, z}, points[2]);
	draw = draw && Helpers::worldToScreen(Vector{min.x, max.y, z}, points[3]);

	if (draw)
	{
		const auto color = Helpers::calculateColor(config->visuals.playerBounds);
		drawList->AddLine(points[0], points[1], color, config->visuals.playerBounds.thickness);
		drawList->AddLine(points[1], points[2], color, config->visuals.playerBounds.thickness);
		drawList->AddLine(points[2], points[3], color, config->visuals.playerBounds.thickness);
		drawList->AddLine(points[3], points[0], color, config->visuals.playerBounds.thickness);
	}
}

void Visuals::playerVelocity(ImDrawList *drawList) noexcept
{
	if (!config->visuals.playerVelocity.enabled)
		return;

	GameData::Lock lock;
	const auto &local = GameData::local();

	if (!local.exists || !local.alive)
		return;

	Vector curDir = local.velocity * 0.12f;
	curDir.z = 0.0f;

	ImVec2 pos, dir;

	bool draw = Helpers::worldToScreen(local.origin, pos);
	draw = draw && Helpers::worldToScreen(curDir + local.origin, dir);

	if (draw)
	{
		const auto color = Helpers::calculateColor(config->visuals.playerVelocity);
		drawList->AddLine(pos, dir, color, config->visuals.playerVelocity.thickness);
		ImGuiCustom::drawText(drawList, std::to_string(static_cast<int>(local.velocity.length())).c_str(), dir, color, config->visuals.playerVelocity.outline, color & IM_COL32_A_MASK);
	}
}

void Visuals::penetrationCrosshair(ImDrawList *drawList) noexcept
{
	// Upcoming
}

void Visuals::drawAimbotFov(ImDrawList* drawList) noexcept
{
	if (!config->visuals.drawAimbotFov.enabled)
		return;

	if (!localPlayer || localPlayer->nextAttack() > memory->globalVars->serverTime() || localPlayer->isDefusing() || localPlayer->waitForNoAttack())
		return;

	const auto activeWeapon = localPlayer->getActiveWeapon();
	if (!activeWeapon || !activeWeapon->clip())
		return;

	if (localPlayer->shotsFired() > 0 && !activeWeapon->isFullAuto())
		return;

	auto weaponIndex = getWeaponIndex(activeWeapon->itemDefinitionIndex2());
	if (!weaponIndex)
		return;

	auto weaponClass = getWeaponClass(activeWeapon->itemDefinitionIndex2());
	if (static Helpers::KeyBindState flag; !flag[config->aimbot[weaponIndex].bind])
		weaponIndex = weaponClass;

	if (static Helpers::KeyBindState flag; !flag[config->aimbot[weaponIndex].bind])
		weaponIndex = 0;

	if (static Helpers::KeyBindState flag; !flag[config->aimbot[weaponIndex].bind])
		return;

	if (!config->aimbot[weaponIndex].betweenShots && (activeWeapon->nextPrimaryAttack() > memory->globalVars->serverTime() || (activeWeapon->isFullAuto() && localPlayer->shotsFired() > 1)))
		return;

	if (!config->aimbot[weaponIndex].ignoreFlash && localPlayer->isFlashed())
		return;

	if (config->aimbot[weaponIndex].scopedOnly && activeWeapon->isSniperRifle() && !localPlayer->isScoped())
		return;

	const auto& screensize = ImGui::GetIO().DisplaySize;
	float radius = std::tan(Helpers::degreesToRadians(config->aimbot[weaponIndex].fov) / 2.f) / std::tan(Helpers::degreesToRadians(localPlayer->isScoped() ? localPlayer->fov() : config->visuals.fov) / 2.f) * screensize.x;
	const ImVec2 screen_mid = { screensize.x / 2.0f, screensize.y / 2.0f };

	const auto aimPunchAngle = localPlayer->getEyePosition() + Vector::fromAngle(interfaces->engine->getViewAngles() + localPlayer->getAimPunch()) * 1000.0f;

	if (ImVec2 pos; Helpers::worldToScreen(aimPunchAngle, pos))
	{
		drawList->AddCircle(localPlayer->shotsFired() > 1 ? pos : screen_mid, radius, Helpers::calculateColor(config->visuals.drawAimbotFov), 360);
		drawList->AddCircleFilled(localPlayer->shotsFired() > 1 ? pos : screen_mid, radius, Helpers::calculateColor(Color4{
			config->visuals.drawAimbotFov.color[0],
			config->visuals.drawAimbotFov.color[1], 
			config->visuals.drawAimbotFov.color[2],
			config->visuals.drawAimbotFov.color[3] * 0.5f
			}), 360);
	}
}