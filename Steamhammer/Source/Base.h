#pragma once

namespace UAlbertaBot
{

class Base
{
private:
	BWAPI::Position	position;			// upper left corner of the resource depot spot
	BWAPI::Unitset	geysers;			// the base's associated geysers

public:
	BWAPI::Unit		resourceDepot;		// hatchery, etc.
	BWAPI::Player	owner;              // self, enemy, neutral
	bool			reserved;			// if this is a planned expansion

	// The resourceDepot pointer is set for a base if the depot has been seen.
	// It is possible to infer a base location without seeing the depot.

	Base(BWAPI::Position pos);

	void findGeysers();

	void setOwner(BWAPI::Unit depot, BWAPI::Player player);
	BWAPI::Unitset getGeysers() { return geysers; };
};
}