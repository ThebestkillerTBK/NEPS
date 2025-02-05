#pragma once

#include "Vector.h"
#include "Pad.h"

struct UserCmd
{
	enum
	{
		Button_Attack = 1 << 0,
		Button_Jump = 1 << 1,
		Button_Duck = 1 << 2,
		Button_Forward = 1 << 3,
		Button_Back = 1 << 4,
		Button_Use = 1 << 5,
		Button_MoveLeft = 1 << 9,
		Button_MoveRight = 1 << 10,
		Button_Attack2 = 1 << 11,
		Button_Score = 1 << 16,
		Button_Walk = 1 << 18,
		Button_Speed = 1 << 17,
		Button_Zoom = 1 << 19,
		Button_Bullrush = 1 << 22,
		Button_Reload = 1 << 26
	};

	PAD(4)
	int commandNumber;
	int tickCount;
	Vector viewangles;
	Vector aimdirection;
	float forwardmove;
	float sidemove;
	float upmove;
	int buttons;
	char impulse;
	int weaponSelect;
	int weaponSubtype;
	int randomSeed;
	short mousedx;
	short mousedy;
	bool hasBeenPredicted;
};
