#pragma once

#include "VirtualMethod.h"

struct ConVar;
struct ConCommandBase;

class Iterator
{
	public:
	VIRTUAL_METHOD(void, setFirst, 0, (), (this))
	VIRTUAL_METHOD(void, next, 1, (), (this))
	VIRTUAL_METHOD(bool, isValid, 2, (), (this))
	VIRTUAL_METHOD(ConCommandBase *, get, 3, (), (this))
};

class Cvar
{
public:
	VIRTUAL_METHOD(ConVar *, findVar, 15, (const char *name), (this, name))

	VIRTUAL_METHOD(Iterator*, factoryInternalIterator, 42, (), (this))
};